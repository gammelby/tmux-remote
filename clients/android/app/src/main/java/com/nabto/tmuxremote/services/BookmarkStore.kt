package com.nabto.tmuxremote.services

import android.content.Context
import android.content.SharedPreferences
import com.nabto.tmuxremote.models.DeviceBookmark
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class BookmarkStore(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences("tmuxremote_prefs", Context.MODE_PRIVATE)

    private val json = Json { ignoreUnknownKeys = true }

    private val _devices = MutableStateFlow<List<DeviceBookmark>>(emptyList())
    val devices: StateFlow<List<DeviceBookmark>> = _devices

    var lastDeviceId: String?
        get() = prefs.getString(KEY_LAST_DEVICE, null)
        set(value) { prefs.edit().putString(KEY_LAST_DEVICE, value).apply() }

    init {
        load()
    }

    fun load() {
        val data = prefs.getString(KEY_DEVICES, null) ?: return
        _devices.value = try {
            json.decodeFromString<List<DeviceBookmark>>(data)
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun save() {
        val data = json.encodeToString(_devices.value)
        prefs.edit().putString(KEY_DEVICES, data).apply()
    }

    fun addDevice(bookmark: DeviceBookmark) {
        val list = _devices.value.toMutableList()
        val index = list.indexOfFirst { it.deviceId == bookmark.deviceId }
        if (index >= 0) {
            list[index] = bookmark
        } else {
            list.add(bookmark)
        }
        _devices.value = list
        save()
    }

    fun removeDevice(id: String) {
        _devices.value = _devices.value.filter { it.deviceId != id }
        if (lastDeviceId == id) {
            lastDeviceId = null
        }
        save()
    }

    fun updateLastSession(deviceId: String, session: String) {
        val list = _devices.value.toMutableList()
        val index = list.indexOfFirst { it.deviceId == deviceId }
        if (index < 0) return
        list[index] = list[index].copy(
            lastSession = session,
            lastConnected = System.currentTimeMillis()
        )
        _devices.value = list
        lastDeviceId = deviceId
        save()
    }

    fun clearLastSession(deviceId: String) {
        val list = _devices.value.toMutableList()
        val index = list.indexOfFirst { it.deviceId == deviceId }
        if (index < 0) return
        list[index] = list[index].copy(lastSession = null)
        _devices.value = list
        if (lastDeviceId == deviceId) {
            lastDeviceId = null
        }
        save()
    }

    fun bookmark(deviceId: String): DeviceBookmark? {
        return _devices.value.find { it.deviceId == deviceId }
    }

    companion object {
        private const val KEY_DEVICES = "tmuxremote_devices"
        private const val KEY_LAST_DEVICE = "tmuxremote_last_device"
    }
}
