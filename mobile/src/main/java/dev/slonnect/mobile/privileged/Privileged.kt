/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.privileged

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.kde.kdeconnect_tp.BuildConfig
import rikka.shizuku.Shizuku

/**
 * Access to [PrivilegedService] through Shizuku (https://shizuku.rikka.app): the phone nodes
 * that Android only allows system apps to do (connect Bluetooth audio, toggle Bluetooth, shell).
 */
object Privileged {
    private const val TAG = "Slonnect/Privileged"
    private const val PERMISSION_REQUEST = 4711

    enum class State { NOT_INSTALLED, NOT_RUNNING, NEEDS_PERMISSION, READY }

    private val _state = MutableStateFlow(State.NOT_RUNNING)
    val state: StateFlow<State> = _state.asStateFlow()

    @Volatile private var service: IPrivileged? = null
    private var pending: CompletableDeferred<IPrivileged?>? = null

    fun init(context: Context) {
        Shizuku.addBinderReceivedListenerSticky { refresh(context) }
        Shizuku.addBinderDeadListener {
            service = null
            refresh(context)
        }
        Shizuku.addRequestPermissionResultListener { _, _ -> refresh(context) }
        refresh(context)
    }

    private val main = android.os.Handler(android.os.Looper.getMainLooper())

    fun refresh(context: Context) {
        _state.value = try {
            when {
                !Shizuku.pingBinder() -> if (isInstalled(context)) State.NOT_RUNNING else State.NOT_INSTALLED
                Shizuku.isPreV11() -> State.NOT_RUNNING
                Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED -> State.NEEDS_PERMISSION
                else -> State.READY
            }
        } catch (e: RuntimeException) {
            // Shizuku hands over its binder before it has finished attaching this app, and
            // throws "Not an attached client" until then (seen at app start while Shizuku runs).
            // Must never crash the app, which runs this from Application.onCreate; look again soon.
            Log.i(TAG, "Shizuku not ready yet: ${e.message}")
            main.postDelayed({ refresh(context) }, 1000)
            State.NOT_RUNNING
        }
    }

    fun requestPermission() {
        runCatching { if (Shizuku.pingBinder() && !Shizuku.isPreV11()) Shizuku.requestPermission(PERMISSION_REQUEST) }
            .onFailure { Log.w(TAG, "Shizuku permission request failed", it) }
    }

    /** What to tell the user when [state] isn't READY, or null. */
    fun problem(): String? = when (_state.value) {
        State.NOT_INSTALLED -> "Install Shizuku on the phone to allow this"
        State.NOT_RUNNING -> "Start Shizuku on the phone to allow this"
        State.NEEDS_PERMISSION -> "Allow Slonnect in Shizuku (open Slonnect → Setup)"
        State.READY -> null
    }

    private fun isInstalled(context: Context) =
        runCatching { context.packageManager.getPackageInfo("moe.shizuku.privileged.api", 0) }.isSuccess

    private suspend fun bind(context: Context): IPrivileged? {
        service?.let { if (it.asBinder().isBinderAlive) return it }
        refresh(context)
        if (_state.value != State.READY) return null
        val deferred = withContext(Dispatchers.Main) {
            pending ?: CompletableDeferred<IPrivileged?>().also { d ->
                pending = d
                val args = Shizuku.UserServiceArgs(ComponentName(context.packageName, PrivilegedService::class.java.name))
                    .daemon(false)
                    .processNameSuffix("privileged")
                    .debuggable(BuildConfig.DEBUG)
                    .version(BuildConfig.VERSION_CODE)
                Shizuku.bindUserService(args, object : ServiceConnection {
                    override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                        service = binder?.takeIf { it.pingBinder() }?.let { IPrivileged.Stub.asInterface(it) }
                        pending = null
                        d.complete(service)
                    }
                    override fun onServiceDisconnected(name: ComponentName?) {
                        service = null
                    }
                })
            }
        }
        return withTimeoutOrNull(10_000) { deferred.await() }
    }

    class Unavailable(message: String) : Exception(message)

    private suspend fun <T> use(context: Context, block: (IPrivileged) -> T): T {
        val s = bind(context) ?: throw Unavailable(problem() ?: "Shizuku didn't start Slonnect's helper")
        return withContext(Dispatchers.IO) { block(s) }
    }

    data class ExecResult(val exitCode: Int, val output: String)

    suspend fun exec(context: Context, command: String, timeoutMs: Long = 20_000): ExecResult = use(context) { s ->
        val raw = s.exec(command, timeoutMs)
        val code = raw.substringBefore('\n').toIntOrNull() ?: -1
        ExecResult(code, raw.substringAfter('\n', ""))
    }

    /** Throws with the reason when the phone's Bluetooth stack refuses. */
    suspend fun bluetooth(context: Context, address: String, connect: Boolean) = use(context) { s ->
        val error = s.bluetooth(address, if (connect) "connect" else "disconnect")
        if (error.isNotEmpty()) {
            Log.w(TAG, "bluetooth $address: $error")
            throw IllegalStateException(error)
        }
    }
}
