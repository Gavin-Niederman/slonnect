/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.app.NotificationCompat
import androidx.core.app.Person
import androidx.core.app.RemoteInput
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import kotlinx.coroutines.launch
import org.kde.kdeconnect_tp.BuildConfig
import org.kde.kdeconnect_tp.R

/**
 * Fake RCS for testing without a SIM, Google account or anyone's real messages
 * (`just emulate`). Everything here is synthetic: invented people with 555-01xx numbers.
 *
 *  - [seed] fills the bridge's demo store (a separate database) with a few conversations;
 *  - [postIncoming] posts a Google-Messages-style conversation notification from this app, which
 *    the bridge captures exactly like a real one (demo mode also bridges this package), with a
 *    reply action;
 *  - [DemoComposeActivity] is a stand-in compose screen, so sending without a notification
 *    goes through the real accessibility path too.
 *
 * Driven over adb in debug builds (see DemoReceiver):
 *   adb shell am broadcast -n <pkg>/dev.slonnect.mobile.bridge.DemoReceiver --es cmd seed
 *   adb shell am broadcast -n <pkg>/dev.slonnect.mobile.bridge.DemoReceiver --es cmd incoming --es from "Ada Lovelace" --es text "Hi!"
 */
object DemoRcs {
    private const val TAG = "Slonnect/DemoRcs"
    private const val CHANNEL = "slonnect_demo_rcs"
    const val ACTION_REPLY = "dev.slonnect.mobile.DEMO_REPLY"
    const val EXTRA_CONVERSATION = "conversation"
    const val KEY_REPLY = "reply"

    private val people = linkedMapOf(
        "Ada Lovelace" to "+15555550101",
        "Grace Hopper" to "+15555550102",
        "Alan Turing" to "+15555550103",
        "Katherine Johnson" to "+15555550104",
        "Margaret Hamilton" to "+15555550105",
    )

    fun numberFor(name: String): String? = people[name]
    fun nameFor(address: String): String = people.entries.firstOrNull { it.value == address.substringBefore(';') }?.key ?: address

    private val me = Person.Builder().setName("You").setKey("me").build()
    private fun person(name: String) = Person.Builder().setName(name).setKey(people[name] ?: name)
        .setUri(people[name]?.let { "tel:$it" }).build()

    /** Conversation title → message history, as the fake messaging app keeps it. */
    private val history = HashMap<String, MutableList<NotificationCompat.MessagingStyle.Message>>()
    private val groups = mapOf("Climbing crew" to listOf("Ada Lovelace", "Alan Turing", "Katherine Johnson"))

    fun seed(context: Context) {
        MessageBridge.update { it.copy(enabled = true, demo = true) }
        val store = MessageBridge.store
        store.clear()
        val now = System.currentTimeMillis()
        val h = 3600_000L
        fun conv(title: String, members: List<String>, msgs: List<Triple<Long, String?, String>>) {
            val tid = store.newSyntheticThreadId()
            // Same key the demo notifications use (their shortcut id), so new messages join the seeded thread.
            val conv = BridgeConversation(tid, "demo:$title", title, members.map { people[it] ?: it }, members.size > 1, now - msgs.minOf { it.first })
            store.upsertConversation(conv)
            for ((ago, from, text) in msgs) {
                val date = now - ago
                store.insert(BridgeMessage(0, tid, text, date, from == null, if (members.size > 1) from?.let { people[it] } else null, "rcs"),
                    "seed|$title|$date|${text.hashCode()}")
            }
            store.upsertConversation(conv.copy(lastRcsAt = now - msgs.minOf { it.first }))
        }
        conv("Ada Lovelace", listOf("Ada Lovelace"), listOf(
            Triple(50 * h, "Ada Lovelace", "Did the analytical engine notes arrive?"),
            Triple(49 * h, null, "Yes! Reading them tonight 📚"),
            Triple(26 * h, "Ada Lovelace", "Note G is the fun one"),
            Triple(2 * h, "Ada Lovelace", "Lunch on Thursday?"),
        ))
        conv("Grace Hopper", listOf("Grace Hopper"), listOf(
            Triple(30 * h, null, "Found the bug. It was literally a moth."),
            Triple(29 * h, "Grace Hopper", "Tape it in the logbook 🦋"),
            Triple(20 * 60_000L, "Grace Hopper", "A ship in port is safe, but that's not what ships are built for."),
        ))
        conv("Climbing crew", listOf("Ada Lovelace", "Alan Turing", "Katherine Johnson"), listOf(
            Triple(72 * h, "Alan Turing", "Gym Saturday 10am?"),
            Triple(71 * h, "Katherine Johnson", "I'll do the trajectory math for the overhang"),
            Triple(70 * h, null, "In!"),
            Triple(5 * h, "Ada Lovelace", "Who has the rope?"),
        ))
        conv("Margaret Hamilton", listOf("Margaret Hamilton"), listOf(
            Triple(8 * 24 * h, "Margaret Hamilton", "Priority displays are go."),
            Triple(8 * 24 * h - 600_000, null, "🚀"),
        ))
        Log.i(TAG, "seeded demo RCS history")
        MessageBridge.latestMessages().let { latest -> if (latest.isNotEmpty()) notifyListeners(latest) }
    }

    // Same path as a captured message, so the desktop refreshes.
    private fun notifyListeners(messages: List<org.kde.kdeconnect.helpers.SMSHelper.Message>) = MessageBridge.dispatch(messages)

