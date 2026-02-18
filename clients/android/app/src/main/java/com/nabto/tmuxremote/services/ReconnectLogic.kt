package com.nabto.tmuxremote.services

import kotlin.math.min
import kotlin.math.pow

class ReconnectLogic {
    val maxBackoff: Double = 15.0
    val maxTotalTime: Double = 30.0

    fun backoff(attempt: Int): Double {
        val value = 2.0.pow((attempt - 1).toDouble())
        return min(value, maxBackoff)
    }

    fun shouldGiveUp(elapsedSeconds: Double): Boolean {
        return elapsedSeconds > maxTotalTime
    }
}
