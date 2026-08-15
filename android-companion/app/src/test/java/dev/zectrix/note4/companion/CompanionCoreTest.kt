package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import kotlin.io.path.createTempDirectory

class CompanionCoreTest {
    @Test
    fun fragmentsMatchTheCppGoldenVectorAndReassemble() {
        val frame = CompanionProtocol.unhex(
            "5a4301000205020144332211887766550e0000005faebb010180040078563412020002006f6b",
        )
        val expected = listOf(
            "a70101002a0000005a4301000205020144332211887766",
            "a70100002a000f00550e0000005faebb01018004007856",
            "a70102002a001e003412020002006f6b",
        )
        val fragments = CompanionFragments.encode(frame, 42, 23)
        assertEquals(expected, fragments.map(CompanionProtocol::hex))
        val reassembler = CompanionFragments.Reassembler()
        assertNull(reassembler.accept(fragments[0]))
        assertNull(reassembler.accept(fragments[1]))
        assertArrayEquals(frame, reassembler.accept(fragments[2]))
    }

    private val frameHex = "5a4301000205020144332211887766550e0000005faebb010180040078563412020002006f6b"

    @Test fun goldenVectorMatchesFirmware() {
        val payload = CompanionProtocol.unhex("0180040078563412020002006f6b")
        val frame = CompanionProtocol.encode(
            CompanionProtocol.Header(
                messageClass = CompanionProtocol.MessageClass.COMMAND,
                flags = 5,
                messageType = 258,
                requestId = 0x11223344,
                sequence = 0x55667788,
            ), payload,
        )
        assertEquals(frameHex, CompanionProtocol.hex(frame))
        val result = CompanionProtocol.decode(frame)
        assertTrue(result is CompanionProtocol.DecodeResult.Success)
        val decoded = (result as CompanionProtocol.DecodeResult.Success).frame
        assertEquals(258, decoded.header.messageType)
        assertEquals(2, CompanionProtocol.decodeTlvs(decoded.payload).size)
    }

    @Test fun mutationAndBoundsAreRejected() {
        val frame = CompanionProtocol.unhex(frameHex)
        for (index in frame.indices) {
            val corrupt = frame.copyOf().also { it[index] = (it[index].toInt() xor 1).toByte() }
            assertTrue("byte $index", CompanionProtocol.decode(corrupt) is CompanionProtocol.DecodeResult.Error)
        }
        assertTrue(CompanionProtocol.decode(frame.copyOf(frame.size - 1)) is CompanionProtocol.DecodeResult.Error)
        assertFails { CompanionProtocol.decodeTlvs(byteArrayOf(1, 0, 4, 0, 1)) }
    }

    @Test fun helloAndHelloAckHaveDistinctSessionSemantics() {
        val hello = CompanionProtocol.encode(
            CompanionProtocol.Header(
                messageClass = CompanionProtocol.MessageClass.CONTROL,
                flags = 0,
                messageType = CompanionProtocol.CONTROL_HELLO,
                requestId = 7,
                sequence = 1,
            ), byteArrayOf(),
        )
        val decoded = CompanionProtocol.decode(hello) as CompanionProtocol.DecodeResult.Success
        assertEquals(CompanionProtocol.CONTROL_HELLO, decoded.frame.header.messageType)
        assertEquals(0, decoded.frame.header.flags)

        val ack = CompanionProtocol.encode(
            decoded.frame.header.copy(
                flags = CompanionProtocol.FLAG_RESPONSE,
                messageType = CompanionProtocol.CONTROL_HELLO_ACK,
            ), byteArrayOf(),
        )
        val decodedAck = CompanionProtocol.decode(ack) as CompanionProtocol.DecodeResult.Success
        assertEquals(7, decodedAck.frame.header.requestId)
        assertEquals(1, decodedAck.frame.header.sequence)
        assertEquals(CompanionProtocol.FLAG_RESPONSE, decodedAck.frame.header.flags)
        assertTrue(CompanionProtocol.matchesHelloAck(decodedAck.frame, 7, 1))
        assertFalse(CompanionProtocol.matchesHelloAck(decodedAck.frame, 7, 2))
        assertFalse(CompanionProtocol.matchesHelloAck(decodedAck.frame, 8, 1))
    }

    @Test fun durableQueueSurvivesRestartAndRejectsCorruption() {
        val directory = createTempDirectory("zectrix-companion-").toFile()
        try {
            val file = File(directory, "queue.bin")
            val queue = DurableQueue(file)
            assertTrue(queue.load())
            assertTrue(queue.put(DurableEntry(1, 1, byteArrayOf(1, 2))))
            assertFalse(queue.put(DurableEntry(1, 1, byteArrayOf(3))))
            val restarted = DurableQueue(file)
            assertTrue(restarted.load())
            assertArrayEquals(byteArrayOf(1, 2), restarted.snapshot().single().payload)
            assertTrue(restarted.acknowledge(1, 1))
            assertTrue(DurableQueue(file).apply { assertTrue(load()) }.snapshot().isEmpty())
            file.writeBytes(byteArrayOf(1, 2, 3))
            assertFalse(DurableQueue(file).load())
        } finally {
            directory.deleteRecursively()
        }
    }

    @Test fun lifecycleRequiresOrderedNegotiationAndRecoversFromLoss() {
        val lifecycle = ConnectionStateMachine()
        assertFalse(lifecycle.accept(ConnectionEvent.CONNECT))
        assertTrue(lifecycle.accept(ConnectionEvent.ASSOCIATED))
        assertTrue(lifecycle.accept(ConnectionEvent.APPEARED))
        assertTrue(lifecycle.accept(ConnectionEvent.CONNECT))
        assertTrue(lifecycle.accept(ConnectionEvent.GATT_CONNECTED))
        assertTrue(lifecycle.accept(ConnectionEvent.SERVICES_READY))
        assertTrue(lifecycle.accept(ConnectionEvent.NEGOTIATED))
        assertEquals(ConnectionState.READY, lifecycle.state)
        assertTrue(lifecycle.accept(ConnectionEvent.LOST))
        assertTrue(lifecycle.accept(ConnectionEvent.RETRY))
        assertEquals(ConnectionState.CONNECTING, lifecycle.state)
    }

    private fun assertFails(block: () -> Unit) {
        try {
            block()
            fail("expected failure")
        } catch (_: IllegalArgumentException) {
        }
    }
}
