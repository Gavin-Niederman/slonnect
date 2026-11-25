/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile

import android.util.Log
import dev.slonnect.mobile.nodes.NodeFailure
import dev.slonnect.mobile.nodes.NodeInputs
import dev.slonnect.mobile.nodes.PhoneNodes
import dev.slonnect.mobile.privileged.Privileged
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.kde.kdeconnect.NetworkPacket
import org.kde.kdeconnect.plugins.Plugin
import org.kde.kdeconnect.plugins.PluginFactory.LoadablePlugin
import java.util.UUID

/**
 * The phone end of the Slonnect protocol (protocol/README.md): runs phone nodes for action
 * graphs on the desktop, publishes their catalogue, and triggers the desktop's actions.
 * The desktop side goes through kdeconnectd's `slonnect` plugin.
 */
@LoadablePlugin
class SlonnectPlugin : Plugin() {
    override val displayName: String get() = "Slonnect actions"
    override val description: String get() = "Run the computer's actions from the phone, and let them control this phone (Bluetooth, volume, Do Not Disturb, …)"

    override val supportedPacketTypes: Array<String> = ALL_TYPES
    override val outgoingPacketTypes: Array<String> = ALL_TYPES

    private var scope: CoroutineScope? = null

    override fun onCreate() {
        val s = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        scope = s
        // Node availability depends on Shizuku: tell the desktop when that changes.
        Privileged.state.drop(1).onEach { sendHello(replyRequested = false) }.launchIn(s)
        sendHello(replyRequested = true)
        device.sendPacket(NetworkPacket(ACTIONS_REQUEST))
    }

    override fun onDestroy() {
        scope?.cancel()
        scope = null
        if (isDeviceInitialized) SlonnectState.forgetDesktop(device.deviceId)
    }

    private fun sendHello(replyRequested: Boolean) {
        val np = NetworkPacket(HELLO)
        np["version"] = PROTOCOL_VERSION
        np["role"] = "mobile"
        np["app"] = "slonnect-mobile ${org.kde.kdeconnect_tp.BuildConfig.VERSION_NAME}"
        np["features"] = listOf("actions", "nodes", "rcs-bridge")
        np["nodes"] = PhoneNodes.catalogue(context)
        np["replyRequested"] = replyRequested
        device.sendPacket(np)
    }

    override fun onPacketReceived(np: NetworkPacket): Boolean {
        when (np.type) {
            HELLO -> {
                if (np.getString("role", "") == "desktop") SlonnectState.setDesktop(device.deviceId, np.getString("app", "slonnect"))
                if (np.getBoolean("replyRequested", false)) sendHello(replyRequested = false)
            }
            NODE_RUN -> runNode(np)
            ACTIONS -> SlonnectState.setActions(device.deviceId, parseActions(np.getJsonArray("actions")))
            ACTION_STATUS -> {
                val runId = np.getString("runId", "")
                val state = when (np.getString("state", "")) {
                    "running" -> ActionRun.State.RUNNING
                    "done" -> ActionRun.State.DONE
                    "failed" -> ActionRun.State.FAILED
                    else -> return true
                }
                SlonnectState.updateRun(runId) { it.copy(state = state, step = np.getStringOrNull("step") ?: it.step, error = np.getStringOrNull("error")) }
            }
            else -> return false
        }
        return true
    }

    private fun parseActions(array: JsonArray?): List<RemoteAction> = array.orEmpty().mapNotNull { e ->
        val o = e as? JsonObject ?: return@mapNotNull null
        fun s(k: String) = (o[k] as? JsonPrimitive)?.contentOrNull
        val id = s("id") ?: return@mapNotNull null
        RemoteAction(id, s("name") ?: id, s("icon") ?: "bolt", s("description") ?: "", s("color"))
    }

    private fun runNode(np: NetworkPacket) {
        val requestId = np.getString("requestId", "")
        val nodeId = np.getString("node", "")
        val inputs = np.getJsonObject("inputs") ?: JsonObject(emptyMap())
        val timeoutMs = np.getLong("timeoutMs", 30_000L).coerceIn(1_000L, 120_000L)
        val node = PhoneNodes.find(nodeId)
        val s = scope ?: return
        s.launch {
            val reply = NetworkPacket(NODE_RESULT)
            reply["requestId"] = requestId
            if (node == null) {
                reply["ok"] = false
                reply["error"] = "This phone's Slonnect doesn't know “$nodeId”; update the app"
                return@launch device.sendPacket(reply)
            }
            try {
                val outcome = withTimeoutOrNull(timeoutMs - 500) { node.run(context, NodeInputs(inputs)) }
                    ?: throw NodeFailure("Timed out on the phone")
                reply["ok"] = true
                reply["flows"] = outcome.flows
                reply["outputs"] = outcome.outputs
                if (outcome.log.isNotEmpty()) reply["log"] = outcome.log
            } catch (e: NodeFailure) {
                reply["ok"] = false
                reply["error"] = e.message ?: "Failed"
            } catch (e: Privileged.Unavailable) {
                reply["ok"] = false
                reply["error"] = e.message ?: "Needs Shizuku"
            } catch (e: Exception) {
                Log.w(TAG, "node $nodeId failed", e)
                reply["ok"] = false
                reply["error"] = "${e.javaClass.simpleName}: ${e.message}"
            }
            device.sendPacket(reply)
        }
    }

    /** Ask the desktop to run one of its actions. Returns the run id. */
    fun runAction(actionId: String): String {
        val runId = "m-" + UUID.randomUUID().toString().take(8)
        SlonnectState.putRun(ActionRun(runId, device.deviceId, actionId, ActionRun.State.SENT))
        val np = NetworkPacket(ACTION_RUN)
        np["id"] = actionId
        np["runId"] = runId
        device.sendPacket(np)
        return runId
    }

    fun refreshActions() = device.sendPacket(NetworkPacket(ACTIONS_REQUEST))

    companion object {
        private const val TAG = "Slonnect"
        private const val PROTOCOL_VERSION = 1
        const val HELLO = "kdeconnect.slonnect.hello"
        const val NODE_RUN = "kdeconnect.slonnect.node.run"
        const val NODE_RESULT = "kdeconnect.slonnect.node.result"
        const val ACTIONS = "kdeconnect.slonnect.actions"
        const val ACTIONS_REQUEST = "kdeconnect.slonnect.actions.request"
        const val ACTION_RUN = "kdeconnect.slonnect.action.run"
        const val ACTION_STATUS = "kdeconnect.slonnect.action.status"
        private val ALL_TYPES = arrayOf(HELLO, NODE_RUN, NODE_RESULT, ACTIONS, ACTIONS_REQUEST, ACTION_RUN, ACTION_STATUS)
    }
}
