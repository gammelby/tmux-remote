package com.nabto.tmuxremote.models

data class PairingInfo(
    val productId: String,
    val deviceId: String,
    val username: String,
    val password: String,
    val sct: String
) {
    companion object {
        /** Parse a pairing string: p=<product>,d=<device>,u=<user>,pwd=<pass>,sct=<token> */
        fun parse(string: String): PairingInfo? {
            val trimmed = string.trim()
            if (trimmed.isEmpty()) return null

            val fields = mutableMapOf<String, String>()
            for (part in trimmed.split(",")) {
                val kv = part.split("=", limit = 2)
                if (kv.size == 2) {
                    fields[kv[0]] = kv[1]
                }
            }

            val productId = fields["p"] ?: return null
            val deviceId = fields["d"] ?: return null
            val password = fields["pwd"] ?: return null
            val sct = fields["sct"] ?: return null
            val username = fields["u"] ?: "owner"
            if (username.isEmpty()) return null

            return PairingInfo(
                productId = productId,
                deviceId = deviceId,
                username = username,
                password = password,
                sct = sct
            )
        }
    }
}
