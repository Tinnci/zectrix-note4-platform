package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.concurrent.TimeUnit
import kotlin.io.path.createTempDirectory

class CompanionEndToEndTest {
    private class Peer(directory: File) : AutoCloseable {
        private val process = ProcessBuilder(requireNotNull(System.getProperty("zectrix.peer")), directory.path)
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        private val input = process.inputStream.bufferedReader()
        private val output = process.outputStream.bufferedWriter()
        fun command(value: String): String {
            output.write(value); output.newLine(); output.flush()
            return requireNotNull(input.readLine()) { "C++ peer terminated while processing ${value.substringBefore(' ')}" }
        }
        override fun close() {
            output.close()
            if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly()
            input.close()
        }
    }

    private fun start(peer: Peer, queue: DurableQueue): CompanionSyncSession {
        val own = CompanionProtocol.hex(SyncWire.encodeCursors(queue.cursors()))
        val remote = SyncWire.decodeCursors(CompanionProtocol.unhex(peer.command("cursors")))
        assertEquals("0", peer.command("start $own"))
        return CompanionSyncSession(queue).also { assertEquals(DurableResult.OK, it.start(remote, 0)) }
    }

    private fun receive(session: CompanionSyncSession, receiver: CompanionFragments.Reassembler, packet: ByteArray) {
        receiver.accept(packet)?.let {
            val frame = CompanionProtocol.decode(it) as CompanionProtocol.DecodeResult.Success
            assertTrue(session.receive(frame.frame))
        }
    }

    private fun peerPackets(peer: Peer, now: Long, capacity: Int): List<ByteArray> {
        val output = peer.command("poll $now $capacity")
        return if (output == "none") emptyList() else output.split(',').map(CompanionProtocol::unhex)
    }

    private fun converge(peer: Peer, queue: DurableQueue, capacity: Int) {
        val phone = start(peer, queue)
        val reassembly = CompanionFragments.Reassembler()
        repeat(100) { tick ->
            val now = tick * 10L
            phone.poll(now) { frame ->
                CompanionFragments.encode(frame, tick + 1, capacity).forEach {
                    assertTrue(peer.command("packet ${CompanionProtocol.hex(it)}") in listOf("partial", "ok"))
                }
                true
            }
            peerPackets(peer, now, capacity).forEach { receive(phone, reassembly, it) }
            assertEquals(SyncSessionStatus.ACTIVE, phone.status)
            if (phone.converged() && peer.command("converged") == "yes") return
        }
        fail("C++ and Kotlin did not converge")
    }

    @Test(timeout = 60000) fun ndefProofConsumptionPersistenceExpiryAndClockCrossTheLanguageBoundary() {
        val root = createTempDirectory("note4-enrollment-e2e-").toFile()
        val identity = ByteArray(16) { (it + 1).toByte() }
        try {
            Peer(root).use { peer ->
                fun proof(): ByteArray {
                    val bytes = CompanionProtocol.unhex(peer.command("ndef"))
                    assertEquals(0xd2, bytes[0].toInt() and 255)
                    val mimeLength = bytes[1].toInt() and 255
                    assertEquals(NfcEnrollmentParser.MIME_TYPE, bytes.copyOfRange(3, 3 + mimeLength).toString(Charsets.US_ASCII))
                    val record = NfcEnrollmentParser.parsePayload(bytes.copyOfRange(3 + mimeLength, bytes.size))!!
                    assertEquals("12:34:56:78:9A:BC", record.targetAddress())
                    val handoff = EnrollmentHandoff().also { assertTrue(it.offer(record, 0)) }
                    val field = CompanionProtocol.decodeTlvs(handoff.take(record.targetAddress()!!, identity, 1)!!).single()
                    assertNull(handoff.take(record.targetAddress()!!, identity, 2))
                    return field.value
                }
                val first = CompanionProtocol.hex(proof())
                assertEquals("0", peer.command("field"))
                assertEquals("0", peer.command("proof $first 0"))
                assertNotEquals("0", peer.command("proof $first 0"))
                assertEquals(CompanionProtocol.hex(identity), peer.command("identity"))
                val failedSave = CompanionProtocol.hex(proof())
                assertEquals("0", peer.command("field"))
                assertNotEquals("0", peer.command("proof $failedSave 1"))
                assertNotEquals("0", peer.command("proof $failedSave 0"))
                val expired = CompanionProtocol.hex(proof())
                assertEquals("0", peer.command("field"))
                peer.command("time 120000")
                assertNotEquals("0", peer.command("proof $expired 0"))
                val clock = CompanionProtocol.decodeTlvs(CompanionProtocol.optionalClockSample(1800000000123, 28800)).single().value
                assertEquals("1800000002623 28800", peer.command("clock ${CompanionProtocol.hex(clock)} 1 2500"))
                assertEquals("rejected", peer.command("clock ${CompanionProtocol.hex(clock)} 0 2500"))
                assertEquals("rejected", peer.command("clock ${CompanionProtocol.hex(clock)} 1 30001"))
            }
            Peer(root).use { assertEquals(CompanionProtocol.hex(identity), it.command("identity")) }
        } finally { root.deleteRecursively() }
    }

