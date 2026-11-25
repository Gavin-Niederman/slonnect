/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.ui

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import org.kde.kdeconnect.BackgroundService
import org.kde.kdeconnect.ui.MainActivity

/**
 * Slonnect's home: computers with their actions and controls, the action launcher, and setup.
 * Deeper screens (pairing, plugin settings, touchpad, media, …) are still KDE Connect's, opened
 * from here.
 */
class SlonnectActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent { SlonnectTheme { SlonnectApp(this) } }
    }

    override fun onStart() {
        super.onStart()
        // Starts the connection service and announces this phone again, like KDE Connect's
        // MainActivity does; without it nothing connects.
        refreshConnections()
    }

    fun refreshConnections() = BackgroundService.ForceRefreshConnections(applicationContext)

    fun openDevice(deviceId: String) =
        startActivity(Intent(this, MainActivity::class.java).putExtra(MainActivity.EXTRA_DEVICE_ID, deviceId))

    fun openPairing() =
        startActivity(Intent(this, MainActivity::class.java).putExtra(MainActivity.FLAG_FORCE_OVERVIEW, true))

    fun openKdeConnectSettings() = startActivity(Intent(this, MainActivity::class.java))
}

private enum class Tab(val label: String) { Devices("Devices"), Actions("Actions"), Setup("Setup") }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SlonnectApp(activity: SlonnectActivity) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    val devices = rememberDevices()
    val setupIssues = rememberSetupIssues()
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()

    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeTopAppBar(
                title = { Text(if (tab == 0) "Slonnect" else Tab.entries[tab].label) },
                actions = {
                    IconButton(onClick = activity::refreshConnections) { Icon(Icons.Filled.Refresh, "Look for devices again") }
                    IconButton(onClick = activity::openKdeConnectSettings) { Icon(Icons.Filled.Settings, "KDE Connect settings") }
                },
                scrollBehavior = scroll,
            )
        },
        bottomBar = {
            NavigationBar {
                Tab.entries.forEachIndexed { i, t ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        label = { Text(t.label) },
                        icon = {
                            when (t) {
                                Tab.Devices -> Icon(Icons.Filled.Home, null)
                                Tab.Actions -> Icon(Icons.Filled.PlayArrow, null)
                                Tab.Setup -> BadgedBox(badge = { if (setupIssues > 0) Badge { Text("$setupIssues") } }) {
                                    Icon(Icons.Filled.Build, null)
                                }
                            }
                        },
                    )
                }
            }
        },
    ) { padding ->
        AnimatedContent(tab, Modifier.padding(padding), transitionSpec = { fadeIn() togetherWith fadeOut() }, label = "tab") { t ->
            when (Tab.entries[t]) {
                Tab.Devices -> DevicesScreen(activity, devices, setupIssues, onOpenSetup = { tab = 2 })
                Tab.Actions -> ActionsScreen(devices)
                Tab.Setup -> SetupScreen(activity)
            }
        }
    }
}
