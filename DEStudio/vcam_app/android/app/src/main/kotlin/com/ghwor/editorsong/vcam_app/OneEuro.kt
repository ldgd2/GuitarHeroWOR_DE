package com.ghwor.editorsong.vcam_app

import kotlin.math.PI
import kotlin.math.abs

// Filtro "One Euro" (Casiez, Roussel y Vogel, CHI 2012): suaviza mucho cuando la senal esta quieta (quita el temblor
// de la deteccion) y casi nada cuando se mueve rapido (no retrasa una silaba ni un golpe de cabeza).
//   minCutoff: Hz de corte en reposo (menos = mas suave)    beta: cuanto sube el corte con la velocidad
class OneEuro(private val minCutoff: Double, private val beta: Double, private val dCutoff: Double = 1.0) {
    private var x: Double? = null
    private var dx = 0.0
    private var lastT = 0L

    private fun alpha(cutoff: Double, dt: Double): Double {
        val tau = 1.0 / (2 * PI * cutoff)
        return 1.0 / (1.0 + tau / dt)
    }

    fun filter(value: Double, tNanos: Long): Double {
        val prev = x
        if (prev == null) { x = value; lastT = tNanos; return value }
        val dt = ((tNanos - lastT) / 1e9).coerceIn(1e-3, 0.5)
        lastT = tNanos
        val d = (value - prev) / dt
        dx += alpha(dCutoff, dt) * (d - dx)
        val cutoff = minCutoff + beta * abs(dx)
        val out = prev + alpha(cutoff, dt) * (value - prev)
        x = out
        return out
    }

    fun reset() { x = null; dx = 0.0 }
}

// Un filtro por canal, creado al vuelo
class OneEuroBank(private val minCutoff: Double, private val beta: Double) {
    private val f = HashMap<String, OneEuro>()
    fun filter(key: String, v: Double, t: Long) = f.getOrPut(key) { OneEuro(minCutoff, beta) }.filter(v, t)
    fun reset() = f.clear()
}
