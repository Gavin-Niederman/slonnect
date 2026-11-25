/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile

import android.os.Build

/**
 * Lets the app reach the host's kdeconnectd from the Android emulator (`just emulate`).
 *
 * The emulator sits behind a NAT, so LAN discovery can't find it. Instead it sends its identity
 * to the host (10.0.2.2, which the daemon sees as 127.0.0.1) and listens on a fixed TCP port
 * that `adb forward` exposes on the host, so the daemon's connection back to 127.0.0.1:<port>
 * lands in the emulator.
 */
object Emulator {
    /** Port `just emulate` forwards (adb forward tcp:1760 tcp:1760). Inside KDE Connect's range. */
    const val TCP_PORT = 1760
    /** The host machine's loopback, as seen from inside the emulator. */
    const val HOST = "10.0.2.2"

    val isEmulator: Boolean by lazy {
        Build.HARDWARE == "ranchu" || Build.HARDWARE == "goldfish" ||
            Build.FINGERPRINT.startsWith("generic") || Build.PRODUCT.contains("sdk_gphone")
    }

    val tcpPort: Int? get() = if (isEmulator) TCP_PORT else null
    val host: String? get() = if (isEmulator) HOST else null
}
