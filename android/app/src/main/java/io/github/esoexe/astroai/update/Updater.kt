package io.github.esoexe.astroai.update

import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import io.github.esoexe.astroai.AstroApp
import io.github.esoexe.astroai.BuildConfig
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.strings
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import kotlinx.coroutines.CancellationException
import java.io.File

/**
 * Updates from the official site only (the app is not on Google Play). latest.json has an "android" section
 * with the APK address, size and SHA-256. The APK is downloaded, its SHA-256 checked, then Android itself asks
 * you to install it; Android also refuses any APK that is not signed with the same key as this app.
 */
class Updater(private val app: AstroApp) {
    companion object {
        const val FEED = "https://esoexe.github.io/astro-ai/latest.json"
        const val SITE = "https://esoexe.github.io/astro-ai/download.html"

        fun parse(v: String): List<Int> = Regex("(\\d+)\\.(\\d+)\\.(\\d+)").find(v)?.groupValues?.drop(1)?.map { it.toInt() } ?: listOf(0, 0, 0)
        fun newer(a: String, b: String): Boolean {
            val x = parse(a); val y = parse(b)
            for (i in 0..2) if (x[i] != y[i]) return x[i] > y[i]
            return false
        }
    }

    class Info(val version: String, val url: String, val sha256: String, val bytes: Long, val highlights: List<String>, val date: String)

    var available by mutableStateOf<Info?>(null)
        private set
    var checking by mutableStateOf(false)
        private set
    var progress by mutableFloatStateOf(-1f)
        private set
    var message by mutableStateOf("")
        private set

    val current: String get() = BuildConfig.VERSION_NAME.substringBefore('-')

    /** Checks at most every 12 hours unless [force]. Returns the update or null. */
    suspend fun check(force: Boolean = false): Info? {
        if (!force && (!app.prefs.updateAuto || System.currentTimeMillis() - app.prefs.updateLastCheck < 12 * 3600_000L)) return available
        checking = true; message = ""
        try {
            val j = Api.get(FEED, 20_000)
            app.prefs.updateLastCheck = System.currentTimeMillis()
            val a = j.optJSONObject("android")
            if (a == null) { available = null; message = tr("You have the latest version (#)", current); return null }
            val lang = I18n.lang
            val hl = a.optJSONObject("highlights_i18n")?.optJSONArray(lang)?.strings() ?: a.optJSONArray("highlights")?.strings() ?: emptyList()
            val info = Info(a.str("version"), a.str("url"), a.str("sha256"), a.long("bytes"), hl, a.str("released"))
            val ok = info.url.startsWith("https://") && info.sha256.length == 64 && newer(info.version, current)
            available = if (ok) info else null
            message = if (ok) tr("Version # is available", info.version) else tr("You have the latest version (#)", current)
            return available
        } catch (e: CancellationException) { throw e } catch (e: Exception) {
            message = tr("Could not check for updates: #", e.message ?: "")
            return null
        } finally { checking = false }
    }

    fun skip(info: Info) { app.prefs.updateSkip = info.version; available = null }
    val skipped: Boolean get() = available?.version == app.prefs.updateSkip

    /** Downloads, verifies and hands the APK to Android's installer. */
    suspend fun install(info: Info) {
        if (!app.packageManager.canRequestPackageInstalls()) {
            app.ui.toast(tr("Allow A.S.T.R.O AI to install updates, then tap Update again"))
            try { app.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { }
            return
        }
        val dir = File(app.cacheDir, "updates").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }
        val f = File(dir, "AstroAI-${info.version}-android.apk")
        progress = 0f; message = tr("Downloading #…", info.version)
        try {
            Api.download(info.url, f, info.sha256) { done, total -> val p = if (total > 0) done.toFloat() / total else -1f; app.ui.post { progress = p } }
            message = tr("Checked (SHA-256 matches). Android will ask you to install it.")
            val uri = FileProvider.getUriForFile(app, "${app.packageName}.files", f)
            app.startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (e: CancellationException) { f.delete(); throw e } catch (e: Exception) {
            f.delete()
            message = e.message ?: tr("The update failed")
            app.ui.toast(message, "error")
        } finally { progress = -1f }
    }
}
