/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/** An action published by a desktop running Slonnect (kdeconnect.slonnect.actions). */
data class RemoteAction(val id: String, val name: String, val icon: String, val description: String, val color: String?)

/** Progress of one action run the phone asked for. */
data class ActionRun(
    val runId: String,
    val deviceId: String,
    val actionId: String,
    val state: State,
    val step: String? = null,
    val error: String? = null,
    val startedAt: Long = System.currentTimeMillis(),
) {
    enum class State { SENT, RUNNING, DONE, FAILED }
}

/** What the UI shows about each desktop: its actions and the runs started from here. */
object SlonnectState {
    private val _actions = MutableStateFlow<Map<String, List<RemoteAction>>>(emptyMap())
    /** deviceId → actions; a device is present once its Slonnect desktop app said hello. */
    val actions: StateFlow<Map<String, List<RemoteAction>>> = _actions.asStateFlow()

    private val _runs = MutableStateFlow<Map<String, ActionRun>>(emptyMap())
    /** runId → run, most recent per action kept. */
    val runs: StateFlow<Map<String, ActionRun>> = _runs.asStateFlow()

    private val _desktops = MutableStateFlow<Map<String, String>>(emptyMap())
    /** deviceId → app version string of desktops that run Slonnect. */
    val desktops: StateFlow<Map<String, String>> = _desktops.asStateFlow()

    fun setDesktop(deviceId: String, app: String) = _desktops.update { it + (deviceId to app) }
    fun forgetDesktop(deviceId: String) {
        _desktops.update { it - deviceId }
    }

    fun setActions(deviceId: String, list: List<RemoteAction>) = _actions.update { it + (deviceId to list) }

    fun putRun(run: ActionRun) = _runs.update { runs ->
        // One entry per action and device: a new tap replaces the last one.
        runs.filterValues { !(it.actionId == run.actionId && it.deviceId == run.deviceId && it.runId != run.runId) } + (run.runId to run)
    }

    fun updateRun(runId: String, change: (ActionRun) -> ActionRun) = _runs.update { runs ->
        runs[runId]?.let { runs + (runId to change(it)) } ?: runs
    }

    fun lastRun(deviceId: String, actionId: String): ActionRun? =
        _runs.value.values.filter { it.deviceId == deviceId && it.actionId == actionId }.maxByOrNull { it.startedAt }
}
