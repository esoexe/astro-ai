package io.github.esoexe.astroai.core

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Settings, kept in SharedPreferences. Compose reads them through observable state, so a change anywhere
 * (Settings, an action in a chat, Live voice) updates every screen at once.
 */
class Prefs(ctx: Context) {
    private val sp: SharedPreferences = ctx.getSharedPreferences("astro", Context.MODE_PRIVATE)

    // Bumped on every change: screens that show many settings read it to recompose.
    var rev by mutableIntStateOf(0)
        private set

    private inner class Str(private val key: String, private val def: String, private val allowed: Set<String>? = null) {
        var state = mutableStateOf(read())
        fun read(): String = (sp.getString(key, def) ?: def).let { if (allowed != null && it !in allowed) def else it }
        fun get() = state.value
        fun set(v: String) { val x = if (allowed != null && v !in allowed) def else v; sp.edit().putString(key, x).apply(); state.value = x; rev++ }
    }
    private inner class Bool(private val key: String, private val def: Boolean) {
        var state = mutableStateOf(sp.getBoolean(key, def))
        fun get() = state.value
        fun set(v: Boolean) { sp.edit().putBoolean(key, v).apply(); state.value = v; rev++ }
    }
    private inner class Num(private val key: String, private val def: Float) {
        var state = mutableStateOf(sp.getFloat(key, def))
        fun get() = state.value
        fun set(v: Float) { sp.edit().putFloat(key, v).apply(); state.value = v; rev++ }
    }
    private inner class LongV(private val key: String, private val def: Long) {
        var state = mutableStateOf(sp.getLong(key, def))
        fun get() = state.value
        fun set(v: Long) { sp.edit().putLong(key, v).apply(); state.value = v; rev++ }
    }

    // ----- chat -----
    private val _mode = Str("workspace_mode_v1", "general", setOf("general", "writing", "tutor", "coding", "planning", "research"))
    var mode: String get() = _mode.get(); set(v) = _mode.set(v)
    private val _language = Str("workspace_language_v1", "auto", setOf("auto", "English", "French", "Arabic"))
    var language: String get() = _language.get(); set(v) = _language.set(v)
    /** "brain" (One Brain, the default), "auto" (fastest route) or a model id. */
    private val _model = Str("workspace_model_v1", "brain")
    var model: String get() = _model.get(); set(v) = _model.set(v)
    private val _agent = Bool("workspace_agent_v1", true)
    /** The assistant can act in the app (notes, tasks, reminders, memory, web search). */
    var agentTools: Boolean get() = _agent.get(); set(v) = _agent.set(v)
    private val _web = Bool("astro_web", false)
    var webSearch: Boolean get() = _web.get(); set(v) = _web.set(v)
    private val _enter = Bool("workspace_enter_v1", false)
    /** Hardware keyboards: Enter sends (phones use the send button by default). */
    var enterSends: Boolean get() = _enter.get(); set(v) = _enter.set(v)
    private val _bgcheck = Bool("astro_bgcheck", true)
    var codeCheck: Boolean get() = _bgcheck.get(); set(v) = _bgcheck.set(v)

    // ----- appearance -----
    private val _theme = Str("workspace_theme_v1", "system", setOf("system", "light", "dark"))
    var theme: String get() = _theme.get(); set(v) = _theme.set(v)
    private val _density = Str("workspace_density_v1", "normal", setOf("compact", "normal", "large"))
    var density: String get() = _density.get(); set(v) = _density.set(v)
    private val _uiLang = Str("astro.uiLang", "", setOf("", "en", "fr", "ar"))
    /** Interface language; empty = follow the phone. */
    var uiLang: String get() = _uiLang.get(); set(v) = _uiLang.set(v)
    private val _name = Str("workspace_name_v1", "")
    var name: String get() = _name.get(); set(v) = _name.set(clip(v.trim(), 40))
    private val _motion = Bool("astro_motion", true)
    var motion: Boolean get() = _motion.get(); set(v) = _motion.set(v)

    // ----- One Brain -----
    private val _minds = Num("brain_minds", 0f)
    /** 0 = All (every free model thinks together, the default); otherwise 2..12. */
    var brainMinds: Int get() = _minds.get().toInt(); set(v) = _minds.set((if (v == 0) 0 else v.coerceIn(2, 12)).toFloat())
    private val _grace = Num("brain_grace", 8f)
    var brainGrace: Int get() = _grace.get().toInt().coerceIn(2, 20); set(v) = _grace.set(v.coerceIn(2, 20).toFloat())
    private val _allowTrain = Bool("brain_allow_train", true)
    var brainAllowTrain: Boolean get() = _allowTrain.get(); set(v) = _allowTrain.set(v)
    private val _waitAll = Bool("brain_wait_all", true)
    /** All mode waits for every model, even slow ones, before the merge (the user's choice: patient). */
    var brainWaitAll: Boolean get() = _waitAll.get(); set(v) = _waitAll.set(v)
    private val _fastFirst = Bool("brain_fast_first", true)
    /** Shows the first finished draft while the others are still thinking. */
    var brainFastFirst: Boolean get() = _fastFirst.get(); set(v) = _fastFirst.set(v)
    private val _localOnly = Bool("brain_local_only", false)
    var brainLocalOnly: Boolean get() = _localOnly.get(); set(v) = _localOnly.set(v)
    private val _brainTimes = Str("astro_brain_times", "[]")
    var brainTimes: String get() = _brainTimes.get(); set(v) = _brainTimes.set(v)

