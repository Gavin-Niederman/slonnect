/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.bridge

import android.app.Notification
import android.app.PendingIntent
import android.app.RemoteInput
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.provider.ContactsContract
import android.provider.Telephony
import android.service.notification.StatusBarNotification
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.edit
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import org.kde.kdeconnect.helpers.SMSHelper
import org.kde.kdeconnect.plugins.notifications.NotificationReceiver
import java.util.concurrent.CopyOnWriteArrayList

/**
 * The Messages bridge: another way to get RCS in and out of the phone, without Google Messages
 * for Web.
 *
 * Android has no public RCS API and Google Messages keeps RCS chats in its private database, so
 * they never reach the SMS provider KDE Connect reads. The bridge instead:
 *  - **receives** by reading the conversation notifications of the messaging app
 *    (MessagingStyle: sender, text, time, group title, plus the earlier messages shown in the
 *    notification) and keeping a local copy (BridgeStore). Messages that also exist in the SMS
 *    provider are dropped, so only RCS and other non-SMS messages are added;
 *  - **merges** them into the normal `kdeconnect.sms.*` packets (SMSPlugin), so kdeconnectd and
 *    every SMS client on the desktop see RCS chats as ordinary threads;
 *  - **sends** through the notification's own reply action when there is one (works with the
 *    screen off), or else opens the messaging app with the text filled in and lets
 *    [BridgeAccessibilityService] press Send. The messaging app picks RCS or SMS itself.
 *
 * Limits: only messages that produced a notification are seen (not ones read on the phone
 * first, or sent from the phone itself), images arrive as "[image]", and sending without a
 * reply action needs the phone unlocked. See mobile/SLONNECT.md.
 */
object MessageBridge {
    private const val TAG = "Slonnect/Bridge"
    private const val PREFS = "slonnect_bridge"

    /** Apps whose conversation notifications are bridged. */
    val messagingApps = listOf("com.google.android.apps.messaging", "com.samsung.android.messaging")

    enum class SendMode { RCS_THREADS, ALWAYS, NEVER }

    data class Settings(val enabled: Boolean, val sendMode: SendMode, val demo: Boolean)

    private lateinit var appContext: Context
    private lateinit var prefs: SharedPreferences
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _settings = MutableStateFlow(Settings(false, SendMode.RCS_THREADS, false))
    val settings: StateFlow<Settings> = _settings.asStateFlow()

    private var realStore: BridgeStore? = null
    private var demoStore: BridgeStore? = null
    val store: BridgeStore
        get() = if (_settings.value.demo) demoStore ?: BridgeStore(appContext, "slonnect_bridge_demo.db").also { demoStore = it }
        else realStore ?: BridgeStore(appContext).also { realStore = it }

    /** Called with new messages, already converted for the SMS plugin. */
    fun interface Listener {
        fun onBridgeMessages(messages: List<SMSHelper.Message>)
    }

    private val listeners = CopyOnWriteArrayList<Listener>()
    fun addListener(l: Listener) = listeners.add(l)
    fun removeListener(l: Listener) = listeners.remove(l)
    fun dispatch(messages: List<SMSHelper.Message>) = listeners.forEach { it.onBridgeMessages(messages) }

    /** Latest reply action per conversation key, from the messaging app's notification. */
    private val replyActions = HashMap<String, Notification.Action>()

    private val _stats = MutableStateFlow(0)
    /** Bumped whenever the store changes, for the UI. */
    val changes: StateFlow<Int> = _stats.asStateFlow()

    private val notificationListener = object : NotificationReceiver.NotificationListener {
        override fun onNotificationPosted(statusBarNotification: StatusBarNotification) = capture(statusBarNotification)
        override fun onNotificationRemoved(statusBarNotification: StatusBarNotification) {}
        override fun onListenerConnected(service: NotificationReceiver) {
            runCatching { service.activeNotifications }.getOrNull()?.forEach { capture(it) }
        }
    }

    fun init(context: Context) {
        appContext = context.applicationContext
        prefs = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        _settings.value = Settings(
            prefs.getBoolean("enabled", false),
            runCatching { SendMode.valueOf(prefs.getString("sendMode", null) ?: "") }.getOrDefault(SendMode.RCS_THREADS),
            prefs.getBoolean("demo", false),
        )
        if (_settings.value.enabled) listen()
    }

    fun update(change: (Settings) -> Settings) {
        val before = _settings.value
        val after = change(before)
        _settings.value = after
        prefs.edit {
            putBoolean("enabled", after.enabled)
            putString("sendMode", after.sendMode.name)
            putBoolean("demo", after.demo)
        }
        if (after.enabled && !before.enabled) listen()
        _stats.value++
    }

    private fun listen() {
        if (!NotificationReceiver.hasReadNotificationsPermission(appContext)) return
        NotificationReceiver.RunCommand(appContext) { it.addListener(notificationListener) }
    }

    val hasNotificationAccess: Boolean get() = NotificationReceiver.hasReadNotificationsPermission(appContext)

    // --- Receiving -------------------------------------------------------------------------

