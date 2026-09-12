package dev.zectrix.note4.companion

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction

data class ReaderProgress(
    val bookId: String,
    val sourceBytes: Long,
    val chapter: Int,
    val offset: Long,
    val largeFont: Boolean,
    val perMille: Int,
)

object ReaderProgressCodec {
    const val SYNC_KEY = 0x0101

    fun encode(progress: ReaderProgress): ByteArray? = runCatching {
        val name = progress.bookId.toByteArray(Charsets.UTF_8)
        require(name.toString(Charsets.UTF_8) == progress.bookId)
        require(name.size in 1..63 && progress.bookId !in setOf(".", ".."))
        require(progress.bookId.none { it.code < 0x20 || it.code == 0x7f || it == '/' || it == '\\' })
        require(progress.sourceBytes in 0..0xffff_ffffL && progress.offset in 0..0xffff_ffffL)
        require(progress.chapter in 0..65535 && progress.perMille in 0..1000)
        val output = ByteBuffer.allocate(15 + name.size).order(ByteOrder.LITTLE_ENDIAN)
        output.put(1).put(if (progress.largeFont) 1 else 0)
        output.putShort(progress.chapter.toShort()).putInt(progress.offset.toInt())
        output.putInt(progress.sourceBytes.toInt()).putShort(progress.perMille.toShort())
        output.put(name.size.toByte()).put(name)
        output.array()
    }.getOrNull()

    fun decode(bytes: ByteArray): ReaderProgress? = runCatching {
        require(bytes.size in 16..78)
        val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(input.get().toInt() == 1)
        val font = input.get().toInt()
        require(font in 0..1)
        val chapter = input.short.toInt() and 0xffff
        val offset = input.int.toLong() and 0xffff_ffffL
        val sourceBytes = input.int.toLong() and 0xffff_ffffL
        val progress = input.short.toInt() and 0xffff
        val length = input.get().toInt() and 0xff
        require(length == input.remaining())
        val name = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(input).toString()
        ReaderProgress(name, sourceBytes, chapter, offset, font == 1, progress).also {
            require(encode(it)?.contentEquals(bytes) == true)
        }
    }.getOrNull()

    // The GATT owner serializes this with other queue operations.
    fun enqueue(queue: DurableQueue, progress: ReaderProgress): Boolean {
        val payload = encode(progress) ?: return false
        val cursor = queue.cursors().firstOrNull { it.key == SYNC_KEY }
        val revision = maxOf(cursor?.acknowledged ?: 0L, cursor?.pending ?: 0L) + 1
        if (revision > 0xffff_ffffL) return false
        return queue.put(DurableEntry(SYNC_KEY, revision, payload))
    }
}
