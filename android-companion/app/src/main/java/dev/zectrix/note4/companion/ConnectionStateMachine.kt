package dev.zectrix.note4.companion

enum class ConnectionState {
    UNASSOCIATED, ASSOCIATED, PRESENT, CONNECTING, DISCOVERING, NEGOTIATING,
    READY, BACKOFF, STOPPED,
}

enum class ConnectionEvent {
    ASSOCIATED, APPEARED, CONNECT, GATT_CONNECTED, SERVICES_READY,
    NEGOTIATED, LOST, RETRY, DISASSOCIATED, STOP,
}

class ConnectionStateMachine(initial: ConnectionState = ConnectionState.UNASSOCIATED) {
    var state: ConnectionState = initial
        private set

    fun accept(event: ConnectionEvent): Boolean {
        val next = when (state to event) {
            ConnectionState.UNASSOCIATED to ConnectionEvent.ASSOCIATED -> ConnectionState.ASSOCIATED
            ConnectionState.ASSOCIATED to ConnectionEvent.APPEARED -> ConnectionState.PRESENT
            ConnectionState.PRESENT to ConnectionEvent.CONNECT -> ConnectionState.CONNECTING
            ConnectionState.CONNECTING to ConnectionEvent.GATT_CONNECTED -> ConnectionState.DISCOVERING
            ConnectionState.DISCOVERING to ConnectionEvent.SERVICES_READY -> ConnectionState.NEGOTIATING
            ConnectionState.NEGOTIATING to ConnectionEvent.NEGOTIATED -> ConnectionState.READY
            ConnectionState.BACKOFF to ConnectionEvent.RETRY -> ConnectionState.CONNECTING
            else -> when (event) {
                ConnectionEvent.LOST -> if (state != ConnectionState.UNASSOCIATED && state != ConnectionState.STOPPED) ConnectionState.BACKOFF else null
                ConnectionEvent.DISASSOCIATED -> ConnectionState.UNASSOCIATED
                ConnectionEvent.STOP -> ConnectionState.STOPPED
                else -> null
            }
        } ?: return false
        state = next
        return true
    }
}
