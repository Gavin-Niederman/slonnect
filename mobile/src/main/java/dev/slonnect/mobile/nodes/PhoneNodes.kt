/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.nodes

import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationManager
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.AudioManager
import android.net.Uri
import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import dev.slonnect.mobile.privileged.Privileged
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import org.kde.kdeconnect.helpers.NotificationHelper
import org.kde.kdeconnect_tp.R
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

/** A pin of a phone node, as described to the desktop (protocol/README.md, PinDescriptor). */
data class Pin(
    val id: String,
    val label: String,
    val kind: String = "text",
    val default: JsonElement? = null,
    val options: List<String> = emptyList(),
    val placeholder: String? = null,
) {
    fun toJson() = buildJsonObject {
        put("id", id)
        put("label", label)
        put("kind", kind)
        default?.let { put("default", it) }
        if (options.isNotEmpty()) put("options", buildJsonArray { options.forEach { add(JsonPrimitive(it)) } })
        placeholder?.let { put("placeholder", it) }
    }
}

class NodeInputs(private val json: JsonObject) {
    fun str(id: String, default: String = ""): String = (json[id] as? JsonPrimitive)?.contentOrNull ?: default
    fun num(id: String, default: Double = 0.0): Double = (json[id] as? JsonPrimitive)?.let { it.doubleOrNull ?: it.contentOrNull?.toDoubleOrNull() } ?: default
    fun flag(id: String): Boolean = (json[id] as? JsonPrimitive)?.let { it.booleanOrNull ?: (it.contentOrNull?.lowercase() in setOf("true", "1", "yes", "on")) } ?: false
}

data class NodeOutcome(
    val flows: List<String> = listOf("out"),
    val outputs: JsonObject = JsonObject(emptyMap()),
    val log: List<String> = emptyList(),
)

class NodeFailure(message: String) : Exception(message)

/** One thing the phone can do for an action graph running on the desktop. */
abstract class PhoneNode(
    val id: String,
    val title: String,
    val icon: String,
    val description: String,
    val inputs: List<Pin> = emptyList(),
    val outputs: List<Pin> = emptyList(),
    val flows: List<String> = listOf("out"),
) {
    /** Why the node can't run right now, or null. Shown on the desktop next to the node. */
    open fun unavailableReason(context: Context): String? = null

    abstract suspend fun run(context: Context, input: NodeInputs): NodeOutcome

    fun describe(context: Context) = buildJsonObject {
        put("id", id)
        put("title", title)
        put("category", "Phone")
        put("icon", icon)
        put("description", description)
        put("inputs", buildJsonArray { inputs.forEach { add(it.toJson()) } })
        put("outputs", buildJsonArray { outputs.forEach { add(it.toJson()) } })
        put("flows", buildJsonArray { flows.forEach { add(JsonPrimitive(it)) } })
        val reason = unavailableReason(context)
        put("available", reason == null)
        reason?.let { put("unavailableReason", it) }
    }
}

private fun fail(message: String): Nothing = throw NodeFailure(message)
private fun outputs(vararg pairs: Pair<String, Any?>) = buildJsonObject {
    for ((k, v) in pairs) when (v) {
        is Boolean -> put(k, v)
        is Number -> put(k, v)
        null -> {}
        else -> put(k, v.toString())
    }
}
private fun choice(id: String, label: String, options: List<String>, default: String = options.first()) =
    Pin(id, label, "choice", JsonPrimitive(default), options)

// --- Bluetooth -------------------------------------------------------------------------

private object Bt {
    fun hasPermission(context: Context) = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    fun adapter(context: Context): BluetoothAdapter =
        context.getSystemService<BluetoothManager>()?.adapter ?: fail("This phone has no Bluetooth")

    @SuppressLint("MissingPermission")
    fun find(context: Context, query: String): BluetoothDevice {
        if (!hasPermission(context)) fail("Allow Slonnect to use nearby devices (Bluetooth) in its setup screen")
        val q = query.trim()
        if (q.isEmpty()) fail("Name the Bluetooth device")
        val bonded = adapter(context).bondedDevices.orEmpty()
        return bonded.firstOrNull { it.address.equals(q, ignoreCase = true) }
            ?: bonded.firstOrNull { it.name.equals(q, ignoreCase = true) || it.alias.equals(q, ignoreCase = true) }
            ?: bonded.firstOrNull { (it.alias ?: it.name ?: "").contains(q, ignoreCase = true) }
            ?: fail("“$q” isn't paired with this phone")
    }