    @SuppressLint("MissingPermission")
    fun postIncoming(context: Context, from: String, text: String, group: String? = null) {
        val nm = context.getSystemService<NotificationManager>() ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            nm.createNotificationChannel(NotificationChannel(CHANNEL, "Demo RCS conversations", NotificationManager.IMPORTANCE_DEFAULT))
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "can't post demo notifications: permission missing")
            return
        }
        val title = group ?: from
        val msgs = history.getOrPut(title) { mutableListOf() }
        msgs += NotificationCompat.MessagingStyle.Message(text, System.currentTimeMillis(), person(from))
        show(context, title)
    }

    private fun show(context: Context, title: String) {
        val nm = context.getSystemService<NotificationManager>() ?: return
        val style = NotificationCompat.MessagingStyle(me).setGroupConversation(groups.containsKey(title))
        if (groups.containsKey(title)) style.setConversationTitle(title)
        history[title].orEmpty().takeLast(8).forEach { style.addMessage(it) }

        val replyIntent = Intent(context, DemoReceiver::class.java).setAction(ACTION_REPLY).putExtra(EXTRA_CONVERSATION, title)
        val pending = PendingIntent.getBroadcast(context, title.hashCode(), replyIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE)
        val reply = NotificationCompat.Action.Builder(R.drawable.ic_notification, "Reply", pending)
            .addRemoteInput(RemoteInput.Builder(KEY_REPLY).setLabel("Message").build())
            .setSemanticAction(NotificationCompat.Action.SEMANTIC_ACTION_REPLY)
            .build()
        val n = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setStyle(style)
            .setShortcutId("demo:$title")
            .addAction(reply)
            .setOnlyAlertOnce(true)
            .build()
        nm.notify(title.hashCode(), n)
    }

    /** The reply action of a demo notification: show it as sent, then answer back. */
    fun onReply(context: Context, title: String, text: String) {
        history.getOrPut(title) { mutableListOf() } += NotificationCompat.MessagingStyle.Message(text, System.currentTimeMillis(), null as Person?)
        show(context, title)
        val responder = groups[title]?.random() ?: title
        Handler(Looper.getMainLooper()).postDelayed({ postIncoming(context, responder, "👍 got “${text.take(30)}”", groups[title]?.let { title }) }, 2500)
    }
}

/** adb entry point for the demo (debug builds only) and the demo notifications' reply action. */
class DemoReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == DemoRcs.ACTION_REPLY) {
            val text = RemoteInput.getResultsFromIntent(intent)?.getCharSequence(DemoRcs.KEY_REPLY)?.toString() ?: return
            DemoRcs.onReply(context, intent.getStringExtra(DemoRcs.EXTRA_CONVERSATION) ?: return, text)
            return
        }
        if (!BuildConfig.DEBUG) return
        when (intent.getStringExtra("cmd")) {
            "seed" -> DemoRcs.seed(context)
            "incoming" -> {
                if (!MessageBridge.settings.value.demo) DemoRcs.seed(context)
                DemoRcs.postIncoming(context, intent.getStringExtra("from") ?: "Ada Lovelace",
                    intent.getStringExtra("text") ?: "Hello from the demo", intent.getStringExtra("group"))
            }
            "off" -> MessageBridge.update { it.copy(demo = false) }
            // Shizuku's permission prompt, so tools/emulator-shizuku.sh can press Allow.
            "shizuku-permission" -> dev.slonnect.mobile.privileged.Privileged.requestPermission()
            "shizuku-state" -> {
                dev.slonnect.mobile.privileged.Privileged.refresh(context)
                Log.i("Slonnect/Node", "shizuku: ${dev.slonnect.mobile.privileged.Privileged.state.value}")
            }
            // Run one phone node without a desktop: --es node shell --es inputs '{"command":"id"}'.
            // The result goes to logcat (tag Slonnect/Node).
            "node" -> {
                val id = intent.getStringExtra("node") ?: return
                val inputs = runCatching { kotlinx.serialization.json.Json.parseToJsonElement(intent.getStringExtra("inputs") ?: "{}") as kotlinx.serialization.json.JsonObject }
                    .getOrElse { kotlinx.serialization.json.JsonObject(emptyMap()) }
                val node = dev.slonnect.mobile.nodes.PhoneNodes.find(id) ?: run { Log.w("Slonnect/Node", "no node $id"); return }
                val pending = goAsync()
                kotlinx.coroutines.CoroutineScope(kotlinx.coroutines.Dispatchers.Default).launch {
                    try {
                        val r = node.run(context.applicationContext, dev.slonnect.mobile.nodes.NodeInputs(inputs))
                        Log.i("Slonnect/Node", "$id ok flows=${r.flows} outputs=${r.outputs} log=${r.log}")
                    } catch (e: Exception) {
                        Log.i("Slonnect/Node", "$id failed: ${e.message}")
                    } finally {
                        pending.finish()
                    }
                }
            }
        }
    }
}

/**
 * Stand-in for the messaging app's compose screen in demo mode: it accepts smsto:/sms_body like
 * Google Messages, and its Send button is what BridgeAccessibilityService presses.
 */
class DemoComposeActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val to = intent.data?.schemeSpecificPart.orEmpty()
        val initial = intent.getStringExtra("sms_body").orEmpty()
        setContent {
            MaterialTheme {
                Surface(Modifier.fillMaxSize()) {
                    var text by remember { mutableStateOf(initial) }
                    Column(Modifier.safeDrawingPadding().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        Text("Demo messages · to $to", style = MaterialTheme.typography.titleMedium)
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(text, { text = it }, Modifier.weight(1f))
                            FilledIconButton(onClick = {
                                val name = DemoRcs.nameFor(to)
                                DemoRcs.onReply(this@DemoComposeActivity, name, text)
                                finish()
                            }) { Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Send") }
                        }
                    }
                }
            }
        }
    }
}
