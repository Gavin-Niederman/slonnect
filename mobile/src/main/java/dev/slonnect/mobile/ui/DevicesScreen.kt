/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.slonnect.mobile.ActionRun
import dev.slonnect.mobile.RemoteAction
import dev.slonnect.mobile.SlonnectPlugin
import dev.slonnect.mobile.SlonnectState
import kotlinx.coroutines.delay
import org.kde.kdeconnect.Device
import org.kde.kdeconnect.KdeConnect
import org.kde.kdeconnect.plugins.Plugin
import org.kde.kdeconnect.plugins.battery.BatteryPlugin

/** What the home screen shows about one paired device. */
data class DeviceUi(
    val id: String,
    val name: String,
    val iconRes: Int,
    val reachable: Boolean,
    val battery: Int?,
    val charging: Boolean,
    val buttons: List<Plugin.PluginUiButton>,
    val slonnect: SlonnectPlugin?,
)

private fun snapshot(): List<DeviceUi> = KdeConnect.getInstance().devices.values
    .filter { it.isPaired }
    .sortedWith(compareByDescending<Device> { it.isReachable }.thenBy { it.name })
    .map { d ->
        val battery = d.getPlugin(Plugin.getPluginKey(BatteryPlugin::class.java)) as? BatteryPlugin
        DeviceUi(
            id = d.deviceId,
            name = d.name,
            iconRes = d.iconDrawable,
            reachable = d.isReachable,
            battery = battery?.remoteBatteryInfo?.currentCharge,
            charging = battery?.remoteBatteryInfo?.isCharging == true,
            buttons = if (d.isReachable) d.loadedPlugins.values.flatMap { runCatching { it.getUiButtons() }.getOrDefault(emptyList()) } else emptyList(),
            slonnect = d.getPlugin(Plugin.getPluginKey(SlonnectPlugin::class.java)) as? SlonnectPlugin,
        )
    }

/** Paired devices, refreshed when the device list changes and every couple of seconds. */
@Composable
fun rememberDevices(): List<DeviceUi> {
    val state = produceState(initialValue = snapshot()) {
        val kc = KdeConnect.getInstance()
        val key = "slonnect-home-${hashCode()}"
        kc.addDeviceListChangedCallback(key) { value = snapshot() }
        try {
            while (true) {
                delay(2000)
                value = snapshot()
            }
        } finally {
            kc.removeDeviceListChangedCallback(key)
        }
    }
    return state.value
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun DevicesScreen(activity: SlonnectActivity, devices: List<DeviceUi>, setupIssues: Int, onOpenSetup: () -> Unit) {
    val actions by SlonnectState.actions.collectAsState()
    LazyColumn(
        contentPadding = PaddingValues(16.dp, 8.dp, 16.dp, 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (setupIssues > 0) item {
            Card(
                onClick = onOpenSetup,
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer),
            ) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Warning, null, tint = MaterialTheme.colorScheme.onTertiaryContainer)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text("Finish setting up", style = MaterialTheme.typography.titleSmall)
                        Text(if (setupIssues == 1) "1 thing still needs permission on this phone" else "$setupIssues things still need permission on this phone",
                            style = MaterialTheme.typography.bodySmall)
                    }
                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null)
                }
            }
        }
        if (devices.isEmpty()) item { EmptyHero(activity::openPairing) }
        items(devices, key = { it.id }) { d ->
            DeviceCard(d, actions[d.id].orEmpty(), onOpen = { activity.openDevice(d.id) }, onButton = { it.onClick(activity) })
        }
        if (devices.isNotEmpty()) item {
            TextButton(onClick = activity::openPairing, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Filled.Add, null)
                Spacer(Modifier.width(8.dp))
                Text("Pair another device")
            }
        }
    }
}

