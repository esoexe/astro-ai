package io.github.esoexe.astroai

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import io.github.esoexe.astroai.core.tr
import java.io.File

/** Navigation, toasts and system actions (share, open, copy) that any part of the app can trigger. */
class UiBus(private val app: AstroApp) {
    class Toast(val id: Long, val text: String, val kind: String, val action: String?, val run: (() -> Unit)?)

    private val main = Handler(Looper.getMainLooper())
    val toasts = mutableStateListOf<Toast>()
    private var nextId = 1L

    /** chat, agent, workspace, notes, tasks, reader, lab, knowledge, calculator, settings, connect, feedback */
    var route by mutableStateOf("chat")
    var settingsTab by mutableStateOf("general")
    var liveOpen by mutableStateOf(false)
    var readerUrl by mutableStateOf("")
    var drawerOpen by mutableStateOf(false)
    /** A pairing link (QR code from Settings > Phone on a computer) waiting for the user to confirm it. */
    var pendingPair by mutableStateOf<PairLink?>(null)
    /** Text shared to the app from another app, put into the composer. */
    var incomingText by mutableStateOf<String?>(null)

    fun toast(text: String, kind: String = "", action: String? = null, run: (() -> Unit)? = null) = post {
        val t = Toast(nextId++, text, kind, action, run)
        toasts.add(t)
        while (toasts.size > 3) toasts.removeAt(0)
        main.postDelayed({ toasts.remove(t) }, if (action != null) 6500 else 3800)
    }

    fun dismiss(t: Toast) { toasts.remove(t) }

    fun post(block: () -> Unit) { if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block) }

    fun go(target: String) = post {
        drawerOpen = false
        when {
            target == "live" -> liveOpen = true
            target.startsWith("settings") -> { settingsTab = target.substringAfter(':', "general"); route = "settings" }
            else -> route = target
        }
    }

    fun goSoon(target: String) { main.postDelayed({ go(target) }, 450) }
    fun closeLive() { liveOpen = false }

    fun copy(text: String, label: String = "A.S.T.R.O AI") {
        val cm = app.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText(label, text))
        if (android.os.Build.VERSION.SDK_INT < 33) toast(tr("Copied"))
    }

    fun openUrl(url: String) {
        if (!url.startsWith("https://") && !url.startsWith("http://")) return
        try { app.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { toast(tr("No app can open this link"), "error") }
    }

    private fun shareFile(name: String): File {
        val dir = File(app.cacheDir, "share").apply { mkdirs() }
        dir.listFiles()?.filter { System.currentTimeMillis() - it.lastModified() > 3_600_000 }?.forEach { it.delete() }
        return File(dir, name.replace(Regex("[\\\\/:*?\"<>|]+"), "-").ifEmpty { "file.txt" })
    }

    private fun mimeOf(name: String) = when (name.substringAfterLast('.', "").lowercase()) {
        "md" -> "text/markdown"; "html", "htm" -> "text/html"; "csv" -> "text/csv"; "json" -> "application/json"; "zip" -> "application/zip"
        "svg" -> "image/svg+xml"; "png" -> "image/png"; "jpg", "jpeg" -> "image/jpeg"; "pdf" -> "application/pdf"; else -> "text/plain"
    }

    /** Writes a file and opens Android's share sheet (save to Files, Drive, send...). */
    fun shareText(name: String, content: String) = shareBytes(name, content.toByteArray(Charsets.UTF_8))

    fun shareBytes(name: String, bytes: ByteArray) = post {
        try {
            val f = shareFile(name); f.writeBytes(bytes)
            val uri = FileProvider.getUriForFile(app, "${app.packageName}.files", f)
            val send = Intent(Intent.ACTION_SEND).setType(mimeOf(f.name)).putExtra(Intent.EXTRA_STREAM, uri).putExtra(Intent.EXTRA_TITLE, f.name)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            app.startActivity(Intent.createChooser(send, f.name).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (e: Exception) { toast(tr("Could not save the file: #", e.message ?: ""), "error") }
    }
}

/** astroai://pair?c=12345678&n=computer&u=http://192.168.1.20:4174 (from the computer's QR code, via the site's pair page). */
data class PairLink(val urls: List<String>, val code: String, val name: String) {
    companion object {
        private val URL_RE = Regex("^http://(\\d{1,3}(?:\\.\\d{1,3}){3}):(\\d{2,5})$")
        fun parse(uri: android.net.Uri): PairLink? {
            if (uri.scheme != "astroai" || uri.host != "pair") return null
            val code = uri.getQueryParameter("c")?.trim().orEmpty()
            if (!Regex("^\\d{8}$").matches(code)) return null
            // literal private IPv4 addresses only (no DNS lookup happens for a literal address)
            val urls = uri.getQueryParameters("u").filter { u ->
                val m = URL_RE.matchEntire(u) ?: return@filter false
                try { io.github.esoexe.astroai.net.Api.isPrivate(java.net.InetAddress.getByName(m.groupValues[1])) && m.groupValues[2].toInt() in 1..65535 } catch (_: Exception) { false }
            }.take(3)
            if (urls.isEmpty()) return null
            val name = uri.getQueryParameter("n").orEmpty().filter { it >= ' ' }.take(60)
            return PairLink(urls, code, name)
        }
    }
}
