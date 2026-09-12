package dev.zectrix.note4.companion

import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import java.util.zip.CRC32

data class DurableEntry(val key: Int, val revision: Long, val payload: ByteArray)
data class SyncCursor(val key: Int, val acknowledged: Long, val received: Long, val pending: Long)
enum class DurableResult { OK, INVALID, STORE_ERROR, RESYNC_REQUIRED, CAPACITY }

interface DurableStorage {
    fun read(): ByteArray?
    fun write(bytes: ByteArray): Boolean
}

class FileDurableStorage(private val file: File) : DurableStorage {
    override fun read(): ByteArray? {
        if (!file.exists()) return null
        val length = file.length()
        require(length in 12..DurableQueue.MAX_STORE_SIZE.toLong())
        return file.inputStream().use { input ->
            val bytes = ByteArray(length.toInt())
            var offset = 0
            while (offset < bytes.size) {
                val count = input.read(bytes, offset, bytes.size - offset)
                require(count > 0)
                offset += count
            }
            require(input.read() == -1)
            bytes
        }
    }

    override fun write(bytes: ByteArray): Boolean = try {
        val temporary = File(file.parentFile, "${file.name}.tmp")
        temporary.parentFile?.mkdirs()
        FileOutputStream(temporary).use { output ->
            output.write(bytes)
            output.fd.sync()
        }
        // Atomic replacement keeps the last committed file intact on a crash.
        Files.move(temporary.toPath(), file.toPath(),
            StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        FileChannel.open(file.absoluteFile.parentFile!!.toPath(), StandardOpenOption.READ).use {
            it.force(true)
        }
        true
    } catch (_: Exception) {
        false
    }
}

class DurableQueue(private val storage: DurableStorage) {
    constructor(file: File) : this(FileDurableStorage(file))

    companion object {
        const val CAPACITY = 8
        const val VALUE_CAPACITY = 256
        const val MAX_STORE_SIZE = 12 + CAPACITY * (18 + 2 * VALUE_CAPACITY)
        private const val MAGIC = 0x51445a43
        private const val VERSION = 2
    }

    private data class State(val key: Int, val acknowledged: Long = 0,
                             val incoming: DurableEntry? = null, val pending: DurableEntry? = null)
    private var entries = linkedMapOf<Int, State>()
    private var loaded = false

    fun load(): Boolean {
        entries.clear()
        loaded = false
        return try {
            val bytes = storage.read()
            val decoded = linkedMapOf<Int, State>()
            if (bytes != null) {
                require(bytes.size in 12..MAX_STORE_SIZE)
                val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
                val crc = CRC32().apply { update(bytes, 0, bytes.size - 4) }.value
                require(input.getInt(bytes.size - 4).toLong() and 0xffff_ffffL == crc)
                input.limit(bytes.size - 4)
                require(input.int == MAGIC)
                val version = input.u16()
                require(version == 1 || version == VERSION)
                val count = input.u16()
                require(count <= CAPACITY)
                repeat(count) {
                    val key = input.u16()
                    require(key != 0 && key !in decoded)
                    val state = if (version == 1) {
                        val revision = input.u32()
                        val size = input.u16()
                        require(input.u16() == 0 && revision != 0L && size <= VALUE_CAPACITY)
                        State(key, pending = DurableEntry(key, revision, ByteArray(size).also(input::get)))
                    } else {
                        val acknowledged = input.u32()
                        val received = input.u32()
                        val pending = input.u32()
                        val pendingSize = input.u16()
                        val incomingSize = input.u16()
                        require(pendingSize <= VALUE_CAPACITY && incomingSize <= VALUE_CAPACITY)
                        require((pending == 0L && pendingSize == 0) || pending > acknowledged)
                        require(received != 0L || incomingSize == 0)
                        val outgoing = ByteArray(pendingSize).also(input::get)
                        val incoming = ByteArray(incomingSize).also(input::get)
                        State(key, acknowledged,
                            if (received == 0L) null else DurableEntry(key, received, incoming),
                            if (pending == 0L) null else DurableEntry(key, pending, outgoing))
                    }
                    decoded[key] = state
                }
                require(!input.hasRemaining())
            }
            entries = decoded
            loaded = true
            true
        } catch (_: Exception) {
            false
        }
    }

    fun put(entry: DurableEntry): Boolean {
        if (!loaded || !valid(entry)) return false
        val old = entries[entry.key]
        if (entry.revision <= (old?.acknowledged ?: 0)) return false
        old?.pending?.let {
            if (entry.revision <= it.revision) return entry.revision == it.revision && entry.payload.contentEquals(it.payload)
        }
        if (old == null && entries.size == CAPACITY) return false
        return change { put(entry.key, (old ?: State(entry.key)).copy(pending = entry.copied())) }
    }

