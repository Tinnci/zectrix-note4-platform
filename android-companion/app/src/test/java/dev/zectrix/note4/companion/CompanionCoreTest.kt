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

    @Test fun enrollmentProofTlvMatchesFirmwareValue() {
        val token = ByteArray(16) { (it + 1).toByte() }
        val companionId = ByteArray(16) { (0xa0 + it).toByte() }
        val value = CompanionProtocol.encodeHelloEnrollmentProof(0x89abcdef, token, companionId)
        assertEquals(
            "efcdab890102030405060708090a0b0c0d0e0f10a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
            CompanionProtocol.hex(value),
        )
        val (generation, decodedToken, decodedCompanionId) =
            CompanionProtocol.decodeHelloEnrollmentProof(value)!!
        assertEquals(0x89abcdef, generation)
        assertArrayEquals(token, decodedToken)
        assertArrayEquals(companionId, decodedCompanionId)
        assertNull(CompanionProtocol.decodeHelloEnrollmentProof(ByteArray(35)))

        val tlv = CompanionProtocol.encodeTlv(
            CompanionProtocol.HELLO_ENROLLMENT_PROOF_TYPE, required = true, value = value,
        )
        val fields = CompanionProtocol.decodeTlvs(tlv)
        assertEquals(1, fields.size)
        assertTrue(fields[0].required)
        assertEquals(CompanionProtocol.HELLO_ENROLLMENT_PROOF_TYPE, fields[0].type)
        assertArrayEquals(value, fields[0].value)

        val identity = CompanionProtocol.encodeCompanionIdentity(companionId)
        assertArrayEquals(companionId, CompanionProtocol.decodeCompanionIdentity(identity))
        assertNull(CompanionProtocol.decodeCompanionIdentity(ByteArray(15)))

        val ack = CompanionProtocol.encodeHelloAckStatus(
            CompanionProtocol.HELLO_ACK_STATUS_OK, peerAuthorized = true, errorReason = 0,
        )
        assertEquals("00010000", CompanionProtocol.hex(ack))
        val decodedAck = CompanionProtocol.decodeHelloAckStatus(ack)!!
        assertEquals(CompanionProtocol.HELLO_ACK_STATUS_OK, decodedAck.status)
        assertTrue(decodedAck.peerAuthorized)
        assertEquals(0, decodedAck.errorReason)
        assertNull(CompanionProtocol.decodeHelloAckStatus(ByteArray(3)))
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

    @Test fun nfcEnrollmentParserValidatesPayload() {
        val payload = ByteArray(NfcEnrollmentParser.PAYLOAD_SIZE)
        "ZEN1".toByteArray(Charsets.US_ASCII).copyInto(payload, 0)
        payload[4] = 1
        payload[6] = 1
        payload[7] = 0
        val token = ByteArray(16) { (it + 1).toByte() }
        token.copyInto(payload, 34)
        put32(payload, 30, 0x12345678)
        val record = NfcEnrollmentParser.parsePayload(payload)!!
        assertEquals(1, record.version)
        assertEquals(0x12345678, record.generation)
        assertArrayEquals(token, record.token)
        assertNull(NfcEnrollmentParser.parsePayload(payload.copyOf(49)))
        val zeroToken = payload.copyOf()
        zeroToken.fill(0, 34, 50)
        assertNull(NfcEnrollmentParser.parsePayload(zeroToken))
        val unknownFlags = payload.copyOf().apply { this[5] = 2 }
        assertNull(NfcEnrollmentParser.parsePayload(unknownFlags))
        val invalidAddressType = payload.copyOf().apply { this[7] = 2 }
        assertNull(NfcEnrollmentParser.parsePayload(invalidAddressType))
        val emptyValidAddress = payload.copyOf().apply { this[5] = 1 }
        assertNull(NfcEnrollmentParser.parsePayload(emptyValidAddress))
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

    private fun put32(output: ByteArray, offset: Int, value: Long) {
        repeat(4) { output[offset + it] = (value ushr (it * 8)).toByte() }
    }

    private fun assertFails(block: () -> Unit) {
        try {
            block()
            fail("expected failure")
        } catch (_: IllegalArgumentException) {
        }
    }
}
