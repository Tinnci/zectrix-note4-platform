package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test

class ReaderProgressTest {
    private val progress = ReaderProgress("test.epub", 30000, 2, 451, false, 45)

    @Test fun firmwareLayoutAndUnicode() {
        val bytes = requireNotNull(ReaderProgressCodec.encode(progress))
        assertArrayEquals(byteArrayOf(1, 0, 2, 0, 0xc3.toByte(), 1, 0, 0, 0x30, 0x75, 0, 0, 45, 0, 9),
            bytes.copyOfRange(0, 15))
        assertArrayEquals("test.epub".toByteArray(), bytes.copyOfRange(15, bytes.size))
        assertEquals(progress, ReaderProgressCodec.decode(bytes))
        val chinese = progress.copy(bookId = "阅读笔记.txt", offset = 0xffff_ffffL, largeFont = true)
        assertEquals(chinese, ReaderProgressCodec.decode(requireNotNull(ReaderProgressCodec.encode(chinese))))
    }

    @Test fun malformedValuesAreRejected() {
        val bytes = requireNotNull(ReaderProgressCodec.encode(progress))
        assertNull(ReaderProgressCodec.decode(bytes.copyOf(bytes.size - 1)))
        assertNull(ReaderProgressCodec.decode(bytes + byteArrayOf(0)))
        for (index in listOf(0, 1, 14, 15)) {
            val broken = bytes.copyOf()
            broken[index] = 0xff.toByte()
            assertNull(ReaderProgressCodec.decode(broken))
        }
        assertNull(ReaderProgressCodec.encode(progress.copy(bookId = "../book.txt")))
        assertNull(ReaderProgressCodec.encode(progress.copy(bookId = "中".repeat(22))))
        assertNull(ReaderProgressCodec.encode(progress.copy(perMille = 1001)))
        assertNull(ReaderProgressCodec.encode(progress.copy(offset = -1)))
    }

    @Test fun resumeUsesDurableRevisionsAcrossRestartAndAck() {
        class Store : DurableStorage {
            var bytes: ByteArray? = null
            var fail = false
            override fun read() = bytes?.copyOf()
            override fun write(bytes: ByteArray): Boolean {
                if (fail) return false
                this.bytes = bytes.copyOf()
                return true
            }
        }
        val store = Store()
        val queue = DurableQueue(store)
        assertTrue(queue.load())
        assertTrue(ReaderProgressCodec.enqueue(queue, progress))
        val reboot = DurableQueue(store)
        assertTrue(reboot.load())
        assertTrue(ReaderProgressCodec.enqueue(reboot, progress.copy(offset = 900)))
        assertEquals(2L, reboot.cursors().single().pending)
        assertTrue(reboot.acknowledge(ReaderProgressCodec.SYNC_KEY, 2))
        store.fail = true
        assertFalse(ReaderProgressCodec.enqueue(reboot, progress))
        assertEquals(2L, reboot.cursors().single().acknowledged)
        store.fail = false
        assertTrue(ReaderProgressCodec.enqueue(reboot, progress))
        assertEquals(3L, reboot.cursors().single().pending)
    }
}
