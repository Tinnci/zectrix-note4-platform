package dev.zectrix.note4.companion

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

data class DurableEntry(val key: Int, val revision: Long, val payload: ByteArray)

class DurableQueue(private val file: File) {
    companion object {
        const val CAPACITY = 8
        const val VALUE_CAPACITY = 256
        private const val MAGIC = 0x51445a43
        private const val VERSION = 1
    }

    private val entries = linkedMapOf<Int, DurableEntry>()

    fun load(): Boolean {
        entries.clear()
        if (!file.exists()) return true
        return try {
            val bytes = file.readBytes()
            if (bytes.size < 12) return false
            val storedCrc = ByteBuffer.wrap(bytes, bytes.size - 4, 4)
                .order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xffff_ffffL
            val crc = CRC32().apply { update(bytes, 0, bytes.size - 4) }.value
            if (crc != storedCrc) return false
            val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (input.int != MAGIC || input.short.toInt() != VERSION) return false
            val count = input.short.toInt() and 0xffff
            if (count > CAPACITY) return false
            repeat(count) {
                if (input.remaining() < 10) return false
                val key = input.short.toInt() and 0xffff
                val revision = input.int.toLong() and 0xffff_ffffL
                val size = input.short.toInt() and 0xffff
                input.short // reserved
                if (key == 0 || revision == 0L || size > VALUE_CAPACITY || input.remaining() < size + 4) return false
                val payload = ByteArray(size).also(input::get)
                if (entries.put(key, DurableEntry(key, revision, payload)) != null) return false
            }
            input.position() == bytes.size - 4
        } catch (_: Exception) {
            entries.clear()
            false
        }
    }

    fun put(entry: DurableEntry): Boolean {
        require(entry.key in 1..0xffff && entry.revision in 1..0xffff_ffffL)
        require(entry.payload.size <= VALUE_CAPACITY)
        val old = entries[entry.key]
        if (old != null && entry.revision <= old.revision) return false
        if (old == null && entries.size == CAPACITY) return false
        entries[entry.key] = entry.copy(payload = entry.payload.copyOf())
        if (save()) return true
        if (old == null) entries.remove(entry.key) else entries[entry.key] = old
        return false
    }

    fun acknowledge(key: Int, revision: Long): Boolean {
        val old = entries[key] ?: return false
        if (old.revision != revision) return false
        entries.remove(key)
        if (save()) return true
        entries[key] = old
        return false
    }

    fun snapshot(): List<DurableEntry> = entries.values.map { it.copy(payload = it.payload.copyOf()) }

    private fun save(): Boolean = try {
        val size = 8 + entries.values.sumOf { 10 + it.payload.size } + 4
        val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(MAGIC).putShort(VERSION.toShort()).putShort(entries.size.toShort())
        for (entry in entries.values) {
            buffer.putShort(entry.key.toShort()).putInt(entry.revision.toInt())
                .putShort(entry.payload.size.toShort()).putShort(0)
                .put(entry.payload)
        }
        val crc = CRC32().apply { update(buffer.array(), 0, buffer.position()) }.value
        buffer.putInt(crc.toInt())
        val temporary = File(file.parentFile, "${file.name}.tmp")
        temporary.parentFile?.mkdirs()
        temporary.writeBytes(buffer.array())
        if (file.exists() && !file.delete()) {
            false
        } else {
            temporary.renameTo(file)
        }
    } catch (_: Exception) {
        false
    }
}