    fun enqueue(key: Int, payload: ByteArray): Boolean {
        val state = entries[key]
        if (state?.pending?.payload?.contentEquals(payload) == true) return true
        val revision = maxOf(state?.acknowledged ?: 0L, state?.pending?.revision ?: 0L) + 1
        return put(DurableEntry(key, revision, payload))
    }

    fun acknowledge(key: Int, revision: Long): Boolean {
        if (!loaded || revision !in 1..0xffff_ffffL) return false
        val old = entries[key] ?: return false
        if (revision <= old.acknowledged) return true
        val pending = old.pending ?: return false
        if (revision > pending.revision) return false
        return change { put(key, old.copy(acknowledged = revision,
            pending = if (pending.revision == revision) null else pending)) }
    }

    fun accept(entry: DurableEntry): DurableResult {
        if (!loaded) return DurableResult.STORE_ERROR
        if (!valid(entry)) return DurableResult.INVALID
        val old = entries[entry.key]
        old?.incoming?.let {
            if (entry.revision < it.revision) return DurableResult.OK
            if (entry.revision == it.revision) return if (entry.payload.contentEquals(it.payload)) DurableResult.OK else DurableResult.INVALID
        }
        if (old == null && entries.size == CAPACITY) return DurableResult.CAPACITY
        return if (change { put(entry.key, (old ?: State(entry.key)).copy(incoming = entry.copied())) }) DurableResult.OK else DurableResult.STORE_ERROR
    }

    fun incoming(key: Int): DurableEntry? = entries[key]?.incoming?.copied()
    fun snapshot(): List<DurableEntry> = entries.values.mapNotNull { it.pending?.copied() }
    fun cursors(): List<SyncCursor> = entries.values.map {
        SyncCursor(it.key, it.acknowledged, it.incoming?.revision ?: 0, it.pending?.revision ?: 0)
    }

    fun reconcile(peer: List<SyncCursor>): DurableResult {
        if (!loaded) return DurableResult.STORE_ERROR
        if (!SyncWire.validCursors(peer)) return DurableResult.INVALID
        val local = cursors().associateBy { it.key }
        val remote = peer.associateBy { it.key }
        val keys = local.keys + remote.keys
        if (keys.size > CAPACITY) return DurableResult.CAPACITY
        for (key in keys) {
            val own = local[key] ?: SyncCursor(key, 0, 0, 0)
            val other = remote[key] ?: SyncCursor(key, 0, 0, 0)
            if (other.received !in own.acknowledged..maxOf(own.acknowledged, own.pending) ||
                own.received !in other.acknowledged..maxOf(other.acknowledged, other.pending)) return DurableResult.RESYNC_REQUIRED
        }
        if (peer.all { entries[it.key]?.acknowledged == it.received }) return DurableResult.OK
        return if (change {
            for (other in peer) {
                val old = get(other.key) ?: State(other.key)
                put(other.key, old.copy(acknowledged = other.received,
                    pending = old.pending?.takeIf { it.revision > other.received }))
            }
        }) DurableResult.OK else DurableResult.STORE_ERROR
    }

    private fun change(block: LinkedHashMap<Int, State>.() -> Unit): Boolean {
        val previous = entries
        entries = LinkedHashMap(entries).apply(block)
        if (save()) return true
        entries = previous
        return false
    }

    private fun save(): Boolean = try {
        val size = 12 + entries.values.sumOf { 18 + (it.pending?.payload?.size ?: 0) + (it.incoming?.payload?.size ?: 0) }
        val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(MAGIC).putShort(VERSION.toShort()).putShort(entries.size.toShort())
        for (state in entries.values) {
            buffer.putShort(state.key.toShort()).putInt(state.acknowledged.toInt())
                .putInt((state.incoming?.revision ?: 0).toInt()).putInt((state.pending?.revision ?: 0).toInt())
                .putShort((state.pending?.payload?.size ?: 0).toShort()).putShort((state.incoming?.payload?.size ?: 0).toShort())
            state.pending?.payload?.let(buffer::put)
            state.incoming?.payload?.let(buffer::put)
        }
        buffer.putInt(CRC32().apply { update(buffer.array(), 0, buffer.position()) }.value.toInt())
        storage.write(buffer.array())
    } catch (_: Exception) {
        false
    }

    private fun valid(entry: DurableEntry) = entry.key in 1..0xffff && entry.revision in 1..0xffff_ffffL && entry.payload.size <= VALUE_CAPACITY
    private fun DurableEntry.copied() = copy(payload = payload.copyOf())
    private fun ByteBuffer.u16() = short.toInt() and 0xffff
    private fun ByteBuffer.u32() = int.toLong() and 0xffff_ffffL
}
