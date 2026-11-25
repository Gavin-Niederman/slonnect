/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.sin

class PointerSmootherTest {
    private class Recorder {
        val sends = mutableListOf<Pair<Float, Float>>()
        val smoother = PointerSmoother({ dx, dy -> sends += dx to dy })
        val totalX get() = sends.sumOf { it.first.toDouble() }
    }

    private fun feed(r: Recorder, rateHz: Int, seconds: Double, v: (Double) -> Float) {
        val stepNs = 1_000_000_000L / rateHz
        var t = 1_000_000_000L
        val n = (seconds * rateHz).toInt()
        for (i in 0..n) {
            r.smoother.onReading(v(i.toDouble() / rateHz), 0f, t)
            t += stepNs
        }
    }

    // Upstream moved by v per event at 50 Hz; the speed must not depend on the sampling rate.
    @Test
    fun speedIsIndependentOfSamplingRate() {
        val at50 = Recorder().also { feed(it, 50, 1.0) { 0.02f } }
        val at200 = Recorder().also { feed(it, 200, 1.0) { 0.02f } }
        val expected = 0.02 * 50 // one second at upstream speed
        assertEquals(expected, at50.totalX, expected * 0.05)
        assertEquals(expected, at200.totalX, expected * 0.05)
    }

    @Test
    fun packetsArePaced() {
        val r = Recorder().also { feed(it, 200, 1.0) { 0.02f } }
        // 200 Hz sampling, at most one packet per 8 ms → ≤ 125 a second.
        assertTrue("sent ${r.sends.size}", r.sends.size in 90..126)
    }

    // Small fast wobble (hand tremor at 10 Hz) while holding still is mostly filtered out.
    @Test
    fun tremorIsDamped() {
        val raw = Recorder().also { feed(it, 200, 1.0) { t -> (0.01 * sin(2 * Math.PI * 10 * t)).toFloat() } }
        val peak = raw.sends.maxOf { abs(it.first) }
        // Unfiltered, one 8 ms packet of a 0.01 amplitude wobble moves up to ~0.01*50*0.008 = 0.004.
        assertTrue("peak $peak", peak < 0.0025f)
    }
}
