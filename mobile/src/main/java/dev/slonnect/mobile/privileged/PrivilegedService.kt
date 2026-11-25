/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.privileged

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.system.exitProcess

/**
 * Shizuku user service: this class runs in a separate process started by Shizuku as the shell
 * user (uid 2000) or root, not in the app. The shell user holds BLUETOOTH_PRIVILEGED, which
 * Android 12+ requires to connect or disconnect another device's audio profiles, and hidden
 * APIs aren't blocked in this kind of process.
 *
 * Shizuku hands us a Context for the app's package; Bluetooth checks that the calling uid owns
 * the package in the attribution, so we build one for com.android.shell (or use the system
 * context as root) instead.
 */
class PrivilegedService(private val appContext: Context?) : IPrivileged.Stub() {
    constructor() : this(null)

    override fun destroy() {
        exitProcess(0)
    }

    override fun exec(command: String, timeoutMs: Long): String {
        val process = ProcessBuilder("sh", "-c", command).redirectErrorStream(true).start()
        val output = StringBuilder()
        val reader = Thread { output.append(process.inputStream.bufferedReader().readText()) }.apply { start() }
        val finished = process.waitFor(timeoutMs.coerceIn(100, 120_000), TimeUnit.MILLISECONDS)
        if (!finished) process.destroyForcibly()
        reader.join(1000)
        val code = if (finished) process.exitValue() else 124
        return "$code\n${output.toString().take(64 * 1024)}"
    }

    @SuppressLint("MissingPermission")
    override fun bluetooth(address: String, mode: String): String {
        return try {
            val adapter = bluetoothAdapter() ?: return "No Bluetooth adapter"
            if (!adapter.isEnabled) {
                if (mode == "disconnect") return ""
                exec("cmd bluetooth_manager enable", 5000)
                waitUntil(6000) { adapter.isEnabled }
            }
            val device = adapter.getRemoteDevice(address)
            if (mode == "connect") connect(adapter, device) else disconnect(adapter, device)
        } catch (e: Throwable) {
            (e.cause ?: e).let { "${it.javaClass.simpleName}: ${it.message}" }
        }
    }

    private fun connect(adapter: BluetoothAdapter, device: BluetoothDevice): String {
        // Android 13+: one call connects every profile the device is allowed to use.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val result = runCatching { device.javaClass.getMethod("connect").invoke(device) as Int }.getOrNull()
            if (result == 0 /* BluetoothStatusCodes.SUCCESS */) return ""
        }
        val errors = mutableListOf<String>()
        var any = false
        for (profile in AUDIO_PROFILES) {
            val proxy = proxy(adapter, profile) ?: continue
            try {
                runCatching {
                    proxy.javaClass.getMethod("setConnectionPolicy", BluetoothDevice::class.java, Int::class.javaPrimitiveType)
                        .invoke(proxy, device, CONNECTION_POLICY_ALLOWED)
                }
                val ok = proxy.javaClass.getMethod("connect", BluetoothDevice::class.java).invoke(proxy, device) as Boolean
                any = any || ok
            } catch (e: Throwable) {
                errors += "profile $profile: ${(e.cause ?: e).message}"
            } finally {
                adapter.closeProfileProxy(profile, proxy)
            }
        }
        return if (any) "" else errors.joinToString("; ").ifEmpty { "The phone's Bluetooth stack refused to connect" }
    }

    private fun disconnect(adapter: BluetoothAdapter, device: BluetoothDevice): String {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val result = runCatching { device.javaClass.getMethod("disconnect").invoke(device) as Int }.getOrNull()
            if (result == 0) return ""
        }
        var any = false
        for (profile in AUDIO_PROFILES) {
            val proxy = proxy(adapter, profile) ?: continue
            try {
                any = (proxy.javaClass.getMethod("disconnect", BluetoothDevice::class.java).invoke(proxy, device) as Boolean) || any
            } catch (_: Throwable) {
            } finally {
                adapter.closeProfileProxy(profile, proxy)
            }
        }
        return if (any) "" else "The phone's Bluetooth stack refused to disconnect"
    }

    private fun proxy(adapter: BluetoothAdapter, profile: Int): BluetoothProfile? {
        var result: BluetoothProfile? = null
        val latch = CountDownLatch(1)
        val listener = object : BluetoothProfile.ServiceListener {
            override fun onServiceConnected(p: Int, proxy: BluetoothProfile) {
                result = proxy
                latch.countDown()
            }
            override fun onServiceDisconnected(p: Int) {}
        }
        val requested = runCatching { adapter.getProfileProxy(shellContext(), listener, profile) }.getOrDefault(false)
        if (!requested) return null
        latch.await(3, TimeUnit.SECONDS)
        return result
    }

    private fun bluetoothAdapter(): BluetoothAdapter? =
        shellContext().getSystemService(BluetoothManager::class.java)?.adapter

    private val shellContextLazy by lazy @SuppressLint("PrivateApi", "DiscouragedPrivateApi") {
        val activityThread = Class.forName("android.app.ActivityThread")
        val thread = activityThread.getMethod("currentActivityThread").invoke(null)
            ?: activityThread.getMethod("systemMain").invoke(null)
        val system = activityThread.getMethod("getSystemContext").invoke(thread) as Context
        val pkg = if (android.os.Process.myUid() == 0) "android" else "com.android.shell"
        runCatching { system.createPackageContext(pkg, 0) }.getOrDefault(system)
    }

    private fun shellContext(): Context = shellContextLazy

    private fun waitUntil(timeoutMs: Long, condition: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!condition() && System.currentTimeMillis() < end) Thread.sleep(100)
    }

    companion object {
        private const val CONNECTION_POLICY_ALLOWED = 100
        // A2DP, HEADSET, and LE_AUDIO (22, API 33+).
        private val AUDIO_PROFILES = listOfNotNull(
            BluetoothProfile.A2DP,
            BluetoothProfile.HEADSET,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) 22 else null,
        )
    }
}