    private fun isBridged(pkg: String) =
        pkg in messagingApps || (_settings.value.demo && pkg == appContext.packageName)

    fun capture(sbn: StatusBarNotification) {
        val s = _settings.value
        if (!s.enabled || !isBridged(sbn.packageName)) return
        val n = sbn.notification
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        val style = NotificationCompat.MessagingStyle.extractMessagingStyleFromNotification(n) ?: return
        val key = n.shortcutId ?: style.conversationTitle?.toString() ?: sbn.tag ?: sbn.key

        n.actions?.firstOrNull { a ->
            !a.remoteInputs.isNullOrEmpty() && (a.semanticAction == Notification.Action.SEMANTIC_ACTION_REPLY || a.semanticAction == 0)
        }?.let { synchronized(replyActions) { replyActions[key] = it } }

        val messages = style.messages.toList()
        if (messages.isEmpty()) return
        scope.launch { ingest(key, style, messages) }
    }

    private fun ingest(key: String, style: NotificationCompat.MessagingStyle, messages: List<NotificationCompat.MessagingStyle.Message>) {
        val isGroup = style.isGroupConversation
        val others = messages.mapNotNull { it.person }.filter { it.key != style.user.key || it.name != style.user.name }
            .distinctBy { it.key ?: it.name?.toString() }
        val addresses = others.map { resolveAddress(it.uri, it.name?.toString()) }.distinct()
        val title = style.conversationTitle?.toString() ?: others.firstOrNull()?.name?.toString() ?: key

        val store = store
        val existing = store.conversationByKey(key)
        val threadId = existing?.threadId ?: telephonyThread(addresses) ?: store.newSyntheticThreadId()
        val conv = BridgeConversation(threadId, key, title, existing?.addresses?.union(addresses)?.toList() ?: addresses, isGroup,
            existing?.lastRcsAt ?: 0)

        val added = mutableListOf<BridgeMessage>()
        for (m in messages) {
            val body = m.text?.toString() ?: (if (m.dataUri != null) "[image]" else continue)
            val outgoing = m.person == null || (m.person?.name == style.user.name && m.person?.key == style.user.key)
            val sender = if (outgoing) null else m.person?.let { resolveAddress(it.uri, it.name?.toString()) }
            // Already in the SMS provider (plain SMS/MMS), or something we sent through the bridge.
            if (!s_demo() && inSmsProvider(threadId, body, m.timestamp)) continue
            if (outgoing && store.hasSimilar(threadId, body, m.timestamp, 120_000)) continue
            val dedup = "$key|${m.timestamp}|${sender ?: "me"}|${body.hashCode()}"
            val msg = BridgeMessage(0, threadId, body, m.timestamp, outgoing, if (isGroup) sender else null, "rcs")
            store.insert(msg, dedup)?.let { added += it }
        }
        if (added.isEmpty() && existing != null) return
        store.upsertConversation(conv.copy(lastRcsAt = maxOf(conv.lastRcsAt, added.maxOfOrNull { it.date } ?: 0)))
        if (added.isNotEmpty()) {
            Log.i(TAG, "bridged ${added.size} message(s) into thread $threadId")
            _stats.value++
            val converted = added.map { toSmsMessage(it, conv) }
            listeners.forEach { it.onBridgeMessages(converted) }
        }
    }

    private fun s_demo() = _settings.value.demo

    private fun resolveAddress(uri: String?, name: String?): String {
        if (uri != null) {
            when {
                uri.startsWith("tel:") -> return Uri.decode(uri.removePrefix("tel:"))
                uri.startsWith("mailto:") -> return Uri.decode(uri.removePrefix("mailto:"))
            }
        }
        if (name.isNullOrBlank()) return "Unknown"
        if (_settings.value.demo) return DemoRcs.numberFor(name) ?: name
        // A contact with exactly this display name: use its first phone number.
        return runCatching {
            appContext.contentResolver.query(
                ContactsContract.CommonDataKinds.Phone.CONTENT_URI,
                arrayOf(ContactsContract.CommonDataKinds.Phone.NUMBER),
                "${ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME} = ?", arrayOf(name), null,
            )?.use { if (it.moveToFirst()) it.getString(0) else null }
        }.getOrNull() ?: name
    }

    private fun telephonyThread(addresses: List<String>): Long? {
        if (_settings.value.demo || addresses.isEmpty() || addresses.any { !it.any(Char::isDigit) }) return null
        return runCatching { Telephony.Threads.getOrCreateThreadId(appContext, addresses.toSet()) }.getOrNull()
    }

    private fun inSmsProvider(threadId: Long, body: String, date: Long): Boolean {
        if (threadId >= BridgeStore.SYNTHETIC_BASE) return false
        return runCatching {
            appContext.contentResolver.query(
                Telephony.Sms.CONTENT_URI, arrayOf(Telephony.Sms._ID),
                "${Telephony.Sms.THREAD_ID} = ? AND ${Telephony.Sms.BODY} = ? AND ${Telephony.Sms.DATE} BETWEEN ? AND ?",
                arrayOf(threadId.toString(), body, (date - 120_000).toString(), (date + 120_000).toString()), null,
            )?.use { it.moveToFirst() } ?: false
        }.getOrDefault(false)
    }

