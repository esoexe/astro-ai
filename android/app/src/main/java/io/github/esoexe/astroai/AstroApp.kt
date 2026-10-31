package io.github.esoexe.astroai

import android.app.Application
import android.content.res.Configuration
import io.github.esoexe.astroai.brain.ChatController
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.core.Prefs
import io.github.esoexe.astroai.core.Store
import io.github.esoexe.astroai.engine.Backend
import io.github.esoexe.astroai.engine.LocalEngine
import io.github.esoexe.astroai.update.Updater
import io.github.esoexe.astroai.voice.Speech
import java.util.Locale

/** App-wide singletons. Everything runs in one process; the engine is a child process of it. */
class AstroApp : Application() {
    lateinit var prefs: Prefs; private set
    lateinit var store: Store; private set
    lateinit var engine: LocalEngine; private set
    lateinit var backend: Backend; private set
    lateinit var ui: UiBus; private set
    lateinit var work: Work; private set
    lateinit var reminders: Reminders; private set
    lateinit var speech: Speech; private set
    lateinit var chat: ChatController; private set
    lateinit var updater: Updater; private set

    /** True while an activity of the app is on screen. */
    @Volatile var foreground = false
    /** The backend and reminders are started once per process, by the first activity. */
    var started = false

    override fun onCreate() {
        super.onCreate()
        instance = this
        CrashReports.install(this)
        prefs = Prefs(this)
        I18n.load(this)
        applyUiLanguage()
        store = Store(this, prefs)
        engine = LocalEngine(this)
        backend = Backend(prefs, engine)
        ui = UiBus(this)
        work = Work(this)
        reminders = Reminders(this)
        store.onRemindersChanged = { reminders.scheduleAll() }
        speech = Speech(this)
        chat = ChatController(this)
        updater = Updater(this)
    }

    /** Interface language: the user's choice, otherwise the phone's language when it is French or Arabic. */
    fun applyUiLanguage() {
        val chosen = prefs.uiLang
        val sys = Locale.getDefault().language
        I18n.setLanguage(chosen.ifEmpty { if (sys == "fr" || sys == "ar") sys else "en" })
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (prefs.uiLang.isEmpty()) applyUiLanguage()
    }

    companion object {
        lateinit var instance: AstroApp
            private set
    }
}
