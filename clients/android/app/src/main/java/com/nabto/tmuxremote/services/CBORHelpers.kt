package com.nabto.tmuxremote.services

import com.fasterxml.jackson.dataformat.cbor.CBORFactory
import com.fasterxml.jackson.dataformat.cbor.CBORGenerator
import com.fasterxml.jackson.dataformat.cbor.CBORParser
import com.nabto.tmuxremote.models.SessionInfo
import com.nabto.tmuxremote.patterns.PatternMatch
import com.nabto.tmuxremote.patterns.PatternType
import com.nabto.tmuxremote.patterns.ResolvedAction
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer

sealed class ControlStreamEvent {
    data class Sessions(val sessions: List<SessionInfo>) : ControlStreamEvent()
    data class PatternPresent(val match: PatternMatch) : ControlStreamEvent()
    data class PatternUpdate(val match: PatternMatch) : ControlStreamEvent()
    data class PatternGone(val instanceId: String) : ControlStreamEvent()
}

object CBORHelpers {

    private val factory = CBORFactory()

    // MARK: - Encoding

    fun encodeAttach(session: String, cols: Int, rows: Int): ByteArray {
        return encodeMap(
            "session" to session,
            "cols" to cols,
            "rows" to rows
        )
    }

    fun encodeCreate(session: String, cols: Int, rows: Int, command: String? = null): ByteArray {
        val pairs = mutableListOf<Pair<String, Any>>(
            "session" to session,
            "cols" to cols,
            "rows" to rows
        )
        if (command != null) {
            pairs.add("command" to command)
        }
        return encodeMap(*pairs.toTypedArray())
    }

    fun encodeResize(cols: Int, rows: Int): ByteArray {
        return encodeMap(
            "cols" to cols,
            "rows" to rows
        )
    }

    fun encodePatternResolve(instanceId: String, decision: String, keys: String? = null): ByteArray {
        val pairs = mutableListOf<Pair<String, Any>>(
            "type" to "pattern_resolve",
            "instance_id" to instanceId,
            "decision" to decision
        )
        if (keys != null) {
            pairs.add("keys" to keys)
        }
        val payload = encodeMap(*pairs.toTypedArray())
        val lengthPrefix = ByteBuffer.allocate(4).putInt(payload.size).array()
        return lengthPrefix + payload
    }

    fun encodePairingPayload(username: String): ByteArray {
        return encodeMap("Username" to username)
    }

    private fun encodeMap(vararg pairs: Pair<String, Any>): ByteArray {
        val baos = ByteArrayOutputStream()
        val gen: CBORGenerator = factory.createGenerator(baos)
        gen.writeStartObject()
        for ((key, value) in pairs) {
            gen.writeFieldName(key)
            when (value) {
                is String -> gen.writeString(value)
                is Int -> gen.writeNumber(value)
                is Long -> gen.writeNumber(value)
                else -> gen.writeString(value.toString())
            }
        }
        gen.writeEndObject()
        gen.close()
        return baos.toByteArray()
    }

    // MARK: - Decoding

    fun decodeSessions(data: ByteArray): List<SessionInfo> {
        val parser = factory.createParser(data)
        return decodeSessionsFromParser(parser)
    }

    fun decodeControlStreamEvent(data: ByteArray): ControlStreamEvent? {
        val map = decodeMap(data) ?: return null
        val type = map["type"] as? String ?: return null

        return when (type) {
            "sessions" -> {
                @Suppress("UNCHECKED_CAST")
                val sessionList = map["sessions"] as? List<Map<String, Any>> ?: return null
                val sessions = sessionList.mapNotNull { decodeSessionMap(it) }
                ControlStreamEvent.Sessions(sessions)
            }
            "pattern_present" -> {
                val match = decodePatternInstance(map) ?: return null
                ControlStreamEvent.PatternPresent(match)
            }
            "pattern_update" -> {
                val match = decodePatternInstance(map) ?: return null
                ControlStreamEvent.PatternUpdate(match)
            }
            "pattern_gone" -> {
                val instanceId = map["instance_id"] as? String ?: return null
                ControlStreamEvent.PatternGone(instanceId)
            }
            else -> null
        }
    }

