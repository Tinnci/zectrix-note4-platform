package dev.zectrix.note4.companion

/** Fixed PBM raster, with row-bounded Floyd-Steinberg error diffusion. */
object MonochromeCover {
    const val WIDTH = 400
    const val HEIGHT = 300
    val HEADER = "P4\n400 300\n".toByteArray(Charsets.US_ASCII)
    val FILE_SIZE = HEADER.size + WIDTH * HEIGHT / 8

    fun encode(argb: IntArray): ByteArray {
        require(argb.size == WIDTH * HEIGHT)
        val output = ByteArray(FILE_SIZE)
        HEADER.copyInto(output)
        var current = IntArray(WIDTH + 2)
        var next = IntArray(WIDTH + 2)
        for (y in 0 until HEIGHT) {
            for (x in 0 until WIDTH) {
                val pixel = argb[y * WIDTH + x]
                val alpha = pixel ushr 24
                val luminance = (((pixel ushr 16 and 255) * 299 + (pixel ushr 8 and 255) * 587 +
                    (pixel and 255) * 114) / 1000 * alpha + 255 * (255 - alpha)) / 255
                val value = (luminance * 16 + current[x + 1]).coerceIn(0, 255 * 16)
                val black = value < 128 * 16
                if (black) {
                    val at = HEADER.size + y * (WIDTH / 8) + x / 8
                    output[at] = (output[at].toInt() or (0x80 ushr (x % 8))).toByte()
                }
                val error = value - if (black) 0 else 255 * 16
                current[x + 2] += error * 7 / 16
                next[x] += error * 3 / 16
                next[x + 1] += error * 5 / 16
                next[x + 2] += error / 16
            }
            val previous = current
            current = next
            next = previous.apply { fill(0) }
        }
        return output
    }
}
