package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.io.path.createTempDirectory

class CompanionQualificationTest {
    private val identity = ByteArray(16) { (it + 11).toByte() }
    private val address = "12:34:56:78:9A:BC"
    private fun record() = CompanionEnrollmentRecord(1, 1, 1, 0,
        CompanionProtocol.unhex("123456789abc"), ByteArray(16) { 7 }, 1, ByteArray(16) { 9 })

    @Test fun tapBindsTargetExpiresAndNeverReplays() {
        val handoff = EnrollmentHandoff()
        val source = record()
        assertTrue(handoff.offer(source, 500))
        source.token.fill(0)
        assertEquals(address, handoff.address)
        val payload = CompanionProtocol.decodeTlvs(handoff.take(address.lowercase(), identity, 501)!!).single()
        val (_, token, companion) = CompanionProtocol.decodeHelloEnrollmentProof(payload.value)!!
        assertArrayEquals(ByteArray(16) { 9 }, token)
        assertArrayEquals(identity, companion)
        assertNull(handoff.take(address, identity, 502))
        assertTrue(handoff.offer(record(), 500))
        assertThrows(IllegalArgumentException::class.java) { handoff.take("12:34:56:78:9A:BD", identity, 501) }
        assertNull(handoff.address)
        assertTrue(handoff.offer(record(), 500))
        assertThrows(IllegalArgumentException::class.java) { handoff.take(address, identity, 120500) }
        assertNull(handoff.take(address, identity, 120501))
        assertFalse(handoff.offer(record().copy(flags = 0), 0))
        assertFalse(handoff.offer(record().copy(bleRole = 2), 0))
        assertFalse(handoff.offer(record().copy(deviceId = ByteArray(16)), 0))
        assertFalse(handoff.offer(record().copy(bleAddressType = 1), 0))
        assertFalse(handoff.offer(record().copy(token = ByteArray(16)), 0))
    }

    @Test fun associationSelectionDoesNotLeakProofToTheFirstDevice() {
        val other = "22:33:44:55:66:77"
        assertEquals(address, selectApprovedPeer(listOf(other, address), address, other))
        assertNull(selectApprovedPeer(listOf(other), address, other))
        assertNull(selectApprovedPeer(listOf(address, other), null, null))
        assertEquals(other, selectApprovedPeer(listOf(address, other), null, other))
        assertEquals(address, selectApprovedPeer(listOf(address.lowercase()), null, null))
        assertNull(normalizeAddress("../../sync"))
        assertNull(normalizeAddress("null"))
    }

    @Test fun offlineReaderAndWeatherSurvivePhoneProcessRestart() {
        val root = createTempDirectory("note4-offline-").toFile()
        try {
            val file = File(root, "queue.bin")
            val queue = DurableQueue(file).also { assertTrue(it.load()) }
            val reading = ReaderProgress("离线阅读.epub", 10000, 3, 512, false, 200)
            assertEquals(DurableResult.OK, queue.accept(DurableEntry(ReaderProgressCodec.SYNC_KEY, 8, ReaderProgressCodec.encode(reading)!!)))
            val restarted = DurableQueue(file).also { assertTrue(it.load()) }
            assertEquals(reading, ReaderProgressCodec.decode(restarted.incoming(ReaderProgressCodec.SYNC_KEY)!!.payload))
            assertTrue(ReaderProgressCodec.enqueue(restarted, reading))
            assertTrue(restarted.enqueue(WeatherSync.KEY, WeatherSync.encode(WeatherSnapshot("Shanghai", -5, 61, 1800000000, 1800021600))))
            val reopened = DurableQueue(file).also { assertTrue(it.load()) }
            assertEquals(2, reopened.snapshot().size)
            assertEquals(1L, reopened.snapshot().first { it.key == ReaderProgressCodec.SYNC_KEY }.revision)
            assertNull(ReaderProgressCodec.encode(reading.copy(bookId = "bad\u007f.txt")))
        } finally { root.deleteRecursively() }
    }

    @Test fun coverConversionHasExactDimensionsPolarityAndBoundedGrayDither() {
        val white = MonochromeCover.encode(IntArray(120000) { -1 })
        assertEquals(15011, white.size)
        assertArrayEquals(MonochromeCover.HEADER, white.copyOfRange(0, 11))
        assertTrue(white.drop(11).all { it == 0.toByte() })
        val black = MonochromeCover.encode(IntArray(120000) { 0xff000000.toInt() })
        assertTrue(black.drop(11).all { it == 0xff.toByte() })
        assertArrayEquals(white, MonochromeCover.encode(IntArray(120000)))
        val gray = MonochromeCover.encode(IntArray(120000) { 0xff808080.toInt() })
        val blackPixels = gray.drop(11).sumOf { Integer.bitCount(it.toInt() and 255) }
        assertTrue(blackPixels in 57000..63000)
        val corners = IntArray(120000) { -1 }.apply { this[0] = 0xff000000.toInt(); this[lastIndex] = 0xff000000.toInt() }
        val encoded = MonochromeCover.encode(corners)
        assertEquals(0x80, encoded[11].toInt() and 255)
        assertEquals(1, encoded.last().toInt() and 255)
        assertThrows(IllegalArgumentException::class.java) { MonochromeCover.encode(IntArray(1)) }
    }

    @Test fun localTransferNeverAcceptsPublicHostsCredentialsOrInjectedPaths() {
        for (text in listOf("http://10.1.2.3", "http://192.168.4.1/", "http://172.16.3.2:8080"))
            assertTrue(TransferEndpoint.parse(text).origin.startsWith("http://"))
        for (text in listOf("https://192.168.4.1", "http://example.com", "http://8.8.8.8", "http://127.0.0.1",
            "http://192.168.4.1/secret", "http://user:code@192.168.4.1", "http://192.168.4.1?code=a",
            "http://192.168.4.1#fragment", "http://192.168.4.1:0", "http://010.0.0.1", "http://[::1]")) {
            assertThrows(text, Exception::class.java) { TransferEndpoint.parse(text) }
        }
        assertTrue(Note4TransferClient.validBookName("中文阅读.epub"))
        for (name in listOf("../file.txt", "a/b.txt", "bad\u0000.txt", "bad\u007f.epub", "book.pdf", "a".repeat(64) + ".txt"))
            assertFalse(Note4TransferClient.validBookName(name))
    }

    @Test fun weatherRejectsMalformedOrExpiredSnapshots() {
        val sample = WeatherSnapshot("上海", -15, 71, 1800000000, 1800021600)
        val bytes = WeatherSync.encode(sample)
        assertEquals(sample, WeatherSync.decode(bytes))
        assertTrue(WeatherSync.fresh(sample, 1800000000))
        assertFalse(WeatherSync.fresh(sample, 1800021600))
        assertFalse(WeatherSync.fresh(sample, 1799999600))
        assertNull(WeatherSync.decode(bytes.copyOf(bytes.size - 1)))
        assertNull(WeatherSync.decode(bytes.copyOf().apply { this[1] = 4 }))
        assertNull(WeatherSync.decode(bytes.copyOf().apply { this[13] = 0xff.toByte() }))
        assertThrows(IllegalArgumentException::class.java) { WeatherSync.encode(sample.copy(expiresAt = sample.observedAt + 21601)) }
        assertThrows(IllegalArgumentException::class.java) { WeatherSync.encode(sample.copy(place = "bad\nplace")) }
    }
}