    /** Public API: is the device connected on any audio profile? */
    @SuppressLint("MissingPermission")
    suspend fun isConnected(context: Context, device: BluetoothDevice): Boolean {
        val adapter = adapter(context)
        if (!adapter.isEnabled) return false
        for (profile in listOf(BluetoothProfile.A2DP, BluetoothProfile.HEADSET)) {
            val proxy = proxy(context, adapter, profile) ?: continue
            try {
                if (proxy.getConnectionState(device) == BluetoothProfile.STATE_CONNECTED) return true
            } finally {
                adapter.closeProfileProxy(profile, proxy)
            }
        }
        return false
    }

    private suspend fun proxy(context: Context, adapter: BluetoothAdapter, profile: Int): BluetoothProfile? =
        withTimeoutOrNull(3000) {
            suspendCoroutine { cont ->
                val ok = adapter.getProfileProxy(context, object : BluetoothProfile.ServiceListener {
                    override fun onServiceConnected(p: Int, proxy: BluetoothProfile) = cont.resume(proxy)
                    override fun onServiceDisconnected(p: Int) {}
                }, profile)
                if (!ok) cont.resume(null)
            }
        }

    /** Without Shizuku, only Android ≤ 11 (and some ROMs) let apps call the hidden connect methods. */
    @SuppressLint("MissingPermission")
    suspend fun connectUnprivileged(context: Context, device: BluetoothDevice, connect: Boolean): Boolean {
        val adapter = adapter(context)
        var any = false
        for (profile in listOf(BluetoothProfile.A2DP, BluetoothProfile.HEADSET)) {
            val proxy = proxy(context, adapter, profile) ?: continue
            try {
                any = (proxy.javaClass.getMethod(if (connect) "connect" else "disconnect", BluetoothDevice::class.java)
                    .invoke(proxy, device) as Boolean) || any
            } catch (_: Throwable) {
            } finally {
                adapter.closeProfileProxy(profile, proxy)
            }
        }
        return any
    }

    fun needsPrivilege() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
}

private val deviceInput = Pin("device", "Device", placeholder = "name or address")

private class BluetoothNode : PhoneNode(
    "bluetooth", "Phone Bluetooth device", "bluetooth_connected",
    "Connects or disconnects a Bluetooth device paired with the phone (earbuds, car, speaker). Needs Shizuku on Android 12 and later.",
    inputs = listOf(deviceInput, choice("mode", "Do", listOf("connect", "disconnect", "toggle"))),
    outputs = listOf(Pin("connected", "Connected", "bool")),
) {
    override fun unavailableReason(context: Context) = when {
        !Bt.hasPermission(context) -> "Allow Slonnect to use nearby devices on the phone"
        Bt.needsPrivilege() -> Privileged.problem()
        else -> null
    }

    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val device = Bt.find(context, input.str("device"))
        val name = device.alias ?: device.name ?: device.address
        val connectedNow = Bt.isConnected(context, device)
        val connect = when (input.str("mode", "connect")) {
            "disconnect" -> false
            "toggle" -> !connectedNow
            else -> true
        }
        if (connect == connectedNow)
            return NodeOutcome(outputs = outputs("connected" to connectedNow), log = listOf("$name was already ${if (connect) "connected" else "disconnected"}"))

        val how: String
        if (Privileged.state.value == Privileged.State.READY) {
            Privileged.bluetooth(context, device.address, connect)
            how = "through Shizuku"
        } else if (Bt.connectUnprivileged(context, device, connect)) {
            how = "directly"
        } else {
            fail(Privileged.problem() ?: "Android didn't let Slonnect change the connection")
        }
        // Profiles come up a moment after the request; wait for the state to settle.
        val deadline = SystemClock.elapsedRealtime() + 8000
        var state = Bt.isConnected(context, device)
        while (state != connect && SystemClock.elapsedRealtime() < deadline) {
            kotlinx.coroutines.delay(250)
            state = Bt.isConnected(context, device)
        }
        if (state != connect) fail("$name didn't ${if (connect) "connect" else "disconnect"} (asked $how)")
        return NodeOutcome(outputs = outputs("connected" to state), log = listOf("${if (connect) "Connected" else "Disconnected"} $name $how"))
    }
}