    // --- Feeding the SMS plugin -------------------------------------------------------------

    fun toSmsMessage(m: BridgeMessage, conv: BridgeConversation): SMSHelper.Message {
        val addresses = (if (m.outgoing || conv.isGroup) conv.addresses else listOfNotNull(m.sender ?: conv.addresses.firstOrNull()))
            .ifEmpty { listOf(conv.title) }
        var event = SMSHelper.Message.EVENT_TEXT_MESSAGE
        if (conv.isGroup || addresses.size > 1) event = event or SMSHelper.Message.EVENT_MULTI_TARGET
        return SMSHelper.Message(
            addresses.map { SMSHelper.Address(it) },
            m.body,
            m.date,
            if (m.outgoing) Telephony.Sms.MESSAGE_TYPE_SENT else Telephony.Sms.MESSAGE_TYPE_INBOX,
            1,
            SMSHelper.ThreadID(m.threadId),
            BridgeStore.UID_BASE + m.uid,
            event,
            -1,
            null,
            buildJsonObject {
                put("transport", JsonPrimitive(m.transport))
                put("bridge", JsonPrimitive(true))
                m.sender?.let { put("sender", JsonPrimitive(it)) }
            },
        )
    }

    /** Newest bridged message of every conversation (for the conversation list). */
    fun latestMessages(): List<SMSHelper.Message> {
        if (!_settings.value.enabled) return emptyList()
        val store = store
        return store.allConversations().mapNotNull { conv -> store.newest(conv.threadId)?.let { toSmsMessage(it, conv) } }
    }

    /** Bridged messages of one thread, newest first, only those older than [before] if given. */
    fun messages(threadId: Long, before: Long? = null, limit: Long? = null): List<SMSHelper.Message> {
        if (!_settings.value.enabled) return emptyList()
        val store = store
        val conv = store.conversation(threadId) ?: return emptyList()
        return store.messages(threadId, before = before, limit = limit).map { toSmsMessage(it, conv) }
    }

    // --- Sending ----------------------------------------------------------------------------

    private fun normalize(address: String) = address.filter(Char::isDigit).takeLast(10).ifEmpty { address.lowercase() }

    private fun conversationFor(addresses: List<String>): BridgeConversation? {
        val want = addresses.map(::normalize).toSet()
        return store.allConversations().firstOrNull { c -> c.addresses.map(::normalize).toSet() == want }
    }

    /** Should a message to these addresses go through the bridge instead of plain SMS? */
    fun shouldSend(addresses: List<String>): Boolean {
        val s = _settings.value
        if (!s.enabled) return false
        return when (s.sendMode) {
            SendMode.NEVER -> false
            SendMode.ALWAYS -> true
            SendMode.RCS_THREADS -> conversationFor(addresses)?.let { System.currentTimeMillis() - it.lastRcsAt < 30L * 24 * 3600 * 1000 } ?: false
        }
    }

    /**
     * Sends through the messaging app. Calls [onResult] with null on success or the reason it
     * couldn't; the caller falls back to SMS then.
     */
    fun send(addresses: List<String>, body: String, onResult: (String?) -> Unit) {
        val conv = conversationFor(addresses)
        val action = conv?.let { synchronized(replyActions) { replyActions[it.key] } }
        if (action != null && replyThrough(action, body)) {
            recordOutgoing(conv, addresses, body)
            return onResult(null)
        }
        BridgeAccessibilityService.compose(appContext, addresses, body, if (_settings.value.demo) appContext.packageName else null) { error ->
            if (error == null) recordOutgoing(conv, addresses, body)
            onResult(error)
        }
    }

    private fun replyThrough(action: Notification.Action, body: String): Boolean = try {
        val inputs = action.remoteInputs
        val results = Bundle().apply { inputs.forEach { putCharSequence(it.resultKey, body) } }
        val intent = Intent()
        RemoteInput.addResultsToIntent(inputs, intent, results)
        action.actionIntent.send(appContext, 0, intent)
        true
    } catch (e: PendingIntent.CanceledException) {
        Log.i(TAG, "reply action expired, falling back to the app")
        false
    }

    private fun recordOutgoing(existing: BridgeConversation?, addresses: List<String>, body: String) = scope.launch {
        val store = store
        val conv = existing ?: BridgeConversation(
            telephonyThread(addresses) ?: store.newSyntheticThreadId(), "to:" + addresses.joinToString(","),
            addresses.joinToString(", "), addresses, addresses.size > 1, 0,
        ).also { store.upsertConversation(it) }
        val now = System.currentTimeMillis()
        val msg = store.insert(BridgeMessage(0, conv.threadId, body, now, true, null, "rcs"), "out|${conv.key}|$now|${body.hashCode()}")
            ?: return@launch
        store.upsertConversation(conv.copy(lastRcsAt = maxOf(conv.lastRcsAt, now)))
        _stats.value++
        val converted = listOf(toSmsMessage(msg, conv))
        listeners.forEach { it.onBridgeMessages(converted) }
    }
}
