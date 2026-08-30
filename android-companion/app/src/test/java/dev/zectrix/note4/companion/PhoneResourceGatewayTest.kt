package dev.zectrix.note4.companion

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.util.ArrayDeque
import java.util.concurrent.Executor

class PhoneResourceGatewayTest {
    private class FakeClient(vararg results: ResourceFetchResult) : ResourceHttpClient {
        val results = ArrayDeque(results.toList())
        var calls = 0
        override fun fetch(request: ResourceGatewayProtocol.Request): ResourceFetchResult {
            calls++
            return results.removeFirst()
        }
    }

    private val directExecutor = Executor { it.run() }

    @Test fun requestCodecMatchesFirmwareVector() {
        val payload = ResourceGatewayProtocol.encodeRequest(defaultRequest())
        assertEquals(
            "018002000100028002000008038004001027000004000400000000000580010001",
            CompanionProtocol.hex(payload),
        )
        assertEquals(defaultRequest(), ResourceGatewayProtocol.decodeRequest(payload))
        val frame = CompanionProtocol.encode(
            CompanionProtocol.Header(
                messageClass = CompanionProtocol.MessageClass.COMMAND,
                flags = CompanionProtocol.FLAG_ACK_REQUESTED or
                    CompanionProtocol.FLAG_RETRIABLE,
                messageType = ResourceGatewayProtocol.MESSAGE_TYPE,
                requestId = 0x11223344,
                sequence = 1,
            ),
            payload,
        )
        assertEquals(
            "5a43010002050001443322110100000021000000dc60172c" +
                "018002000100028002000008038004001027000004000400000000000580010001",
            CompanionProtocol.hex(frame),
        )

        val response = ResourceGatewayProtocol.Response(
            ResourceGatewayProtocol.Status.SUCCESS,
            ResourceGatewayProtocol.ContentType.TEXT_PLAIN_UTF8,
            "ok\n".toByteArray(),
        )
        val encoded = ResourceGatewayProtocol.encodeResponse(response)
        assertEquals("01800100000280010001038003006f6b0a", CompanionProtocol.hex(encoded))
        val decoded = ResourceGatewayProtocol.decodeResponse(encoded)!!
        assertEquals(response.status, decoded.status)
        assertArrayEquals(response.body, decoded.body)
    }

    @Test fun successIsBoundedAndDuplicateUsesStoredResult() {
        val client = FakeClient(ResourceFetchResult.Success("text/plain", "document".toByteArray()))
        val gateway = PhoneResourceGateway(client, directExecutor)
        val first = mutableListOf<ByteArray>()
        gateway.handle(requestFrame(7, 1), authorized = true, first::add)
        assertEquals(1, client.calls)
        assertEquals(ResourceGatewayProtocol.Status.SUCCESS, decodeResponse(first.single()).status)

        val duplicate = mutableListOf<ByteArray>()
        gateway.handle(requestFrame(7, 2), authorized = true, duplicate::add)
        assertEquals(1, client.calls)
        val decodedFrame = CompanionProtocol.decode(duplicate.single()) as
            CompanionProtocol.DecodeResult.Success
        assertEquals(2, decodedFrame.frame.header.sequence)
        assertEquals(ResourceGatewayProtocol.Status.SUCCESS,
            ResourceGatewayProtocol.decodeResponse(decodedFrame.frame.payload)!!.status)
    }

    @Test fun offlineResultCanRetryWithoutChangingRequestIdentity() {
        val client = FakeClient(
            ResourceFetchResult.Offline,
            ResourceFetchResult.Success("text/plain", "recovered".toByteArray()),
        )
        val gateway = PhoneResourceGateway(client, directExecutor)
        val responses = mutableListOf<ByteArray>()
        gateway.handle(requestFrame(9, 1), authorized = true, responses::add)
        assertEquals(ResourceGatewayProtocol.Status.PHONE_OFFLINE,
            decodeResponse(responses.removeFirst()).status)
        gateway.handle(requestFrame(9, 2), authorized = true, responses::add)
        assertEquals(2, client.calls)
        assertEquals(ResourceGatewayProtocol.Status.SUCCESS,
            decodeResponse(responses.single()).status)
    }

    @Test fun authorizationUnsupportedAndRequestIdCollisionDoNotFetch() {
        val client = FakeClient(ResourceFetchResult.Success("text/plain", byteArrayOf(1)))
        val gateway = PhoneResourceGateway(client, directExecutor)
        val responses = mutableListOf<ByteArray>()
        gateway.handle(requestFrame(1, 1), authorized = false, responses::add)
        assertEquals(ResourceGatewayProtocol.Status.NOT_AUTHORIZED,
            decodeResponse(responses.removeFirst()).status)

        gateway.handle(requestFrame(2, 1, capability = 99), authorized = true, responses::add)
        assertEquals(ResourceGatewayProtocol.Status.UNSUPPORTED_CAPABILITY,
            decodeResponse(responses.removeFirst()).status)

        gateway.handle(requestFrame(3, 1), authorized = true, responses::add)
        assertEquals(1, client.calls)
        gateway.handle(requestFrame(3, 2, maximumBytes = 1024), authorized = true, responses::add)
        assertEquals(ResourceGatewayProtocol.Status.INVALID_RESPONSE,
            decodeResponse(responses.last()).status)
        assertEquals(1, client.calls)
    }

    @Test fun bodyReaderRejectsOneByteBeyondBoundary() {
        assertEquals(2048, BoundedBodyReader.read(
            ByteArrayInputStream(ByteArray(2048)), 2048,
        )!!.size)
        assertNull(BoundedBodyReader.read(ByteArrayInputStream(ByteArray(2049)), 2048))
    }

    private fun defaultRequest(
        capability: Int = 1,
        maximumBytes: Int = 2048,
    ) = ResourceGatewayProtocol.Request(capability, maximumBytes, 10_000, 0, true)

    private fun requestFrame(
        requestId: Long,
        sequence: Long,
        capability: Int = 1,
        maximumBytes: Int = 2048,
    ): CompanionProtocol.Frame = CompanionProtocol.Frame(
        CompanionProtocol.Header(
            messageClass = CompanionProtocol.MessageClass.COMMAND,
            flags = CompanionProtocol.FLAG_ACK_REQUESTED or CompanionProtocol.FLAG_RETRIABLE,
            messageType = ResourceGatewayProtocol.MESSAGE_TYPE,
            requestId = requestId,
            sequence = sequence,
        ),
        ResourceGatewayProtocol.encodeRequest(defaultRequest(capability, maximumBytes)),
    )

    private fun decodeResponse(frame: ByteArray): ResourceGatewayProtocol.Response {
        val decoded = CompanionProtocol.decode(frame) as CompanionProtocol.DecodeResult.Success
        assertTrue(decoded.frame.header.flags and CompanionProtocol.FLAG_RESPONSE != 0)
        return ResourceGatewayProtocol.decodeResponse(decoded.frame.payload)!!
    }
}
