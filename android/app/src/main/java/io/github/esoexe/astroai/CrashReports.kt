package io.github.esoexe.astroai

import android.os.Build
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.engine.Backend
import io.github.esoexe.astroai.net.Api
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Opt-in crash reports (off by default, Settings > Data). A crash of the app is written to a local file; at the
 * next start it is sent through the engine's /api/report (which removes personal paths and keys) only when
 * the user turned reports on. Otherwise the file is simply deleted.
 * Reports the user sends by hand (feedback, "the engine could not start") go through the engine when it runs,
 * or straight to the same reports table with the same redaction when it does not.
 */
object CrashReports {
    private lateinit var file: File
    // Same public endpoint and publishable (client-side) key the engine uses in astro_quality.inc.
    private const val DIRECT = "https://vgtucdfretcvojcfvbzk.supabase.co/rest/v1/astro_reports"
    private const val PUBLISHABLE = "sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol"

    fun install(app: AstroApp) {
        file = File(app.filesDir, "crash.txt")
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try { file.writeText("${e.javaClass.name}: ${e.message}\n" + e.stackTrace.take(30).joinToString("\n") { "  at $it" }) } catch (_: Exception) { }
            previous?.uncaughtException(t, e)
        }
    }

    val platform: String get() = "android-${Build.VERSION.SDK_INT}-${Build.SUPPORTED_ABIS.firstOrNull() ?: "arm64"}"

    /** Called once the engine is ready. */
    suspend fun sendPending(app: AstroApp) {
        if (!file.exists()) return
        val text = try { file.readText() } catch (_: Exception) { "" }
        file.delete()
        if (!app.prefs.crashReports || text.isBlank()) return
        try { sendNow(app, "android-app", text.lineSequence().first().take(400), text.take(7000), kind = "crash") } catch (_: Exception) { }
    }

    /** Sends one report now. Throws with a readable message when it could not be sent. */
    suspend fun sendNow(app: AstroApp, category: String, message: String, detail: String = "", kind: String = "error", contact: String = "") {
        val msg = message.ifBlank { "(no message)" }
        if (app.backend.phase == Backend.Phase.Ready) {
            val r = Api.post("/api/report", jsonOf("kind" to kind, "category" to category, "message" to msg, "detail" to detail, "contact" to contact,
                "platform" to platform, "source" to "android", "lang" to app.prefs.uiLang), 20_000)
            if (r.has("ok") && !r.optBoolean("ok")) throw IllegalStateException(r.optString("error", "Could not send"))
            return
        }
        val row = jsonOf("kind" to kind, "category" to scrub(category).take(40), "message" to scrub(msg).take(4000), "detail" to scrub(detail).take(8000),
            "contact" to (if (kind == "feedback") contact.take(200) else ""), "platform" to platform, "app_version" to BuildConfig.VERSION_NAME.substringBefore('-'),
            "lang" to app.prefs.uiLang.take(40), "source" to "android")
        withContext(Dispatchers.IO) {
            Api.requestBlocking("POST", DIRECT, row, 20_000, mapOf("apikey" to PUBLISHABLE, "Prefer" to "return=minimal"))
        }
    }

    private val reKeys = Regex("(sk|pk|rk|sb_secret|sb_publishable|ghp|gho|ghu|ghs|ghr|github_pat|xox[abprs]|AIza|hf|gsk|csk|nvapi|or-v1)[-_][-_A-Za-z0-9]{10,}")
    private val reJwt = Regex("eyJ[-_A-Za-z0-9]{10,}\\.[-_A-Za-z0-9]{10,}\\.[-_A-Za-z0-9]{10,}")
    private val reBearer = Regex("(authorization|bearer|api[-_ ]?key|token|password|secret)([\"'\\s:=]+)(bearer\\s+|basic\\s+)?[^\\s\"',;]{6,}", RegexOption.IGNORE_CASE)
    private val reLong = Regex("[A-Za-z0-9_\\-]{40,}")
    private val reEmail = Regex("[A-Za-z0-9._%+\\-]+@[A-Za-z0-9.\\-]+\\.[A-Za-z]{2,}")
    private val reData = Regex("/data/(user/\\d+|data)/[^/\\s\"']+")

    /** The engine's scrub_report, plus Android's per-app data paths. */
    fun scrub(t: String): String = t.replace(reJwt, "[redacted-token]").replace(reKeys, "[redacted-key]").replace(reBearer, "$1$2[redacted]")
        .replace(reLong, "[redacted]").replace(reEmail, "[email]").replace(reData, "~")
}
