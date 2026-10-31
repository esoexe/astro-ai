package io.github.esoexe.astroai

import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createEmptyComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import io.github.esoexe.astroai.engine.Backend
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * Phone pairing (desktop Settings > Phone). The link checks always run. The end-to-end test needs a real engine
 * with its phone listener on: ASTRO_PAIR_ADMIN = its local address (makes the code, lists devices) and
 * ASTRO_PAIR_URL = the phone listener's private-network address the phone would use.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w400dp-h860dp-xhdpi")
class PairingTest {
    @get:Rule val rule = createEmptyComposeRule()
    private var scenario: ActivityScenario<MainActivity>? = null
    private val app get() = ApplicationProvider.getApplicationContext<AstroApp>()
    private val admin = System.getenv("ASTRO_PAIR_ADMIN").orEmpty()
    private val remote = System.getenv("ASTRO_PAIR_URL").orEmpty()

    @After fun close() { scenario?.close() }

    /** Backend coroutines run on the main looper; Robolectric only runs it when asked. */
    private fun pump() = org.robolectric.Shadows.shadowOf(android.os.Looper.getMainLooper()).idle()

    private fun settle(ms: Long = 600) { rule.mainClock.advanceTimeBy(ms); rule.waitForIdle() }

    private fun shot(name: String) {
        if (System.getenv("ASTRO_SMOKE_SHOTS") != "1") return
        val dir = File("build/smoke").apply { mkdirs() }
        val bmp = rule.onRoot().captureToImage().asAndroidBitmap()
        File(dir, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    private fun open(link: String) {
        val i = Intent(Intent.ACTION_VIEW, Uri.parse(link)).setClass(app, MainActivity::class.java)
        scenario = ActivityScenario.launch(i)
        settle()
    }

    private fun adminCall(method: String, path: String, body: String? = null): JSONObject {
        val c = URL(admin + path).openConnection() as HttpURLConnection
        c.requestMethod = method; c.connectTimeout = 3000; c.readTimeout = 8000
        c.setRequestProperty("x-astro-client", "pairing-test")
        if (body != null) { c.doOutput = true; c.setRequestProperty("content-type", "application/json"); c.outputStream.use { it.write(body.toByteArray()) } }
        val text = (if (c.responseCode < 400) c.inputStream else c.errorStream).use { it.readBytes().toString(Charsets.UTF_8) }
        return JSONObject(text)
    }

    @Test fun parsesOnlySafePairingLinks() {
        val ok = PairLink.parse(Uri.parse("astroai://pair?c=12345678&n=My%20PC&u=http%3A%2F%2F192.168.1.5%3A4174&u=http%3A%2F%2F100.101.2.3%3A4174"))
        assertNotNull(ok)
        assertEquals("12345678", ok!!.code)
        assertEquals("My PC", ok.name)
        assertEquals(listOf("http://192.168.1.5:4174", "http://100.101.2.3:4174"), ok.urls)
        // public address, https, a host name, a path: all dropped; nothing left -> no link
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=12345678&u=http%3A%2F%2F8.8.8.8%3A4174")))
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=12345678&u=https%3A%2F%2F192.168.1.5%3A4174")))
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=12345678&u=http%3A%2F%2Fevil.example%3A4174")))
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=12345678&u=http%3A%2F%2F192.168.1.5%3A4174%2Fx")))
        val mixed = PairLink.parse(Uri.parse("astroai://pair?c=12345678&u=http%3A%2F%2F8.8.8.8%3A4174&u=http%3A%2F%2F10.0.0.2%3A4174"))
        assertEquals(listOf("http://10.0.0.2:4174"), mixed!!.urls)
        // bad codes, wrong host or scheme
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=1234567&u=http%3A%2F%2F192.168.1.5%3A4174")))
        assertNull(PairLink.parse(Uri.parse("astroai://pair?c=abcdefgh&u=http%3A%2F%2F192.168.1.5%3A4174")))
        assertNull(PairLink.parse(Uri.parse("astroai://other?c=12345678&u=http%3A%2F%2F192.168.1.5%3A4174")))
        assertNull(PairLink.parse(Uri.parse("https://pair?c=12345678&u=http%3A%2F%2F192.168.1.5%3A4174")))
        // at most 3 addresses, names are trimmed to 60 characters
        val many = PairLink.parse(Uri.parse("astroai://pair?c=12345678&n=" + "x".repeat(200) + (1..5).joinToString("") { "&u=http%3A%2F%2F10.0.0.$it%3A4174" }))
        assertEquals(3, many!!.urls.size)
        assertEquals(60, many.name.length)
    }

    @Test fun linkAsksFirstAndSendsNothing() {
        app.prefs.welcomeDone = false
        app.prefs.updateAuto = false
        open("astroai://pair?c=12345678&n=Office%20PC&u=http%3A%2F%2F192.168.77.5%3A4174")
        assertNotNull(app.ui.pendingPair)
        rule.onNodeWithText("Connect to Office PC?").assertExists()
        shot("30-pair-confirm")
        assertEquals("", app.prefs.pcToken)
        assertFalse(app.prefs.pcPaired)
        assertEquals("builtin", app.prefs.connMode)
        rule.onNodeWithText("Cancel").performClick()
        settle()
        assertNull(app.ui.pendingPair)
    }

    @Test fun pairsWithARealComputerAndForgets() {
        assumeTrue("set ASTRO_PAIR_ADMIN and ASTRO_PAIR_URL", admin.isNotEmpty() && remote.isNotEmpty())
        val before = adminCall("GET", "/api/phone").getJSONArray("devices").length()
        val code = adminCall("POST", "/api/phone/pairing", "{}").getJSONObject("pairing").getString("code")
        app.prefs.welcomeDone = false
        app.prefs.updateAuto = false
        open("astroai://pair?c=$code&n=Test%20PC&u=" + Uri.encode(remote))
        rule.onNodeWithText("Connect").performClick()
        val seen = mutableListOf<String>()
        try { rule.waitUntil(30_000) { pump(); val ph = "${app.backend.phase}/${app.prefs.pcPaired}"; if (seen.lastOrNull() != ph) seen += ph; app.prefs.pcPaired && app.backend.phase == Backend.Phase.Ready } }
        catch (e: Throwable) { throw AssertionError("paired=${app.prefs.pcPaired} phase=${app.backend.phase} problem=${app.backend.problem} url=${app.prefs.pcUrl} pending=${app.ui.pendingPair} seen=$seen", e) }
        assertEquals("pc", app.prefs.connMode)
        assertEquals(remote, app.prefs.pcUrl)
        assertEquals(64, app.prefs.pcToken.length)
        assertTrue(app.prefs.welcomeDone)
        assertNull(app.ui.pendingPair)
        settle(1500)
        shot("31-paired-chat")
        val after = adminCall("GET", "/api/phone")
        assertEquals(before + 1, after.getJSONArray("devices").length())
        assertFalse("the code is used up", after.has("pairing"))

        // Using the phone's own engine keeps the key; "Forget this computer" removes it on the computer too.
        rule.runOnIdle { app.backend.useBuiltIn() }
        settle()
        assertTrue(app.prefs.pcPaired)
        assertEquals(before + 1, adminCall("GET", "/api/phone").getJSONArray("devices").length())
        rule.runOnIdle { app.backend.forgetPc() }
        rule.waitUntil(10_000) { pump(); adminCall("GET", "/api/phone").getJSONArray("devices").length() == before }
        assertFalse(app.prefs.pcPaired)
        assertEquals("", app.prefs.pcToken)
    }
}
