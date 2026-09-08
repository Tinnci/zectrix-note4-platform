package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

class CompanionSyncTest {
    private class MemoryStorage : DurableStorage {
        var bytes: ByteArray? = null
        var fail = false
        var saves = 0
        override fun read() = bytes?.copyOf()
        override fun write(bytes: ByteArray): Boolean {
            saves++
            if (fail) { fail = false; return false }
            this.bytes = bytes.copyOf()
            return true
        }
    }

    private fun queue(store: MemoryStorage) = DurableQueue(store).apply { assertTrue(load()) }
    private fun frame(bytes: ByteArray) = (CompanionProtocol.decode(bytes) as CompanionProtocol.DecodeResult.Success).frame
    private fun send(session: CompanionSyncSession, now: Long = 0): ByteArray? {
        var bytes: ByteArray? = null
        session.poll(now) { bytes = it.copyOf(); true }
        return bytes
    }
    private fun transfer(from: CompanionSyncSession, to: CompanionSyncSession, now: Long = 0) {
        send(from, now)?.let { assertTrue(to.receive(frame(it))) }
    }

    @Test fun lostAckAndProcessRestartConvergeInBothDirections() {
        val aStore = MemoryStorage()
        val bStore = MemoryStorage()
        var a = queue(aStore)
        var b = queue(bStore)
        assertTrue(a.put(DurableEntry(1, 3, byteArrayOf(7))))
        assertTrue(b.put(DurableEntry(2, 9, byteArrayOf(8))))
        val ac = a.cursors()
        val bc = b.cursors()
        var aSession = CompanionSyncSession(a)
        var bSession = CompanionSyncSession(b)
        assertEquals(DurableResult.OK, aSession.start(bc, 0))
        assertEquals(DurableResult.OK, bSession.start(ac, 0))
        transfer(aSession, bSession)
        assertEquals(3L, b.incoming(1)!!.revision)
        aSession.disconnect()
        bSession.disconnect()
        assertEquals(1, a.snapshot().size)
        a = queue(aStore)
        b = queue(bStore)
        val restartedAc = a.cursors()
        val restartedBc = b.cursors()
        aSession = CompanionSyncSession(a)
        bSession = CompanionSyncSession(b)
        assertEquals(DurableResult.OK, aSession.start(restartedBc, 0))
        assertEquals(DurableResult.OK, bSession.start(restartedAc, 0))
        assertTrue(a.snapshot().isEmpty())
        assertFalse(aSession.converged())
        repeat(4) { transfer(bSession, aSession); transfer(aSession, bSession) }
        assertTrue(aSession.converged() && bSession.converged())
        assertEquals(9L, queue(aStore).incoming(2)!!.revision)
    }

    @Test fun retriesKeepTheOriginalValueAndOldAcksCannotRemoveItsReplacement() {
        val a = queue(MemoryStorage())
        val bStore = MemoryStorage()
        val b = queue(bStore)
        assertTrue(a.put(DurableEntry(1, 1, byteArrayOf(1))))
        val ac = a.cursors()
        val asession = CompanionSyncSession(a)
        val bsession = CompanionSyncSession(b)
        assertEquals(DurableResult.OK, asession.start(b.cursors(), 0))
        assertEquals(DurableResult.OK, bsession.start(ac, 0))
        val original = send(asession)!!
        assertTrue(a.put(DurableEntry(1, 2, byteArrayOf(2))))
        assertArrayEquals(original, send(asession, 3000))
        bsession.receive(frame(original))
        val saves = bStore.saves
        bsession.receive(frame(original))
        assertEquals(saves, bStore.saves)
        val oldAck = send(bsession, 3000)!!
        asession.receive(frame(oldAck))
        assertEquals(2L, a.snapshot().single().revision)
        val next = send(asession, 3000)!!
        asession.receive(frame(oldAck))
        assertEquals(2L, a.snapshot().single().revision)
        bsession.receive(frame(next))
        asession.receive(frame(send(bsession, 3000)!!))
        assertTrue(asession.converged())
        assertArrayEquals(byteArrayOf(2), b.incoming(1)!!.payload)
        send(asession, 3000)
        assertTrue(a.put(DurableEntry(1, 3, byteArrayOf(3))))
        assertNotNull(send(asession, 100000))
        assertEquals(SyncSessionStatus.ACTIVE, asession.status)
    }

    @Test fun saveFailureDoesNotAckAndTimeoutKeepsTheOutbox() {
        val aStore = MemoryStorage()
        val bStore = MemoryStorage()
        val a = queue(aStore)
        val b = queue(bStore)
        assertTrue(a.put(DurableEntry(1, 1, byteArrayOf(1))))
        val ac = a.cursors()
        val asession = CompanionSyncSession(a)
        val bsession = CompanionSyncSession(b)
        asession.start(b.cursors(), 0)
        bsession.start(ac, 0)
        val original = send(asession)!!
        assertNotNull(send(asession, 3000))
        assertNotNull(send(asession, 6000))
        assertNull(send(asession, 9000))
        assertEquals(SyncSessionStatus.TIMEOUT, asession.status)
        assertEquals(1, queue(aStore).snapshot().size)
        asession.start(b.cursors(), 10000)
        val retried = send(asession, 10000)!!
        assertArrayEquals(original, retried)
        bStore.fail = true
        bsession.receive(frame(retried))
        assertNull(b.incoming(1))
        val nack = send(bsession, 10000)!!
        assertEquals(SyncSessionStatus.STORE_ERROR, bsession.status)
        asession.receive(frame(nack))
        assertEquals(SyncSessionStatus.STORE_ERROR, asession.status)
        assertEquals(1, a.snapshot().size)
    }

