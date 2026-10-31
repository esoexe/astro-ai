package io.github.esoexe.astroai

import android.graphics.Bitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.compose.ui.test.onRoot
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.engine.Backend
import org.junit.After
import org.junit.Assert.assertEquals
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
 * Starts the real app (AstroApp + MainActivity) on the JVM and opens every screen. Any crash while composing,
 * in a click-free first frame, or in a coroutine the screen starts fails the test.
 *
 * The phone engine can't run on the JVM, so the main screens use the app's own "Connect to my computer" mode
 * against a real engine: set ASTRO_SMOKE_URL (e.g. http://127.0.0.1:4391, started with ASTRO_AUTH=off).
 * Without it those tests are skipped. Screenshots go to build/smoke/ when ASTRO_SMOKE_SHOTS=1.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w400dp-h860dp-xhdpi")
class ScreensSmokeTest {
    @get:Rule val rule = createEmptyComposeRule()
    private var scenario: ActivityScenario<MainActivity>? = null
    private val app get() = ApplicationProvider.getApplicationContext<AstroApp>()
    private val engineUrl = System.getenv("ASTRO_SMOKE_URL").orEmpty()

    @After fun close() { scenario?.close() }

    private fun shot(name: String) {
        if (System.getenv("ASTRO_SMOKE_SHOTS") != "1") return
        val dir = File("build/smoke").apply { mkdirs() }
        val bmp = rule.onRoot().captureToImage().asAndroidBitmap()
        File(dir, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    private fun settle(ms: Long = 600) {
        rule.mainClock.advanceTimeBy(ms)
        rule.waitForIdle()
    }

    private fun launch() {
        scenario = ActivityScenario.launch(MainActivity::class.java)
        settle()
    }

    private fun connectToEngine() {
        assumeTrue("set ASTRO_SMOKE_URL to a running engine", engineUrl.isNotEmpty() && reachable(engineUrl))
        app.prefs.welcomeDone = true
        app.prefs.connMode = "pc"
        app.prefs.pcUrl = engineUrl
        app.prefs.pcToken = ""
        app.prefs.updateAuto = false
        launch()
        rule.waitUntil(30_000) { app.backend.phase != Backend.Phase.Starting }
        assertEquals("backend: ${app.backend.problem}", Backend.Phase.Ready, app.backend.phase)
        settle(1500)
    }

    private fun reachable(u: String) = try {
        (URL("$u/api/auth/state").openConnection() as HttpURLConnection).run { connectTimeout = 2000; readTimeout = 4000; responseCode == 200 }
    } catch (_: Exception) { false }

    @Test fun welcomeScreen() {
        app.prefs.welcomeDone = false
        app.prefs.updateAuto = false
        launch()
        shot("00-welcome")
    }

    @Test fun everyScreenOpens() {
        connectToEngine()
        val routes = listOf("chat", "chats", "notes", "tasks", "knowledge", "calculator", "lab", "reader", "workspace", "agent", "feedback", "connect")
        routes.forEachIndexed { i, r ->
            rule.runOnIdle { app.ui.go(r) }
            settle(1500)
            shot("%02d-%s".format(i + 1, r))
        }
        rule.runOnIdle { app.ui.go("live") }
        settle(1000)
        shot("20-live")
        rule.runOnIdle { app.ui.liveOpen = false; app.ui.drawerOpen = true }
        settle(800)
        shot("21-drawer")
    }

    @Test fun everySettingsTabOpens() {
        connectToEngine()
        listOf("general", "chat", "brain", "voice", "providers", "connection", "data", "about").forEachIndexed { i, t ->
            rule.runOnIdle { app.ui.go("settings:$t") }
            settle(1500)
            shot("3$i-settings-$t")
        }
    }

    @Test fun darkThemeAndArabic() {
        connectToEngine()
        rule.runOnIdle { app.prefs.theme = "dark" }
        settle()
        shot("40-chat-dark")
        rule.runOnIdle { app.ui.go("workspace") }
        settle(1500)
        shot("41-workspace-dark")
        rule.runOnIdle { app.prefs.uiLang = "ar"; app.applyUiLanguage(); app.ui.go("chat") }
        settle(1000)
        assertEquals("ar", I18n.lang)
        shot("42-chat-arabic")
        rule.runOnIdle { app.ui.go("settings:general") }
        settle(1000)
        shot("43-settings-arabic")
    }
}