private class BluetoothConnectedNode : PhoneNode(
    "bluetooth.connected", "Phone Bluetooth connected?", "bluetooth",
    "Branches on whether a paired Bluetooth device is connected to the phone right now.",
    inputs = listOf(deviceInput),
    flows = listOf("yes", "no"),
) {
    override fun unavailableReason(context: Context) =
        if (!Bt.hasPermission(context)) "Allow Slonnect to use nearby devices on the phone" else null

    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val device = Bt.find(context, input.str("device"))
        val connected = Bt.isConnected(context, device)
        return NodeOutcome(flows = listOf(if (connected) "yes" else "no"))
    }
}

private class BluetoothPowerNode : PhoneNode(
    "bluetooth.power", "Phone Bluetooth power", "bluetooth",
    "Turns the phone's Bluetooth on or off.",
    inputs = listOf(choice("state", "Turn", listOf("on", "off"))),
) {
    override fun unavailableReason(context: Context) =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) Privileged.problem() else null

    @Suppress("DEPRECATION")
    @SuppressLint("MissingPermission")
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val on = input.str("state", "on") == "on"
        if (Privileged.state.value == Privileged.State.READY) {
            val r = Privileged.exec(context, "cmd bluetooth_manager ${if (on) "enable" else "disable"}")
            if (r.exitCode != 0) fail(r.output.ifBlank { "cmd bluetooth_manager failed" })
        } else if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            val adapter = Bt.adapter(context)
            if (!(if (on) adapter.enable() else adapter.disable())) fail("Android refused")
        } else {
            fail(Privileged.problem() ?: "Needs Shizuku")
        }
        return NodeOutcome()
    }
}

// --- Sound -----------------------------------------------------------------------------

private class VolumeNode : PhoneNode(
    "volume", "Phone volume", "volume_up", "Sets a volume on the phone, in percent, or mutes it.",
    inputs = listOf(
        choice("stream", "Stream", listOf("media", "ring", "notification", "alarm", "call")),
        choice("mode", "Do", listOf("set", "mute", "unmute")),
        Pin("level", "Level %", "number", JsonPrimitive(50)),
    ),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val audio = context.getSystemService<AudioManager>()!!
        val stream = when (input.str("stream", "media")) {
            "ring" -> AudioManager.STREAM_RING
            "notification" -> AudioManager.STREAM_NOTIFICATION
            "alarm" -> AudioManager.STREAM_ALARM
            "call" -> AudioManager.STREAM_VOICE_CALL
            else -> AudioManager.STREAM_MUSIC
        }
        try {
            when (input.str("mode", "set")) {
                "mute" -> audio.adjustStreamVolume(stream, AudioManager.ADJUST_MUTE, AudioManager.FLAG_SHOW_UI)
                "unmute" -> audio.adjustStreamVolume(stream, AudioManager.ADJUST_UNMUTE, AudioManager.FLAG_SHOW_UI)
                else -> {
                    val max = audio.getStreamMaxVolume(stream)
                    val index = Math.round(input.num("level", 50.0).coerceIn(0.0, 100.0) / 100.0 * max).toInt()
                    audio.setStreamVolume(stream, index, AudioManager.FLAG_SHOW_UI)
                }
            }
        } catch (e: SecurityException) {
            fail("Android needs Do Not Disturb access for that (Slonnect → Setup)")
        }
        return NodeOutcome()
    }
}

private class RingerNode : PhoneNode(
    "ringer", "Ringer mode", "vibration", "Normal, vibrate or silent. Silent needs Do Not Disturb access.",
    inputs = listOf(choice("mode", "Mode", listOf("normal", "vibrate", "silent"), "vibrate")),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val audio = context.getSystemService<AudioManager>()!!
        try {
            audio.ringerMode = when (input.str("mode")) {
                "normal" -> AudioManager.RINGER_MODE_NORMAL
                "silent" -> AudioManager.RINGER_MODE_SILENT
                else -> AudioManager.RINGER_MODE_VIBRATE
            }
        } catch (e: SecurityException) {
            fail("Android needs Do Not Disturb access for that (Slonnect → Setup)")
        }
        return NodeOutcome()
    }
}

