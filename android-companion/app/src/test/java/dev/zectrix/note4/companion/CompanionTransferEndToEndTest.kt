package dev.zectrix.note4.companion

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.io.IOException
import java.net.ServerSocket
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlin.io.path.createTempDirectory

class CompanionTransferEndToEndTest {
    @Test(timeout = 60000) fun phoneStreamsBooksAndPicturesToProductionFirmwareHttpStorage() {
        val root = createTempDirectory("note4-transfer-e2e-").toFile()
        val books = File(root, "books").apply { mkdirs() }
        val server = ProcessBuilder(requireNotNull(System.getProperty("zectrix.web")), books.path)
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        try {
            val ready = requireNotNull(server.inputStream.bufferedReader().readLine())
            val match = Regex("READY (http://127.0.0.1:[0-9]+/) code=([A-Z2-9]+)").matchEntire(ready)!!
            val endpoint = TransferEndpoint.parse(match.groupValues[1], allowLoopback = true)
            val code = match.groupValues[2]
            Note4TransferClient(endpoint, "ZZZZZZZZZZZZ").use { denied ->
                assertEquals(401, assertThrows(TransferFailure::class.java) { denied.library() }.status)
            }
            Note4TransferClient(endpoint, code).use { client ->
                assertTrue(client.library().coverSupported)
                val text = File(root, "源文件.txt").apply { writeText("跨端流式传书。\n".repeat(7000)) }
                client.uploadBook(text, "中文阅读.txt")
                assertArrayEquals(text.readBytes(), File(books, "中文阅读.txt").readBytes())
                val original = File(books, "中文阅读.txt").readBytes()
                assertEquals(409, assertThrows(TransferFailure::class.java) { client.uploadBook(text, "中文阅读.txt") }.status)
                assertArrayEquals(original, File(books, "中文阅读.txt").readBytes())
                val cover = File(root, "picture.pbm").apply {
                    writeBytes(MonochromeCover.encode(IntArray(120000) { if (it % 400 < 200) -1 else 0xff000000.toInt() }))
                }
                client.uploadCover(cover)
                assertArrayEquals(cover.readBytes(), File(books, ".cover-phone.pbm").readBytes())
                assertEquals(409, assertThrows(TransferFailure::class.java) { client.uploadCover(cover) }.status)
                assertTrue(client.library().books.none { it.name.endsWith(".pbm") })
                client.removeCover()
                assertFalse(File(books, ".cover-phone.pbm").exists())
                client.uploadCover(cover)
                repeat(65) { File(books, "A%03d.txt".format(it)).writeText("A book") }
                var after: String? = null
                val listed = mutableListOf<String>()
                do {
                    val page = client.library(after)
                    listed += page.books.map { it.name }
                    after = if (page.more) page.books.last().name else null
                } while (after != null)
                assertEquals(66, listed.size)
                assertEquals(66, listed.distinct().size)
                val interrupted = File(root, "interrupted.txt").apply { writeBytes(ByteArray(256000) { 65 }) }
                Note4TransferClient(endpoint, code).use { cancelled ->
                    assertThrows(IOException::class.java) {
                        cancelled.uploadBook(interrupted, "interrupted.txt") { _, _ -> cancelled.close() }
                    }
                }
                // A new request drains the interrupted server request before taking its lease.
                client.library()
                assertFalse(File(books, "interrupted.txt").exists())
                assertFalse(File(books, ".upload.part").exists())
                client.uploadBook(interrupted, "interrupted.txt")
                assertArrayEquals(interrupted.readBytes(), File(books, "interrupted.txt").readBytes())
                client.finish()
            }
            assertTrue(server.waitFor(5, TimeUnit.SECONDS))
            assertEquals(0, server.exitValue())
        } finally {
            if (server.isAlive) { server.destroy(); if (!server.waitFor(5, TimeUnit.SECONDS)) server.destroyForcibly() }
            root.deleteRecursively()
        }
    }

    @Test(timeout = 10000) fun redirectsCannotForwardTheScreenCodeToAnotherHost() {
        ServerSocket(0).use { server ->
            var requests = 0
            val worker = thread {
                server.accept().use { socket ->
                    val input = socket.getInputStream().bufferedReader()
                    while (!input.readLine().isNullOrEmpty()) { }
                    requests++
                    socket.getOutputStream().write(("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:${server.localPort}/elsewhere\r\n" +
                        "Content-Length: 0\r\nConnection: close\r\n\r\n").toByteArray())
                }
            }
            val endpoint = TransferEndpoint.parse("http://127.0.0.1:${server.localPort}", allowLoopback = true)
            Note4TransferClient(endpoint, "ABCDEFGH2345").use {
                assertEquals(302, assertThrows(TransferFailure::class.java) { it.library() }.status)
            }
            worker.join(2000)
            assertFalse(worker.isAlive)
            assertEquals(1, requests)
        }
    }
}
