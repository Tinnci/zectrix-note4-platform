package dev.zectrix.note4.companion

import android.content.Intent
import android.content.pm.PackageManager
import android.nfc.NdefMessage
import android.nfc.NdefRecord
import android.nfc.NfcAdapter
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class CompanionDeviceTest {
    private val context get() = ApplicationProvider.getApplicationContext<android.app.Application>()
    private fun nfc(payload: ByteArray) = Intent(NfcAdapter.ACTION_NDEF_DISCOVERED).apply {
        type = NfcEnrollmentParser.MIME_TYPE
        setPackage(context.packageName)
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        putExtra(NfcAdapter.EXTRA_NDEF_MESSAGES, arrayOf(NdefMessage(arrayOf(NdefRecord.createMime(NfcEnrollmentParser.MIME_TYPE, payload)))))
    }

    @Test fun coldNdefLaunchResolvesAndDoesNotReplayAfterRecreation() {
        val intent = nfc(ByteArray(50))
        val resolved = context.packageManager.queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY)
        assertTrue(resolved.any { it.activityInfo.name == MainActivity::class.java.name })
        ActivityScenario.launch<MainActivity>(intent).use { scenario ->
            scenario.onActivity {
                assertFalse(it.intent.hasExtra(NfcAdapter.EXTRA_NDEF_MESSAGES))
                assertNull(CompanionConnectionManager.pendingTapAddress())
            }
            scenario.recreate()
            scenario.onActivity { assertFalse(it.intent.hasExtra(NfcAdapter.EXTRA_NDEF_MESSAGES)) }
        }
    }

    @Test fun warmNdefUsesExistingActivityAndRejectsAmbiguousRecords() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            var original: MainActivity? = null
            scenario.onActivity { original = it }
            context.startActivity(nfc(ByteArray(50)).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP))
            instrumentation.waitForIdleSync()
            scenario.onActivity { assertSame(original, it) }
            val record = NdefRecord.createMime(NfcEnrollmentParser.MIME_TYPE, ByteArray(50))
            val ambiguous = nfc(ByteArray(50)).apply {
                putExtra(NfcAdapter.EXTRA_NDEF_MESSAGES, arrayOf(NdefMessage(arrayOf(record, record))))
            }
            assertThrows(IllegalArgumentException::class.java) { NfcEnrollmentIntents.read(ambiguous) }
            assertFalse(ambiguous.hasExtra(NfcAdapter.EXTRA_NDEF_MESSAGES))
            assertNull(NfcEnrollmentIntents.read(Intent(Intent.ACTION_SEND).putExtras(nfc(ByteArray(50)))))
        }
    }

    @Test fun androidKeystoreAndAtomicQueueReallySurviveOwnerRecreation() {
        val first = CompanionIdentityStore(context)
        val identity = first.getOrCreate()
        assertTrue(first.isDurable)
        val reopened = CompanionIdentityStore(context)
        assertArrayEquals(identity, reopened.getOrCreate())
        assertTrue(reopened.isDurable)
        val file = File.createTempFile("qualification-", ".bin", context.noBackupFilesDir).apply { delete() }
        try {
            val queue = DurableQueue(file)
            assertTrue(queue.load())
            val reading = ReaderProgress("Android.txt", 400, 0, 20, false, 150)
            assertEquals(DurableResult.OK, queue.accept(DurableEntry(257, 4, ReaderProgressCodec.encode(reading)!!)))
            assertTrue(ReaderProgressCodec.enqueue(queue, reading))
            val restored = DurableQueue(file)
            assertTrue(restored.load())
            assertEquals(reading, ReaderProgressCodec.decode(restored.incoming(257)!!.payload))
            assertEquals(1, restored.snapshot().size)
        } finally { file.delete() }
    }

    @Test fun androidImageDecoderKeepsItsMonochromePreviewInTheViewModel() {
        val file = File.createTempFile("qualification-", ".png", context.cacheDir)
        val bitmap = android.graphics.Bitmap.createBitmap(800, 600, android.graphics.Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(android.graphics.Color.BLACK)
        file.outputStream().use { assertTrue(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
        val store = ViewModelStore()
        try {
            val model = ViewModelProvider(store, ViewModelProvider.AndroidViewModelFactory.getInstance(context))[CompanionTransferModel::class.java]
            InstrumentationRegistry.getInstrumentation().runOnMainSync { model.preparePicture(android.net.Uri.fromFile(file)) }
            val deadline = System.nanoTime() + 10_000_000_000L
            while (model.busy && System.nanoTime() < deadline) Thread.sleep(25)
            assertFalse(model.notice, model.busy)
            val picture = requireNotNull(model.picture) { model.notice }
            assertEquals(400, picture.width)
            assertEquals(300, picture.height)
            assertEquals(android.graphics.Color.BLACK, picture.getPixel(200, 150))
            val same = ViewModelProvider(store, ViewModelProvider.AndroidViewModelFactory.getInstance(context))[CompanionTransferModel::class.java]
            assertSame(model, same)
        } finally {
            InstrumentationRegistry.getInstrumentation().runOnMainSync { store.clear() }
            file.delete()
        }
    }

    @Test fun androidNetworkStackUploadsToTheCppFirmwareServer() {
        val url = InstrumentationRegistry.getArguments().getString("transferUrl")
        assumeTrue("Pass transferUrl from a running C++ book-web-host", url != null)
        val endpoint = TransferEndpoint.parse(requireNotNull(url), allowLoopback = true)
        val file = File.createTempFile("qualification-", ".txt", context.cacheDir)
        try {
            file.writeText("Android native network → C++ storage.\n".repeat(300))
            Note4TransferClient(endpoint, "ABCDEFGH2345").use { client ->
                assertTrue(client.library().coverSupported)
                val name = "Android-${System.currentTimeMillis()}.txt"
                client.uploadBook(file, name)
                assertTrue(client.library().books.any { it.name == name && it.size == file.length() })
                val cover = File.createTempFile("qualification-", ".pbm", context.cacheDir)
                try {
                    cover.writeBytes(MonochromeCover.encode(IntArray(120000) { -1 }))
                    client.uploadCover(cover)
                    client.removeCover()
                } finally { cover.delete() }
            }
        } finally { file.delete() }
    }
}