    @Test(timeout = 120000) fun readingReplaySurvivesEveryFragmentAndAckCutInBothDirections() {
        val root = createTempDirectory("note4-replay-e2e-").toFile()
        val reading = ReaderProgress("a".repeat(59) + ".txt", 10000, 0, 256, false, 120)
        val first = ReaderProgressCodec.encode(reading)!!
        val replacement = ReaderProgressCodec.encode(reading.copy(offset = 512, perMille = 250))!!
        try {
            for (capacity in listOf(20, 182)) {
                val dataCount = CompanionFragments.encode(SyncWire.encode(DurableEntry(257, 1, first), 2), 1, capacity).size
                val ackCount = CompanionFragments.encode(SyncWire.encode(DurableEntry(257, 1, first), 2, 0), 1, capacity).size
                for (fromPhone in listOf(true, false)) for (cut in 0..dataCount + ackCount) {
                    val directory = File(root, "$capacity-$fromPhone-$cut").apply { mkdirs() }
                    val phoneFile = File(directory, "phone.bin")
                    val deviceDirectory = File(directory, "device")
                    var queue = DurableQueue(phoneFile).also { assertTrue(it.load()) }
                    Peer(deviceDirectory).use { peer ->
                        if (fromPhone) assertTrue(queue.put(DurableEntry(257, 1, first)))
                        else assertEquals("0", peer.command("put 257 1 ${CompanionProtocol.hex(first)}"))
                        val phone = start(peer, queue)
                        val receiver = CompanionFragments.Reassembler()
                        val data = if (fromPhone) {
                            var frame: ByteArray? = null
                            phone.poll(0) { frame = it; true }
                            CompanionFragments.encode(requireNotNull(frame), 1, capacity)
                        } else peerPackets(peer, 0, capacity)
                        assertEquals(dataCount, data.size)
                        data.take(cut).forEach {
                            if (fromPhone) peer.command("packet ${CompanionProtocol.hex(it)}") else receive(phone, receiver, it)
                        }
                        if (cut >= dataCount) {
                            val ack = if (fromPhone) peerPackets(peer, 0, capacity) else {
                                var frame: ByteArray? = null
                                phone.poll(0) { frame = it; true }
                                CompanionFragments.encode(requireNotNull(frame), 2, capacity)
                            }
                            assertEquals(ackCount, ack.size)
                            ack.take(cut - dataCount).forEach {
                                if (fromPhone) receive(phone, receiver, it) else peer.command("packet ${CompanionProtocol.hex(it)}")
                            }
                        }
                    }
                    // Both volatile sessions die. Only their committed files survive.
                    queue = DurableQueue(phoneFile).also { assertTrue(it.load()) }
                    Peer(deviceDirectory).use { peer ->
                        if (fromPhone) assertTrue(queue.put(DurableEntry(257, 2, replacement)))
                        else assertEquals("0", peer.command("put 257 2 ${CompanionProtocol.hex(replacement)}"))
                        converge(peer, queue, capacity)
                        if (fromPhone) assertEquals("2 ${CompanionProtocol.hex(replacement)}", peer.command("read 257"))
                        else assertArrayEquals(replacement, queue.incoming(257)!!.payload)
                        assertTrue(queue.snapshot().isEmpty())
                    }
                }
            }
        } finally { root.deleteRecursively() }
    }

    @Test(timeout = 60000) fun weatherAndFailedFirmwareSaveUseTheRealDurableAckPath() {
        val root = createTempDirectory("note4-weather-e2e-").toFile()
        try {
            val file = File(root, "phone.bin")
            var queue = DurableQueue(file).also { assertTrue(it.load()) }
            val weather = WeatherSnapshot("上海", -5, 61, 1800000000, 1800021600)
            val bytes = WeatherSync.encode(weather)
            assertTrue(queue.enqueue(WeatherSync.KEY, bytes))
            Peer(File(root, "peer")).use { peer ->
                val phone = start(peer, queue)
                peer.command("fail-save")
                phone.poll(0) { frame ->
                    CompanionFragments.encode(frame, 1, 20).forEach { peer.command("packet ${CompanionProtocol.hex(it)}") }
                    true
                }
                val reassembly = CompanionFragments.Reassembler()
                peerPackets(peer, 0, 20).forEach { receive(phone, reassembly, it) }
                assertEquals(SyncSessionStatus.STORE_ERROR, phone.status)
                assertEquals("missing", peer.command("read ${WeatherSync.KEY}"))
            }
            queue = DurableQueue(file).also { assertTrue(it.load()) }
            Peer(File(root, "peer")).use { peer ->
                converge(peer, queue, 20)
                assertEquals("1 ${CompanionProtocol.hex(bytes)}", peer.command("read ${WeatherSync.KEY}"))
                assertEquals("-5 61 上海", peer.command("weather ${CompanionProtocol.hex(bytes)} 1800000010"))
                assertEquals("rejected", peer.command("weather ${CompanionProtocol.hex(bytes)} 1800021600"))
                assertEquals("rejected", peer.command("weather ${CompanionProtocol.hex(bytes.copyOf().apply { this[13] = 0xff.toByte() })} 1800000010"))
            }
        } finally { root.deleteRecursively() }
    }
}
