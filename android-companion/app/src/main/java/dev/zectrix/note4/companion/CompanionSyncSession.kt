package dev.zectrix.note4.companion

import java.nio.ByteBuffer
import java.nio.ByteOrder

object SyncWire {
    const val HELLO_CURSORS = 4
    const val STATE = 1
    const val ACK = 3
    const val MAX_FRAME_SIZE = CompanionProtocol.HEADER_SIZE + 18 + DurableQueue.VALUE_CAPACITY

    fun validCursors(cursors: List<SyncCursor>): Boolean =
        cursors.size <= DurableQueue.CAPACITY && cursors.map { it.key }.distinct().size == cursors.size &&
            cursors.all { it.key in 1..0xffff && it.acknowledged in 0..0xffff_ffffL &&
                it.received in 0..0xffff_ffffL && it.pending in 0..0xffff_ffffL &&
                (it.pending == 0L || it.pending > it.acknowledged) }

    fun encodeCursors(cursors: List<SyncCursor>): ByteArray {
        require(validCursors(cursors))
        return ByteBuffer.allocate(4 + 14 * cursors.size).order(ByteOrder.LITTLE_ENDIAN).apply {
            put(1).put(cursors.size.toByte()).putShort(0)
            cursors.forEach { putShort(it.key.toShort()).putInt(it.acknowledged.toInt())
                .putInt(it.received.toInt()).putInt(it.pending.toInt()) }
        }.array()
    }

    fun decodeCursors(value: ByteArray): List<SyncCursor> {
        require(value.size >= 4)
        val input = ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN)
        require(input.get().toInt() == 1)
        val count = input.get().toInt() and 0xff
        require(input.short.toInt() == 0 && count <= DurableQueue.CAPACITY && value.size == 4 + 14 * count)
        return List(count) { SyncCursor(input.short.toInt() and 0xffff,
            input.int.toLong() and 0xffff_ffffL, input.int.toLong() and 0xffff_ffffL,
            input.int.toLong() and 0xffff_ffffL) }.also { require(validCursors(it)) }
    }

    fun decodeHelloAck(payload: ByteArray): Pair<CompanionProtocol.HelloAckStatus, List<SyncCursor>?> {
        var status: CompanionProtocol.HelloAckStatus? = null
        var cursors: List<SyncCursor>? = null
        for (field in CompanionProtocol.decodeTlvs(payload)) {
            when (field.type) {
                CompanionProtocol.HELLO_ACK_STATUS_TYPE -> {
                    require(status == null)
                    status = requireNotNull(CompanionProtocol.decodeHelloAckStatus(field.value))
                }
                HELLO_CURSORS -> {
                    require(cursors == null)
                    cursors = decodeCursors(field.value)
                }
                else -> require(!field.required)
            }
        }
        return requireNotNull(status) to cursors
    }

    fun encode(entry: DurableEntry, sequence: Long, result: Int? = null): ByteArray {
        val key = ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort(entry.key.toShort()).array()
        val revision = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(entry.revision.toInt()).array()
        val payload = CompanionProtocol.encodeTlv(1, true, key) +
            CompanionProtocol.encodeTlv(2, true, revision) +
            CompanionProtocol.encodeTlv(if (result == null) 3 else 4, true,
                if (result == null) entry.payload else byteArrayOf(result.toByte()))
        return CompanionProtocol.encode(CompanionProtocol.Header(
            messageClass = if (result == null) CompanionProtocol.MessageClass.DURABLE_STATE else CompanionProtocol.MessageClass.CONTROL,
            flags = if (result == null) CompanionProtocol.FLAG_ACK_REQUESTED or CompanionProtocol.FLAG_RETRIABLE else CompanionProtocol.FLAG_RESPONSE,
            messageType = if (result == null) STATE else ACK, requestId = sequence, sequence = sequence), payload)
    }

    fun decode(frame: CompanionProtocol.Frame, reply: Boolean): Pair<DurableEntry, Int> {
        val header = frame.header
        require(header.sequence in 1..0xffff_ffffL && header.requestId == header.sequence)
        require(header.flags == if (reply) CompanionProtocol.FLAG_RESPONSE else
            CompanionProtocol.FLAG_ACK_REQUESTED or CompanionProtocol.FLAG_RETRIABLE)
        var key = 0
        var revision = 0L
        var value: ByteArray? = null
        var result = -1
        var seen = 0
        for (field in CompanionProtocol.decodeTlvs(frame.payload)) {
            val bit = when (field.type) {
                1 -> {
                    require(field.value.size == 2)
                    key = ByteBuffer.wrap(field.value).order(ByteOrder.LITTLE_ENDIAN).short.toInt() and 0xffff
                    1
                }
                2 -> {
                    require(field.value.size == 4)
                    revision = ByteBuffer.wrap(field.value).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xffff_ffffL
                    2
                }
                3 -> { require(!reply && field.value.size <= DurableQueue.VALUE_CAPACITY); value = field.value; 4 }
                4 -> {
                    require(reply && field.value.size == 1 && field.value[0].toInt() in 0..3)
                    result = field.value[0].toInt()
                    4
                }
                else -> { require(!field.required); 0 }
            }
            require(seen and bit == 0)
            seen = seen or bit
        }
        require(seen == 7 && key != 0 && revision != 0L)
        return DurableEntry(key, revision, value ?: ByteArray(0)) to result
    }
}

