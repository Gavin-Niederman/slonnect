/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile

import kotlin.math.PI
import kotlin.math.abs

/**
 * Turns gyroscope readings into presenter pointer moves that look smooth on the desktop.
 *
 * Upstream sent one packet per sensor event with `dx = angular velocity * sensitivity`, so
 * the speed depended on how evenly events arrived, hand tremor went straight through, and the
 * desktop saw ~50 jumps a second. Here:
 *  - readings are integrated over the real time between them (event timestamps), scaled so
 *    the speed matches upstream at its 50 Hz sampling rate;
 *  - a One-Euro filter (Casiez et al., CHI 2012) removes tremor while the pointer moves
 *    slowly and stays responsive when it moves fast;
 *  - moves are accumulated and handed to [send] at most every [sendIntervalNs] (~120 Hz),
 *    so the sensor can run faster than the link without flooding it.
 * The desktop eases between the packets it gets (nix/kdeconnect-presenter-smooth.patch).
 */
class PointerSmoother(
    private val send: (dx: Float, dy: Float) -> Unit,
    private val sendIntervalNs: Long = 8_000_000L,
) {
    private val fx = OneEuro()
    private val fy = OneEuro()
    private var lastEventNs = 0L
    private var lastSendNs = 0L
    private var pendingX = 0f
    private var pendingY = 0f

    fun reset() {
        fx.reset(); fy.reset()
        lastEventNs = 0L
        lastSendNs = 0L
        pendingX = 0f; pendingY = 0f
    }

    /** [vx], [vy] are already scaled angular velocities (upstream's per-event deltas). */
    fun onReading(vx: Float, vy: Float, timestampNs: Long) {
        if (lastEventNs == 0L) {
            lastEventNs = timestampNs
            lastSendNs = timestampNs
            return
        }
        val dtNs = (timestampNs - lastEventNs).coerceIn(1_000_000L, 100_000_000L)
        lastEventNs = timestampNs
        val dt = dtNs / 1e9
        val sx = fx.filter(vx.toDouble(), dt)
        val sy = fy.filter(vy.toDouble(), dt)
        // Upstream moved by v per event at 50 Hz, i.e. 50·v per second.
        pendingX += (sx * dt * UPSTREAM_RATE_HZ).toFloat()
        pendingY += (sy * dt * UPSTREAM_RATE_HZ).toFloat()
        if (timestampNs - lastSendNs >= sendIntervalNs && (pendingX != 0f || pendingY != 0f)) {
            send(pendingX, pendingY)
            pendingX = 0f; pendingY = 0f
            lastSendNs = timestampNs
        }
    }

    private class OneEuro(
        private val minCutoffHz: Double = 2.0,
        private val beta: Double = 0.4,
        private val derivativeCutoffHz: Double = 1.0,
    ) {
        private var x = 0.0
        private var dx = 0.0
        private var primed = false

        fun reset() { primed = false; x = 0.0; dx = 0.0 }

        fun filter(value: Double, dt: Double): Double {
            if (!primed) {
                primed = true
                x = value
                return value
            }
            val rawDx = (value - x) / dt
            dx += alpha(derivativeCutoffHz, dt) * (rawDx - dx)
            val cutoff = minCutoffHz + beta * abs(dx)
            x += alpha(cutoff, dt) * (value - x)
            return x
        }

        private fun alpha(cutoffHz: Double, dt: Double): Double {
            val tau = 1.0 / (2 * PI * cutoffHz)
            return 1.0 / (1.0 + tau / dt)
        }
    }

    companion object {
        private const val UPSTREAM_RATE_HZ = 50.0
        /** 200 Hz: the fastest rate allowed without the HIGH_SAMPLING_RATE_SENSORS permission. */
        const val SAMPLING_PERIOD_US = 5_000
    }
}
