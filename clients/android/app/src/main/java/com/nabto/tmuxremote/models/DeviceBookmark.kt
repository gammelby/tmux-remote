package com.nabto.tmuxremote.models

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class DeviceBookmark(
    @SerialName("product_id") val productId: String,
    @SerialName("device_id") val deviceId: String,
    @SerialName("device_fingerprint") val fingerprint: String,
    val sct: String,
    val name: String,
    @SerialName("last_session") var lastSession: String? = null,
    @SerialName("last_connected") var lastConnected: Long? = null
) {
    val id: String get() = deviceId
}