enum class SyncSessionStatus { DISCONNECTED, ACTIVE, PROTOCOL_ERROR, STORE_ERROR, TIMEOUT }

/** All calls run under the GATT owner's monitor, including durable submissions. */
class CompanionSyncSession(private val queue: DurableQueue) {
    var status = SyncSessionStatus.DISCONNECTED
        private set
    private var peer = emptyList<SyncCursor>()
    private var outbound: ByteArray? = null
    private var outboundEntry: DurableEntry? = null
    private var outboundSequence = 0L
    private var nextSequence = 2L
    private var attempts = 0
    private var retryAt = 0L
    private var progressDeadline = 0L
    private var reply: ByteArray? = null
    private var afterReply = SyncSessionStatus.ACTIVE
    private var lastSequence = 0L
    private var lastPayload = ByteArray(0)
    private var idle = false

    fun start(cursors: List<SyncCursor>, now: Long): DurableResult {
        disconnect()
        val result = queue.reconcile(cursors)
        if (result == DurableResult.OK) {
            peer = cursors.toList()
            status = SyncSessionStatus.ACTIVE
            progressDeadline = now + 15_000
        }
        return result
    }

    fun disconnect() {
        status = SyncSessionStatus.DISCONNECTED
        peer = emptyList()
        outbound = null
        outboundEntry = null
        reply = null
        lastPayload = ByteArray(0)
        lastSequence = 0
        nextSequence = 2
        attempts = 0
        idle = false
        afterReply = SyncSessionStatus.ACTIVE
    }

    fun receive(frame: CompanionProtocol.Frame): Boolean {
        if (status != SyncSessionStatus.ACTIVE) return false
        val isReply = frame.header.messageClass == CompanionProtocol.MessageClass.CONTROL && frame.header.messageType == SyncWire.ACK
        val isState = frame.header.messageClass == CompanionProtocol.MessageClass.DURABLE_STATE && frame.header.messageType == SyncWire.STATE
        if (!isReply && !isState) return false
        try {
            val (entry, result) = SyncWire.decode(frame, isReply)
            if (isReply) {
                if (outbound == null || attempts == 0 || frame.header.sequence != outboundSequence ||
                    entry.key != outboundEntry?.key || entry.revision != outboundEntry?.revision) return true
                if (result != 0) {
                    status = if (result == 1) SyncSessionStatus.STORE_ERROR else SyncSessionStatus.PROTOCOL_ERROR
                } else if (!queue.acknowledge(entry.key, entry.revision)) {
                    status = SyncSessionStatus.STORE_ERROR
                } else outbound = null
                return true
            }
            require(frame.payload.size <= SyncWire.MAX_FRAME_SIZE - CompanionProtocol.HEADER_SIZE)
            require(frame.header.sequence >= lastSequence)
            if (frame.header.sequence == lastSequence) require(frame.payload.contentEquals(lastPayload))
            if (reply != null) require(frame.header.sequence == lastSequence)
            val accepted = queue.accept(entry)
            val code = when (accepted) {
                DurableResult.OK -> 0
                DurableResult.STORE_ERROR -> 1
                DurableResult.CAPACITY -> 3
                else -> 2
            }
            afterReply = when (code) { 0 -> SyncSessionStatus.ACTIVE; 1 -> SyncSessionStatus.STORE_ERROR; else -> SyncSessionStatus.PROTOCOL_ERROR }
            lastSequence = frame.header.sequence
            lastPayload = frame.payload.copyOf()
            reply = SyncWire.encode(entry, frame.header.sequence, code)
        } catch (_: IllegalArgumentException) {
            status = SyncSessionStatus.PROTOCOL_ERROR
        }
        return true
    }

    fun poll(now: Long, send: (ByteArray) -> Boolean) {
        if (status != SyncSessionStatus.ACTIVE) return
        if (converged()) { idle = true; return }
        if (idle) progressDeadline = now + 15_000
        idle = false
        if (now >= progressDeadline) { status = SyncSessionStatus.TIMEOUT; return }
        reply?.let {
            if (send(it)) {
                reply = null
                status = afterReply
                progressDeadline = now + 15_000
            }
            return
        }
        if (outbound == null) {
            val entry = queue.snapshot().firstOrNull() ?: return
            if (nextSequence >= 0xffff_ffffL) { status = SyncSessionStatus.PROTOCOL_ERROR; return }
            outboundSequence = nextSequence++
            outboundEntry = entry
            outbound = SyncWire.encode(entry, outboundSequence)
            attempts = 0
            retryAt = now
            progressDeadline = now + 15_000
        }
        if (now < retryAt) return
        if (attempts == 3) { status = SyncSessionStatus.TIMEOUT; return }
        if (send(outbound!!)) { attempts++; retryAt = now + 3_000 }
    }

    fun converged(): Boolean = status == SyncSessionStatus.ACTIVE && outbound == null && reply == null &&
        queue.snapshot().isEmpty() && peer.all { it.pending == 0L || (queue.incoming(it.key)?.revision ?: 0) >= it.pending }

    fun nextWakeMs(now: Long): Long? {
        if (status != SyncSessionStatus.ACTIVE || converged()) return null
        if (reply != null || (outbound == null && queue.snapshot().isNotEmpty())) return 20
        return minOf((progressDeadline - now).coerceAtLeast(0),
            if (outbound != null) (retryAt - now).coerceAtLeast(20) else 15_000)
    }
}