    @Test fun cursorReconciliationIsTransactionalAndRejectsRegression() {
        val store = MemoryStorage()
        val q = queue(store)
        for (key in 1..10) {
            store.fail = true
            assertEquals(DurableResult.STORE_ERROR, q.accept(DurableEntry(key, 1, byteArrayOf(0))))
            assertTrue(q.cursors().isEmpty())
        }
        assertTrue(q.put(DurableEntry(1, 3, byteArrayOf(0))))
        val before = store.bytes!!.copyOf()
        store.fail = true
        assertEquals(DurableResult.STORE_ERROR, q.reconcile(listOf(SyncCursor(1, 0, 3, 0))))
        assertArrayEquals(before, store.bytes)
        assertEquals(1, q.snapshot().size)
        assertEquals(DurableResult.OK, q.reconcile(listOf(SyncCursor(1, 0, 3, 0))))
        assertEquals(DurableResult.RESYNC_REQUIRED, q.reconcile(listOf(SyncCursor(1, 0, 2, 0))))
        assertEquals(DurableResult.RESYNC_REQUIRED, q.reconcile(listOf(SyncCursor(1, 0, 4, 0))))
    }

    @Test fun cursorBytesMatchFirmwareAndHelloAckValidationIsStrict() {
        val cursors = listOf(SyncCursor(0x1234, 0x01020304, 0x05060708, 0x090a0b0c))
        val expected = CompanionProtocol.unhex("01010000341204030201080706050c0b0a09")
        assertArrayEquals(expected, SyncWire.encodeCursors(cursors))
        assertEquals(cursors, SyncWire.decodeCursors(expected))
        assertFalse(SyncWire.validCursors(cursors + cursors))
        assertTrue(runCatching { SyncWire.decodeCursors(expected.copyOf(expected.size - 1)) }.isFailure)
        assertTrue(runCatching { SyncWire.decodeCursors(expected.copyOf().also { it[2] = 1 }) }.isFailure)
        val status = CompanionProtocol.encodeTlv(3, true, byteArrayOf(0, 1, 0, 0))
        val wire = CompanionProtocol.encodeTlv(4, true, SyncWire.encodeCursors(cursors))
        assertEquals(cursors, SyncWire.decodeHelloAck(status + wire).second)
        assertTrue(runCatching { SyncWire.decodeHelloAck(status + status + wire) }.isFailure)
        assertNull(CompanionProtocol.decodeHelloAckStatus(byteArrayOf(2, 1, 0, 0)))
        assertNull(CompanionProtocol.decodeHelloAckStatus(byteArrayOf(0, 2, 0, 0)))
        assertNull(CompanionProtocol.decodeHelloAckStatus(byteArrayOf(1, 1, 0, 0)))
    }

    @Test fun conflictingSequenceCannotOverwriteReceivedState() {
        val store = MemoryStorage()
        val q = queue(store)
        val session = CompanionSyncSession(q)
        assertEquals(DurableResult.OK, session.start(listOf(SyncCursor(1, 0, 0, 1)), 0))
        session.receive(frame(SyncWire.encode(DurableEntry(1, 1, byteArrayOf(1)), 2)))
        session.receive(frame(SyncWire.encode(DurableEntry(1, 2, byteArrayOf(2)), 2)))
        assertEquals(SyncSessionStatus.PROTOCOL_ERROR, session.status)
        assertArrayEquals(byteArrayOf(1), queue(store).incoming(1)!!.payload)
    }

    @Test fun maximumBidirectionalStateSurvivesReload() {
        val store = MemoryStorage()
        val q = queue(store)
        val value = ByteArray(DurableQueue.VALUE_CAPACITY) { it.toByte() }
        for (key in 1..DurableQueue.CAPACITY) {
            assertTrue(q.put(DurableEntry(key, 1, value)))
            assertEquals(DurableResult.OK, q.accept(DurableEntry(key, 2, value)))
        }
        assertEquals(DurableQueue.MAX_STORE_SIZE, store.bytes!!.size)
        val restarted = queue(store)
        assertEquals(DurableQueue.CAPACITY, restarted.snapshot().size)
        assertArrayEquals(value, restarted.incoming(8)!!.payload)
        assertEquals(DurableResult.CAPACITY, restarted.accept(DurableEntry(9, 1, value)))
    }

    @Test fun legacyQueueLoadsButCorruptionNeverPartiallyLoads() {
        val store = MemoryStorage()
        val legacy = ByteBuffer.allocate(23).order(ByteOrder.LITTLE_ENDIAN)
        legacy.putInt(0x51445a43).putShort(1).putShort(1)
            .putShort(2).putInt(3).putShort(1).putShort(0).put(9)
        legacy.putInt(CRC32().apply { update(legacy.array(), 0, 19) }.value.toInt())
        store.bytes = legacy.array()
        val q = queue(store)
        assertEquals(3L, q.snapshot().single().revision)
        assertTrue(q.put(DurableEntry(3, 1, byteArrayOf(4))))
        val good = store.bytes!!.copyOf()
        store.bytes = good.copyOf().also {
            it[6] = 1
            ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN).putInt(it.size - 4,
                CRC32().apply { update(it, 0, it.size - 4) }.value.toInt())
        }
        assertFalse(q.load())
        assertTrue(q.snapshot().isEmpty() && q.cursors().isEmpty())
        assertFalse(q.put(DurableEntry(4, 1, byteArrayOf(1))))
        store.bytes = ByteArray(DurableQueue.MAX_STORE_SIZE + 1)
        assertFalse(q.load())
        store.bytes = good
        assertTrue(q.load())
        assertEquals(2, q.snapshot().size)
    }
}
