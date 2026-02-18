package com.nabto.tmuxremote.patterns

import java.util.UUID

enum class PatternType(val value: String) {
    YES_NO("yes_no"),
    NUMBERED_MENU("numbered_menu"),
    ACCEPT_REJECT("accept_reject");

    companion object {
        fun fromString(s: String): PatternType? = entries.find { it.value == s }
    }
}

data class ResolvedAction(
    val id: String = UUID.randomUUID().toString(),
    val label: String,
    val keys: String
)

data class PatternMatch(
    val id: String,           // instance_id
    val patternId: String,
    val patternType: PatternType,
    val prompt: String?,
    val actions: List<ResolvedAction>,
    val revision: Int
)