private class DndNode : PhoneNode(
    "dnd", "Do Not Disturb", "do_not_disturb_on", "Turns Do Not Disturb on the phone on or off.",
    inputs = listOf(choice("state", "Turn", listOf("on", "off", "toggle"))),
) {
    override fun unavailableReason(context: Context): String? {
        val nm = context.getSystemService<NotificationManager>()!!
        return if (nm.isNotificationPolicyAccessGranted || Privileged.state.value == Privileged.State.READY) null
        else "Give Slonnect Do Not Disturb access on the phone"
    }

    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val nm = context.getSystemService<NotificationManager>()!!
        val isOn = nm.currentInterruptionFilter != NotificationManager.INTERRUPTION_FILTER_ALL
        val on = when (input.str("state", "on")) { "off" -> false; "toggle" -> !isOn; else -> true }
        if (nm.isNotificationPolicyAccessGranted) {
            nm.setInterruptionFilter(if (on) NotificationManager.INTERRUPTION_FILTER_PRIORITY else NotificationManager.INTERRUPTION_FILTER_ALL)
        } else if (Privileged.state.value == Privileged.State.READY) {
            Privileged.exec(context, "cmd notification set_dnd ${if (on) "priority" else "off"}")
        } else {
            fail(unavailableReason(context)!!)
        }
        return NodeOutcome(log = listOf("Do Not Disturb ${if (on) "on" else "off"}"))
    }
}

private class MediaKeyNode : PhoneNode(
    "media", "Phone media key", "play_circle", "Presses a media key on the phone (whatever app is playing).",
    inputs = listOf(choice("key", "Key", listOf("play/pause", "play", "pause", "next", "previous", "stop"))),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val code = when (input.str("key")) {
            "play" -> KeyEvent.KEYCODE_MEDIA_PLAY
            "pause" -> KeyEvent.KEYCODE_MEDIA_PAUSE
            "next" -> KeyEvent.KEYCODE_MEDIA_NEXT
            "previous" -> KeyEvent.KEYCODE_MEDIA_PREVIOUS
            "stop" -> KeyEvent.KEYCODE_MEDIA_STOP
            else -> KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
        }
        val audio = context.getSystemService<AudioManager>()!!
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, code))
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code))
        return NodeOutcome()
    }
}

// --- Device ----------------------------------------------------------------------------

private object Torch {
    @Volatile var on = false
    private var registered = false

    fun camera(context: Context): Pair<CameraManager, String> {
        val cm = context.getSystemService<CameraManager>()!!
        val id = cm.cameraIdList.firstOrNull {
            cm.getCameraCharacteristics(it).get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        } ?: fail("This phone has no flashlight")
        if (!registered) {
            registered = true
            cm.registerTorchCallback(object : CameraManager.TorchCallback() {
                override fun onTorchModeChanged(cameraId: String, enabled: Boolean) {
                    if (cameraId == id) on = enabled
                }
            }, android.os.Handler(android.os.Looper.getMainLooper()))
        }
        return cm to id
    }
}

private class FlashlightNode : PhoneNode(
    "flashlight", "Flashlight", "flashlight_on", "Turns the phone's flashlight on or off.",
    inputs = listOf(choice("state", "Turn", listOf("on", "off", "toggle"), "toggle")),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val (cm, id) = Torch.camera(context)
        kotlinx.coroutines.delay(50) // let the callback report the current state
        val on = when (input.str("state", "toggle")) { "on" -> true; "off" -> false; else -> !Torch.on }
        cm.setTorchMode(id, on)
        return NodeOutcome()
    }
}

private class VibrateNode : PhoneNode(
    "vibrate", "Vibrate", "vibration", "Vibrates the phone.",
    inputs = listOf(Pin("ms", "Milliseconds", "number", JsonPrimitive(300))),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            context.getSystemService<VibratorManager>()!!.defaultVibrator
        else @Suppress("DEPRECATION") context.getSystemService<Vibrator>()!!
        vibrator.vibrate(VibrationEffect.createOneShot(input.num("ms", 300.0).toLong().coerceIn(1, 10_000), VibrationEffect.DEFAULT_AMPLITUDE))
        return NodeOutcome()
    }
}

private class NotifyNode : PhoneNode(
    "notify", "Phone notification", "notifications", "Shows a notification on the phone.",
    inputs = listOf(Pin("title", "Title"), Pin("text", "Text")),
) {
    @SuppressLint("MissingPermission")
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val nm = context.getSystemService<NotificationManager>()!!
        if (!nm.areNotificationsEnabled()) fail("Notifications for Slonnect are off on the phone")
        val n = NotificationCompat.Builder(context, NotificationHelper.Channels.DEFAULT)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(input.str("title").ifBlank { "Slonnect" })
            .setContentText(input.str("text"))
            .setStyle(NotificationCompat.BigTextStyle().bigText(input.str("text")))
            .setAutoCancel(true)
            .build()
        nm.notify(SystemClock.elapsedRealtime().toInt(), n)
        return NodeOutcome()
    }
}