    private fun decodePatternInstance(map: Map<String, Any>): PatternMatch? {
        val instanceId = map["instance_id"] as? String ?: return null
        val patternId = map["pattern_id"] as? String ?: return null
        val patternTypeStr = map["pattern_type"] as? String ?: return null
        val patternType = PatternType.fromString(patternTypeStr) ?: return null
        val prompt = map["prompt"] as? String

        @Suppress("UNCHECKED_CAST")
        val actionsList = map["actions"] as? List<Map<String, Any>> ?: return null
        val actions = actionsList.mapNotNull { actionMap ->
            val label = actionMap["label"] as? String ?: return@mapNotNull null
            val keys = actionMap["keys"] as? String ?: return@mapNotNull null
            ResolvedAction(label = label, keys = keys)
        }
        if (actions.isEmpty()) return null

        val revision = when (val rev = map["revision"]) {
            is Int -> rev
            is Long -> rev.toInt()
            else -> 1
        }

        return PatternMatch(
            id = instanceId,
            patternId = patternId,
            patternType = patternType,
            prompt = prompt,
            actions = actions,
            revision = revision
        )
    }

    private fun decodeSessionsFromParser(parser: CBORParser): List<SessionInfo> {
        val sessions = mutableListOf<SessionInfo>()
        // Expect array start
        if (parser.nextToken() == null) return emptyList()
        // Could be an array of session objects
        if (!parser.isExpectedStartArrayToken) return emptyList()
        while (parser.nextToken() != null && !parser.currentToken.isStructEnd) {
            val sessionMap = parseObject(parser)
            decodeSessionMap(sessionMap)?.let { sessions.add(it) }
        }
        parser.close()
        return sessions
    }

    private fun decodeSessionMap(map: Map<String, Any>): SessionInfo? {
        val name = map["name"] as? String ?: return null
        val cols = (map["cols"] as? Number)?.toInt() ?: 0
        val rows = (map["rows"] as? Number)?.toInt() ?: 0
        val attached = (map["attached"] as? Number)?.toInt() ?: 0
        return SessionInfo(name = name, cols = cols, rows = rows, attached = attached)
    }

    private fun decodeMap(data: ByteArray): Map<String, Any>? {
        return try {
            val parser = factory.createParser(data)
            parser.nextToken() // START_OBJECT
            if (!parser.isExpectedStartObjectToken) {
                parser.close()
                return null
            }
            val result = parseObject(parser)
            parser.close()
            result
        } catch (_: Exception) {
            null
        }
    }

    private fun parseObject(parser: CBORParser): Map<String, Any> {
        val map = mutableMapOf<String, Any>()
        while (parser.nextToken() != null && !parser.currentToken.isStructEnd) {
            val key = parser.currentName ?: continue
            parser.nextToken()
            val value = parseValue(parser)
            if (value != null) {
                map[key] = value
            }
        }
        return map
    }

    private fun parseValue(parser: CBORParser): Any? {
        return when {
            parser.currentToken.isNumeric -> parser.longValue
            parser.currentToken.isBoolean -> parser.booleanValue
            parser.isExpectedStartObjectToken -> parseObject(parser)
            parser.isExpectedStartArrayToken -> parseArray(parser)
            parser.currentToken == com.fasterxml.jackson.core.JsonToken.VALUE_STRING ->
                parser.text
            parser.currentToken == com.fasterxml.jackson.core.JsonToken.VALUE_NULL -> null
            else -> parser.text
        }
    }

    private fun parseArray(parser: CBORParser): List<Any?> {
        val list = mutableListOf<Any?>()
        while (parser.nextToken() != null && !parser.currentToken.isStructEnd) {
            list.add(parseValue(parser))
        }
        return list
    }
}