    // ----- voice -----
    private val _rate = Num("voice_rate", 1f)
    var voiceRate: Float get() = _rate.get().coerceIn(0.6f, 1.6f); set(v) = _rate.set(v.coerceIn(0.6f, 1.6f))
    private val _pitch = Num("voice_pitch", 1f)
    var voicePitch: Float get() = _pitch.get().coerceIn(0.7f, 1.4f); set(v) = _pitch.set(v.coerceIn(0.7f, 1.4f))
    private val _voiceName = Str("voice_name", "")
    var voiceName: String get() = _voiceName.get(); set(v) = _voiceName.set(v)
    private val _autoSpeak = Bool("voice_auto_speak", false)
    var autoSpeak: Boolean get() = _autoSpeak.get(); set(v) = _autoSpeak.set(v)

    // ----- connection -----
    private val _conn = Str("conn_mode", "builtin", setOf("builtin", "pc"))
    /** "builtin" = the engine inside this phone; "pc" = A.S.T.R.O AI running on your computer. */
    var connMode: String get() = _conn.get(); set(v) = _conn.set(v)
    private val _pcUrl = Str("conn_pc_url", "")
    var pcUrl: String get() = _pcUrl.get(); set(v) = _pcUrl.set(v.trim().trimEnd('/'))
    private val _pcToken = Str("conn_pc_token", "")
    /** Session token from the computer's sign-in (never the password). */
    var pcToken: String get() = _pcToken.get(); set(v) = _pcToken.set(v)
    /** pcToken is a device key from pairing (Settings > Phone on the computer), not a password session. */
    private val _pcPaired = Bool("conn_pc_paired", false)
    var pcPaired: Boolean get() = _pcPaired.get(); set(v) = _pcPaired.set(v)
    private val _pcName = Str("conn_pc_name", "")
    var pcName: String get() = _pcName.get(); set(v) = _pcName.set(v.take(60))

    // ----- sync, privacy, app -----
    private val _syncOn = Bool("sync_on", false)
    var syncOn: Boolean get() = _syncOn.get(); set(v) = _syncOn.set(v)
    private val _syncUpdated = LongV("sync_updated", 0L)
    var syncUpdated: Long get() = _syncUpdated.get(); set(v) = _syncUpdated.set(v)
    private val _syncSig = Str("sync_sig", "")
    var syncSig: String get() = _syncSig.get(); set(v) = _syncSig.set(v)
    private val _crash = Bool("crash_reports", false)
    /** Opt-in automatic crash reports (off by default). */
    var crashReports: Boolean get() = _crash.get(); set(v) = _crash.set(v)
    private val _welcome = Bool("welcome_done", false)
    var welcomeDone: Boolean get() = _welcome.get(); set(v) = _welcome.set(v)
    private val _updLast = LongV("update_last_check", 0L)
    var updateLastCheck: Long get() = _updLast.get(); set(v) = _updLast.set(v)
    private val _updSkip = Str("update_skip", "")
    var updateSkip: String get() = _updSkip.get(); set(v) = _updSkip.set(v)
    private val _updAuto = Bool("update_auto", true)
    var updateAuto: Boolean get() = _updAuto.get(); set(v) = _updAuto.set(v)
    private val _labSel = Str("lab_selected", "[]")
    var labSelected: String get() = _labSel.get(); set(v) = _labSel.set(v)
    private val _agentModel = Str("agent_model", "")
    var agentModel: String get() = _agentModel.get(); set(v) = _agentModel.set(v)
    private val _agentMode = Str("agent_mode", "auto", setOf("auto", "plan", "ask"))
    var agentMode: String get() = _agentMode.get(); set(v) = _agentMode.set(v)
    private val _wsProject = Str("ws_project", "")
    var wsProject: String get() = _wsProject.get(); set(v) = _wsProject.set(v)
    private val _exactAsked = Bool("exact_alarm_asked", false)
    var exactAlarmAsked: Boolean get() = _exactAsked.get(); set(v) = _exactAsked.set(v)
    private val _editorWrap = Bool("ws_wrap", true)
    var editorWrap: Boolean get() = _editorWrap.get(); set(v) = _editorWrap.set(v)
}