@Composable
private fun EmptyHero(onPair: () -> Unit) {
    Surface(shape = MaterialTheme.shapes.extraLarge, color = MaterialTheme.colorScheme.primaryContainer) {
        Column(Modifier.fillMaxWidth().padding(28.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("Connect a computer", style = MaterialTheme.typography.headlineSmall, color = MaterialTheme.colorScheme.onPrimaryContainer)
            Spacer(Modifier.height(8.dp))
            Text(
                "Run Slonnect or KDE Connect on your computer, on the same network, then pair it here.",
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onPrimaryContainer,
            )
            Spacer(Modifier.height(20.dp))
            Button(onClick = onPair) { Text("Find devices") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DeviceCard(d: DeviceUi, actions: List<RemoteAction>, onOpen: () -> Unit, onButton: (Plugin.PluginUiButton) -> Unit) {
    val scheme = MaterialTheme.colorScheme
    ElevatedCard(shape = MaterialTheme.shapes.extraLarge, colors = CardDefaults.elevatedCardColors(containerColor = scheme.surfaceContainer)) {
        // Header with a soft gradient when connected.
        val headerBrush = if (d.reachable) Brush.linearGradient(listOf(scheme.primaryContainer, scheme.tertiaryContainer))
        else Brush.linearGradient(listOf(scheme.surfaceContainerHigh, scheme.surfaceContainerHigh))
        Row(
            Modifier.fillMaxWidth().background(headerBrush).clickable(onClick = onOpen).padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(Modifier.size(52.dp).clip(CircleShape).background(scheme.surface.copy(alpha = 0.6f)), contentAlignment = Alignment.Center) {
                Icon(painterResource(d.iconRes), null, Modifier.size(28.dp), tint = scheme.onPrimaryContainer)
            }
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) {
                Text(d.name, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                StatusLine(d)
            }
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Open", tint = scheme.onSurfaceVariant)
        }
        d.battery?.let { level ->
            LinearProgressIndicator(progress = { level / 100f }, modifier = Modifier.fillMaxWidth().height(3.dp))
        }
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            if (actions.isNotEmpty() && d.slonnect != null) {
                Text("Actions", style = MaterialTheme.typography.labelLarge, color = scheme.onSurfaceVariant)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    actions.forEach { a -> ActionTile(d, a, compact = true) }
                }
            }
            if (d.buttons.isNotEmpty()) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    d.buttons.forEach { b ->
                        AssistChip(
                            onClick = { onButton(b) },
                            label = { Text(b.name) },
                            leadingIcon = { Icon(painterResource(b.iconRes), null, Modifier.size(AssistChipDefaults.IconSize)) },
                        )
                    }
                }
            } else if (!d.reachable) {
                Text("Not reachable. Is it on the same network?", style = MaterialTheme.typography.bodyMedium, color = scheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun StatusLine(d: DeviceUi) {
    val dot by animateColorAsState(if (d.reachable) Color(0xFF2E9E5B) else MaterialTheme.colorScheme.outline, label = "dot")
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(dot))
        Spacer(Modifier.width(6.dp))
        val parts = buildList {
            add(if (d.reachable) "Connected" else "Offline")
            d.battery?.let { add("$it%" + if (d.charging) " ⚡" else "") }
            if (d.slonnect != null && d.reachable) add("Slonnect")
        }
        Text(parts.joinToString(" · "), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** A tappable action. Shows progress and the result of the last run from this phone. */
@Composable
fun ActionTile(d: DeviceUi, action: RemoteAction, compact: Boolean, modifier: Modifier = Modifier) {
    val runs by SlonnectState.runs.collectAsState()
    val run = runs.values.filter { it.deviceId == d.id && it.actionId == action.id }.maxByOrNull { it.startedAt }
    val busy = run?.state == ActionRun.State.SENT || run?.state == ActionRun.State.RUNNING
    val pressed = remember { mutableStateOf(false) }
    val scale by animateFloatAsState(if (pressed.value) 0.94f else 1f, label = "press")
    LaunchedEffect(pressed.value) { if (pressed.value) { delay(120); pressed.value = false } }
    val scheme = MaterialTheme.colorScheme
    val container = when (run?.state) {
        ActionRun.State.FAILED -> scheme.errorContainer
        ActionRun.State.DONE -> scheme.secondaryContainer
        else -> scheme.primaryContainer
    }
    val enabled = d.reachable && d.slonnect != null && !busy
    Surface(
        onClick = { pressed.value = true; d.slonnect?.runAction(action.id) },
        enabled = enabled,
        shape = RoundedCornerShape(if (compact) 18.dp else 28.dp),
        color = container,
        modifier = modifier.scale(scale),
    ) {
        if (compact) {
            Row(Modifier.padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                RunIcon(run, busy)
                Spacer(Modifier.width(8.dp))
                Text(action.name, style = MaterialTheme.typography.labelLarge)
            }
        } else {
            Column(Modifier.padding(18.dp).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                RunIcon(run, busy, big = true)
                Text(action.name, style = MaterialTheme.typography.titleMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                val sub = when {
                    busy -> run?.step ?: "Running…"
                    run?.state == ActionRun.State.FAILED -> run.error ?: "Failed"
                    run?.state == ActionRun.State.DONE -> "Done"
                    else -> d.name
                }
                Text(sub, style = MaterialTheme.typography.bodySmall, maxLines = 2, overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun RunIcon(run: ActionRun?, busy: Boolean, big: Boolean = false) {
    val size = if (big) 28.dp else 18.dp
    when {
        busy -> CircularProgressIndicator(Modifier.size(size), strokeWidth = 2.dp)
        run?.state == ActionRun.State.DONE -> Icon(Icons.Filled.CheckCircle, null, Modifier.size(size))
        run?.state == ActionRun.State.FAILED -> Icon(Icons.Filled.Warning, null, Modifier.size(size))
        else -> Icon(Icons.Filled.PlayArrow, null, Modifier.size(size))
    }
}

@Composable
fun ActionsScreen(devices: List<DeviceUi>) {
    val actions by SlonnectState.actions.collectAsState()
    val withActions = devices.filter { it.slonnect != null }
    LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(20.dp)) {
        if (withActions.isEmpty()) item {
            Surface(shape = MaterialTheme.shapes.extraLarge, color = MaterialTheme.colorScheme.surfaceContainerHigh) {
                Column(Modifier.padding(24.dp)) {
                    Text("No computer with Slonnect is connected", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Text("Actions are built in Slonnect on the computer (Actions page). They show up here, and can control this phone too: " +
                        "Bluetooth, volume, Do Not Disturb, the flashlight and more.", style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
        for (d in withActions) {
            val list = actions[d.id].orEmpty()
            item(key = "h-${d.id}") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(d.name, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                    FilledTonalButton(onClick = { d.slonnect?.refreshActions() }, enabled = d.reachable) { Text("Refresh") }
                }
            }
            if (list.isEmpty()) item(key = "e-${d.id}") {
                Text("No actions yet. Make one on ${d.name} (Actions → New action, or Earbuds swap).",
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            // Two-column grid of big tiles.
            items(list.chunked(2), key = { "r-${d.id}-${it.first().id}" }) { row ->
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    row.forEach { a -> ActionTile(d, a, compact = false, modifier = Modifier.weight(1f)) }
                    if (row.size == 1) Spacer(Modifier.weight(1f))
                }
            }
        }
    }
}
