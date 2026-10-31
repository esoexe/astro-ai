package io.github.esoexe.astroai

import android.Manifest
import android.app.AlarmManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.LayoutDirection
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.lifecycle.lifecycleScope
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.engine.Backend
import io.github.esoexe.astroai.ui.AppRoot
import io.github.esoexe.astroai.ui.AstroTheme
import io.github.esoexe.astroai.ui.screens.loadImage
import io.github.esoexe.astroai.ui.screens.readDocument
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Asks for the microphone from anywhere in the UI. */
object MicPermission {
    internal var request: (() -> Unit)? = null
    fun granted(ctx: Context) = ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    /** True when the microphone can be used now; otherwise asks and returns false (tap again after allowing). */
    fun ensure(ctx: Context): Boolean { if (granted(ctx)) return true; request?.invoke(); return false }
}

class MainActivity : ComponentActivity() {
    private val app get() = AstroApp.instance

    private val askMic = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (!ok) app.ui.toast(tr("Voice input needs the microphone permission."), "error") else app.ui.toast(tr("Microphone allowed. Tap the mic again."))
    }
    private val askNotify = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (!ok) app.ui.toast(tr("Reminders can't ring without notifications. You can allow them in Android settings."), "error")
        else checkExactAlarms()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        MicPermission.request = { askMic.launch(Manifest.permission.RECORD_AUDIO) }
        app.reminders.askPermission = { askNotifications() }

        if (!app.started) {
            app.started = true
            app.backend.start()
            app.reminders.scheduleAll()
            lifecycleScope.launch {
                snapshotFlow { app.backend.phase }.first { it == Backend.Phase.Ready }
                CrashReports.sendPending(app)
            }
        }
        // While connected to a computer with sync on: save now and then (only when something changed).
        lifecycleScope.launch {
            while (true) {
                delay(30_000)
                if (app.prefs.syncOn && app.backend.isPc && app.backend.phase == Backend.Phase.Ready && !app.chat.loading)
                    try { withContext(Dispatchers.IO) { app.store.pushBlocking() } } catch (_: Exception) { }
            }
        }
        if (savedInstanceState == null) handleIntent(intent)

        setContent {
            AstroTheme(app.prefs.theme, app.prefs.density) {
                CompositionLocalProvider(LocalLayoutDirection provides if (I18n.rtl) LayoutDirection.Rtl else LayoutDirection.Ltr) {
                    AppRoot()
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        app.foreground = true
        app.work.onReturn()
    }

    override fun onPause() {
        super.onPause()
        app.foreground = false
        app.work.onLeave()
        app.store.flush()
    }

    private fun askNotifications() {
        if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            askNotify.launch(Manifest.permission.POST_NOTIFICATIONS)
        } else checkExactAlarms()
    }

    /** Exact reminders need the "Alarms & reminders" permission on Android 12+; without it they may ring a few minutes late. */
    private fun checkExactAlarms() {
        if (Build.VERSION.SDK_INT < 31) return
        val am = getSystemService(AlarmManager::class.java) ?: return
        if (am.canScheduleExactAlarms() || app.prefs.exactAlarmAsked) return
        app.prefs.exactAlarmAsked = true
        app.ui.toast(tr("Allow exact alarms so reminders ring on time"), action = tr("Allow")) {
            try { startActivity(Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, Uri.parse("package:$packageName"))) } catch (_: Exception) { }
        }
    }

    private fun handleIntent(intent: Intent?) {
        intent ?: return
        intent.getStringExtra("route")?.let { app.ui.go(it) }
        if (intent.action != Intent.ACTION_SEND) return
        val type = intent.type ?: ""
        val text = intent.getStringExtra(Intent.EXTRA_TEXT)
        val subject = intent.getStringExtra(Intent.EXTRA_SUBJECT)
        val stream = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java)
        app.ui.go("chat")
        when {
            stream != null && type.startsWith("image/") -> lifecycleScope.launch {
                try { app.chat.pendingImage = withContext(Dispatchers.IO) { loadImage(stream) } } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
                if (!text.isNullOrBlank()) app.ui.incomingText = text
            }
            stream != null -> lifecycleScope.launch {
                snapshotFlow { app.backend.phase }.first { it == Backend.Phase.Ready }
                try { app.chat.documents.add(withContext(Dispatchers.IO) { readDocument(stream) }); app.ui.toast(tr("File attached to the chat")) }
                catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
            }
            !text.isNullOrBlank() -> {
                val link = io.github.esoexe.astroai.brain.Text.URL_RE.find(text)?.value
                app.ui.incomingText = if (link != null && text.trim() == link) tr("Summarise this page: #", link)
                    else listOfNotNull(subject?.takeIf { it.isNotBlank() && !text.contains(it) }, text).joinToString("\n\n")
            }
        }
    }

}
