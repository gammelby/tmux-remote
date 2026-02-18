package com.nabto.tmuxremote.models

data class SessionInfo(
    val name: String,
    val cols: Int,
    val rows: Int,
    val attached: Int
) {
    val id: String get() = name
}
