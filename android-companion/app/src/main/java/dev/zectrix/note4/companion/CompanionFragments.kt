package dev.zectrix.note4.companion

object CompanionFragments {
    const val HEADER_SIZE = 8
    const val MAX_FRAME_SIZE = CompanionProtocol.HEADER_SIZE + CompanionProtocol.MAX_PAYLOAD_SIZE
    private const val MAGIC = 0xa7
    private const val VERSION = 1
    private const val START = 1
    private const val END = 2

    fun encode(frame: ByteArray, frameId: Int, packetCapacity: Int): List<ByteArray> {
        require(frame.isNotEmpty() && frame.size <= MAX_FRAME_SIZE)
        require(frameId in 1..0xffff && packetCapacity > HEADER_SIZE)
        val dataCapacity = packetCapacity - HEADER_SIZE
        return frame.asList().chunked(dataCapacity).mapIndexed { index, bytes ->
            val offset = index * dataCapacity
            ByteArray(HEADER_SIZE + bytes.size).also { packet ->
                packet[0] = MAGIC.toByte()
                packet[1] = VERSION.toByte()
                packet[2] = ((if (index == 0) START else 0) or
                    (if (offset + bytes.size == frame.size) END else 0)).toByte()
                put16(packet, 4, frameId)
                put16(packet, 6, offset)
                bytes.forEachIndexed { byteIndex, value ->
                    packet[HEADER_SIZE + byteIndex] = value
                }
            }
        }
    }

    class Reassembler {
        private var frameId = 0
        private val bytes = ArrayList<Byte>()

        fun accept(packet: ByteArray): ByteArray? {
            if (packet.size <= HEADER_SIZE || u8(packet[0]) != MAGIC ||
                u8(packet[1]) != VERSION || u8(packet[3]) != 0) {
                reset()
                return null
            }
            val flags = u8(packet[2])
            if (flags and (START or END).inv() != 0) {
                reset()
                return null
            }
            val incomingId = get16(packet, 4)
            val offset = get16(packet, 6)
            if (flags and START != 0) {
                if (offset != 0) {
                    reset()
                    return null
                }
                frameId = incomingId
                bytes.clear()
            } else if (frameId == 0 || incomingId != frameId || offset != bytes.size) {
                reset()
                return null
            }
            if (bytes.size + packet.size - HEADER_SIZE > MAX_FRAME_SIZE) {
                reset()
                return null
            }
            packet.copyOfRange(HEADER_SIZE, packet.size).forEach(bytes::add)
            if (flags and END == 0) return null
            val result = bytes.toByteArray()
            reset()
            return result
        }

        fun reset() {
            frameId = 0
            bytes.clear()
        }
    }

    private fun u8(value: Byte): Int = value.toInt() and 0xff
    private fun put16(output: ByteArray, offset: Int, value: Int) {
        output[offset] = value.toByte()
        output[offset + 1] = (value ushr 8).toByte()
    }
    private fun get16(input: ByteArray, offset: Int): Int =
        u8(input[offset]) or (u8(input[offset + 1]) shl 8)
}