private class ClipboardNode : PhoneNode(
    "clipboard", "Phone clipboard", "content_paste", "Puts text on the phone's clipboard.",
    inputs = listOf(Pin("text", "Text")),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val cm = context.getSystemService<ClipboardManager>()!!
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) {
            cm.setPrimaryClip(ClipData.newPlainText("Slonnect", input.str("text")))
        }
        return NodeOutcome()
    }
}

private class LaunchNode : PhoneNode(
    "launch", "Open on phone", "open_in_new", "Opens an app (package name) or a link on the phone.",
    inputs = listOf(Pin("target", "App or link", placeholder = "com.spotify.music or https://…")),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val target = input.str("target").trim()
        if (target.isEmpty()) fail("Say what to open")
        val intent = if (target.contains(':')) Intent(Intent.ACTION_VIEW, Uri.parse(target))
        else context.packageManager.getLaunchIntentForPackage(target) ?: fail("$target isn't installed")
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            context.startActivity(intent)
        } catch (e: Exception) {
            fail("Android blocked opening it from the background: allow Slonnect to display over other apps")
        }
        return NodeOutcome()
    }
}

private class IntentNode : PhoneNode(
    "intent", "Send intent", "send",
    "Sends a broadcast intent, e.g. to trigger a Tasker or MacroDroid profile. Extras are a JSON object of strings, numbers and booleans.",
    inputs = listOf(
        Pin("action", "Action", placeholder = "net.dinglisch.android.tasker.ACTION_TASK"),
        Pin("package", "Package", placeholder = "optional"),
        Pin("extras", "Extras (JSON)", placeholder = "{\"task_name\": \"Home\"}"),
    ),
) {
    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val action = input.str("action").trim().ifEmpty { fail("Name the intent action") }
        val intent = Intent(action)
        input.str("package").trim().takeIf { it.isNotEmpty() }?.let { intent.setPackage(it) }
        val extras = input.str("extras").trim()
        if (extras.isNotEmpty()) {
            val obj = runCatching { Json.parseToJsonElement(extras).jsonObject }.getOrElse { fail("Extras aren't a JSON object") }
            for ((k, v) in obj) {
                val p = v.jsonPrimitive
                when {
                    p.isString -> intent.putExtra(k, p.content)
                    p.booleanOrNull != null -> intent.putExtra(k, p.booleanOrNull!!)
                    p.content.toLongOrNull() != null -> intent.putExtra(k, p.content.toLong())
                    p.doubleOrNull != null -> intent.putExtra(k, p.doubleOrNull!!)
                }
            }
        }
        context.sendBroadcast(intent)
        return NodeOutcome()
    }
}

private class ShellNode : PhoneNode(
    "shell", "Phone shell (Shizuku)", "terminal",
    "Runs a shell command on the phone with adb-level rights through Shizuku.",
    inputs = listOf(Pin("command", "Command", placeholder = "svc wifi enable")),
    outputs = listOf(Pin("stdout", "Output"), Pin("exitCode", "Exit code", "number")),
) {
    override fun unavailableReason(context: Context) = Privileged.problem()

    override suspend fun run(context: Context, input: NodeInputs): NodeOutcome {
        val command = input.str("command").ifBlank { fail("No command") }
        val r = Privileged.exec(context, command)
        if (r.exitCode != 0) fail("exit ${r.exitCode}: ${r.output.trim().take(300)}")
        return NodeOutcome(outputs = outputs("stdout" to r.output.trimEnd(), "exitCode" to r.exitCode))
    }
}

object PhoneNodes {
    val all: List<PhoneNode> = listOf(
        BluetoothNode(), BluetoothConnectedNode(), BluetoothPowerNode(),
        VolumeNode(), RingerNode(), DndNode(), FlashlightNode(), MediaKeyNode(),
        LaunchNode(), NotifyNode(), VibrateNode(), ClipboardNode(), IntentNode(), ShellNode(),
    )

    fun find(id: String) = all.firstOrNull { it.id == id }

    fun catalogue(context: Context) = buildJsonArray { all.forEach { add(it.describe(context)) } }
}
