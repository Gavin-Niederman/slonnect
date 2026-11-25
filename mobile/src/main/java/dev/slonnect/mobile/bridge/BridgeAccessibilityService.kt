/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.bridge

import android.accessibilityservice.AccessibilityService
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import androidx.core.content.getSystemService

/**
 * Sends a message through the messaging app when there's no notification to reply to: the
 * bridge opens a compose screen with the text filled in (smsto: + sms_body), and this service
 * presses its Send button, then goes back. It only acts on a request the bridge just made and
 * only looks at the messaging apps listed in res/xml/slonnect_bridge_accessibility.xml.
 */
class BridgeAccessibilityService : AccessibilityService() {

    override fun onServiceConnected() {
        instance = this
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    override fun onInterrupt() {}

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        val req = pending ?: return
        if (event?.packageName?.toString() != req.pkg) return
        val root = rootInActiveWindow ?: return
        if (!containsText(root, req.body)) return
        val send = findSend(root) ?: return
        if (send.performAction(AccessibilityNodeInfo.ACTION_CLICK)) {
            finish(null)
            main.postDelayed({ performGlobalAction(GLOBAL_ACTION_BACK) }, 600)
        }
    }

    private fun containsText(node: AccessibilityNodeInfo, body: String): Boolean {
        val needle = body.take(40)
        if (node.isEditable && node.text?.toString()?.contains(needle) == true) return true
        for (i in 0 until node.childCount) node.getChild(i)?.let { if (containsText(it, body)) return true }
        return false
    }

    private fun findSend(root: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        for (id in SEND_IDS) root.findAccessibilityNodeInfosByViewId(id)?.firstOrNull()?.let { return clickable(it) }
        return findByDescription(root)
    }

    private fun findByDescription(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        val desc = node.contentDescription?.toString().orEmpty()
        if (SEND_DESCRIPTION.matches(desc) && node.isEnabled) clickable(node)?.let { return it }
        for (i in 0 until node.childCount) node.getChild(i)?.let { c -> findByDescription(c)?.let { return it } }
        return null
    }

    private fun clickable(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        var n: AccessibilityNodeInfo? = node
        while (n != null && !n.isClickable) n = n.parent
        return n
    }

    private data class Request(val pkg: String, val body: String, val onResult: (String?) -> Unit)

    companion object {
        private val SEND_IDS = listOf(
            "com.google.android.apps.messaging:id/send_message_button_icon",
            "com.google.android.apps.messaging:id/send_message_button_container",
            "com.samsung.android.messaging:id/send_button1",
        )
        private val SEND_DESCRIPTION = Regex("^(send|send message|send sms|send mms|send rcs.*|send end-to-end.*|senden|envoyer|enviar|invia)$", RegexOption.IGNORE_CASE)
        private const val TIMEOUT_MS = 12_000L

        @Volatile private var instance: BridgeAccessibilityService? = null
        @Volatile private var pending: Request? = null
        private val main = Handler(Looper.getMainLooper())
        private val timeout = Runnable { finish("The messaging app didn't show a Send button in time") }

        val isEnabled: Boolean get() = instance != null

        private fun finish(error: String?) {
            val req = pending ?: return
            pending = null
            main.removeCallbacks(timeout)
            req.onResult(error)
        }

        /** Opens the compose screen of [pkg] (default: Google Messages) and sends [body] to [addresses]. */
        fun compose(context: Context, addresses: List<String>, body: String, pkg: String?, onResult: (String?) -> Unit) {
            val target = pkg ?: MessageBridge.messagingApps.first()
            when {
                instance == null -> return onResult("Turn on Slonnect's “Send messages” accessibility service")
                context.getSystemService<KeyguardManager>()?.isKeyguardLocked == true -> return onResult("The phone is locked")
                pending != null -> return onResult("Another message is still being sent")
            }
            main.post {
                pending = Request(target, body, onResult)
                main.postDelayed(timeout, TIMEOUT_MS)
                val intent = Intent(Intent.ACTION_SENDTO, Uri.parse("smsto:" + addresses.joinToString(";") { Uri.encode(it) }))
                    .putExtra("sms_body", body)
                    .setPackage(target)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                try {
                    // Running accessibility services may start activities from the background.
                    instance!!.startActivity(intent)
                } catch (e: Exception) {
                    finish("Couldn't open $target: ${e.message}")
                }
            }
        }
    }
}
