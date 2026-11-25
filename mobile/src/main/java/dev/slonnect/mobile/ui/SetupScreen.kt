/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.ui

import android.Manifest
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import dev.slonnect.mobile.bridge.BridgeAccessibilityService
import dev.slonnect.mobile.bridge.DemoRcs
import dev.slonnect.mobile.bridge.MessageBridge
import dev.slonnect.mobile.privileged.Privileged
import kotlinx.coroutines.delay
import org.kde.kdeconnect.plugins.notifications.NotificationReceiver
import org.kde.kdeconnect_tp.BuildConfig

private data class Checks(
    val notificationAccess: Boolean,
    val postNotifications: Boolean,
    val bluetooth: Boolean,
    val shizuku: Privileged.State,
    val dnd: Boolean,
    val sms: Boolean,
    val accessibility: Boolean,
) {
    val issues: Int get() = listOf(notificationAccess, postNotifications, bluetooth, shizuku == Privileged.State.READY, dnd).count { !it }
}

private fun granted(context: Context, p: String) = ContextCompat.checkSelfPermission(context, p) == PackageManager.PERMISSION_GRANTED

private fun check(context: Context): Checks {
    Privileged.refresh(context)
    return Checks(
        notificationAccess = NotificationReceiver.hasReadNotificationsPermission(context),
        postNotifications = Build.VERSION.SDK_INT < 33 || granted(context, Manifest.permission.POST_NOTIFICATIONS),
        bluetooth = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || granted(context, Manifest.permission.BLUETOOTH_CONNECT),
        shizuku = Privileged.state.value,
        dnd = context.getSystemService<NotificationManager>()?.isNotificationPolicyAccessGranted == true,
        sms = granted(context, Manifest.permission.READ_SMS) && granted(context, Manifest.permission.SEND_SMS) &&
            granted(context, Manifest.permission.READ_CONTACTS),
        accessibility = BridgeAccessibilityService.isEnabled,
    )
}

@Composable
private fun rememberChecks(): Checks {
    val context = LocalContext.current
    return produceState(check(context)) {
        while (true) {
            delay(1500) // permissions change in other apps' screens; just look again
            value = check(context)
        }
    }.value
}

@Composable
fun rememberSetupIssues(): Int = rememberChecks().issues

@Composable
fun SetupScreen(activity: SlonnectActivity) {
    val c = rememberChecks()
    val bridge by MessageBridge.settings.collectAsState()
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {}
    fun open(action: String, data: Uri? = null) = runCatching { activity.startActivity(Intent(action).apply { data?.let { setData(it) } }) }

    LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item {
            Section("This phone") {
                Check("Notification access", "Sync notifications and capture RCS conversations for the Messages bridge.", c.notificationAccess) {
                    open(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
                }
                if (Build.VERSION.SDK_INT >= 33) Check("Show notifications", "Pings, received files, and phone notifications from actions.", c.postNotifications) {
                    ask.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Check("Nearby devices", "Lets actions find and check your Bluetooth earbuds and speakers.", c.bluetooth) {
                    ask.launch(arrayOf(Manifest.permission.BLUETOOTH_CONNECT))
                }
                Check("Do Not Disturb access", "For the Do Not Disturb and silent ringer nodes.", c.dnd) {
                    open(Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS)
                }
            }
        }
        item {
            Section("Shizuku") {
                Text(
                    "Android only lets system apps connect Bluetooth audio, toggle Bluetooth or run shell commands. Shizuku gives Slonnect " +
                        "adb-level rights for exactly those nodes (needed to swap earbuds on Android 12 and later).",
                    style = MaterialTheme.typography.bodyMedium,
                )
                val (label, ok) = when (c.shizuku) {
                    Privileged.State.READY -> "Ready" to true
                    Privileged.State.NEEDS_PERMISSION -> "Allow Slonnect" to false
                    Privileged.State.NOT_RUNNING -> "Start Shizuku" to false
                    Privileged.State.NOT_INSTALLED -> "Get Shizuku" to false
                }
                Check("Shizuku", label, ok) {
                    when (c.shizuku) {
                        Privileged.State.NEEDS_PERMISSION -> Privileged.requestPermission()
                        Privileged.State.NOT_RUNNING -> activity.packageManager.getLaunchIntentForPackage("moe.shizuku.privileged.api")
                            ?.let { activity.startActivity(it) }
                        Privileged.State.NOT_INSTALLED -> open(Intent.ACTION_VIEW, Uri.parse("https://shizuku.rikka.app/download/"))
                        Privileged.State.READY -> {}
                    }
                }
            }
        }
        item {
            Section("Messages bridge (RCS)") {
                Text(
                    "Shows RCS chats from Google Messages on the computer and sends replies through it, without Google Messages for Web. " +
                        "It sees what arrives as a notification; messages you read or send on the phone first aren't captured.",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Bridge RCS conversations", Modifier.weight(1f), style = MaterialTheme.typography.titleSmall)
                    Switch(bridge.enabled, { on -> MessageBridge.update { it.copy(enabled = on) } })
                }
                if (bridge.enabled) {
                    Check("SMS and contacts", "To merge RCS into SMS threads and send from the computer.", c.sms) {
                        ask.launch(arrayOf(Manifest.permission.READ_SMS, Manifest.permission.SEND_SMS, Manifest.permission.READ_CONTACTS))
                    }
                    Check("“Send messages” accessibility service", "Presses Send in Google Messages when there's no notification to reply to.", c.accessibility) {
                        open(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                    }
                    Text("Send through Google Messages", style = MaterialTheme.typography.titleSmall)
                    val modes = MessageBridge.SendMode.entries
                    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                        modes.forEachIndexed { i, m ->
                            SegmentedButton(
                                selected = bridge.sendMode == m,
                                onClick = { MessageBridge.update { it.copy(sendMode = m) } },
                                shape = SegmentedButtonDefaults.itemShape(i, modes.size),
                            ) {
                                Text(when (m) {
                                    MessageBridge.SendMode.RCS_THREADS -> "RCS chats"
                                    MessageBridge.SendMode.ALWAYS -> "Always"
                                    MessageBridge.SendMode.NEVER -> "Never"
                                })
                            }
                        }
                    }
                }
            }
        }
        if (BuildConfig.DEBUG) item {
            Section("Demo (debug builds)") {
                Text("Synthetic RCS history and fake Google Messages notifications, for testing without anyone's real messages.",
                    style = MaterialTheme.typography.bodyMedium)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Demo RCS", Modifier.weight(1f), style = MaterialTheme.typography.titleSmall)
                    Switch(bridge.demo, { on -> MessageBridge.update { it.copy(demo = on) } })
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilledTonalButton(onClick = { DemoRcs.seed(activity) }) { Text("Seed history") }
                    OutlinedButton(onClick = { DemoRcs.postIncoming(activity, "Grace Hopper", "Ping from the demo 👋") }) { Text("Fake incoming") }
                }
            }
        }
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Card(shape = MaterialTheme.shapes.large, colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleLarge)
            HorizontalDivider()
            content()
        }
    }
}

@Composable
private fun Check(title: String, detail: String, ok: Boolean, fix: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(
            if (ok) Icons.Filled.CheckCircle else Icons.Filled.Info, null, Modifier.size(22.dp),
            tint = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.tertiary,
        )
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (!ok) {
            Spacer(Modifier.width(8.dp))
            FilledTonalButton(onClick = fix) { Text("Fix") }
        }
    }
}
