package io.github.esoexe.astroai.engine

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.esoexe.astroai.core.Prefs
import io.github.esoexe.astroai.core.arr
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.net.ApiError
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject

/**
 * Which engine the app talks to: the one built into the phone (default) or A.S.T.R.O AI running on your
 * computer ("Connect to my PC": the computer's own server, its keys and models, signed in with its password).
 */
class Backend(private val prefs: Prefs, private val engine: LocalEngine, private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)) {
    enum class Phase { Starting, Ready, NeedsLogin, NeedsSetup, Unreachable, Failed }

    var phase by mutableStateOf(Phase.Starting)
        private set
    var problem by mutableStateOf("")
        private set
    var log by mutableStateOf("")
        private set
    /** Latest /api/status: models, providers, version. */
    var status by mutableStateOf(JSONObject())
        private set
    var statusAt by mutableLongStateOf(0L)
        private set
    /** Per-model live health from /api/models/health (One Brain ranking). */
    var health: JSONObject = JSONObject()
        private set

    private var watch: Job? = null
    private var poll: Job? = null

    val isPc: Boolean get() = prefs.connMode == "pc"

    fun start() {
        watch?.cancel()
        phase = Phase.Starting; problem = ""
        if (isPc) {
            engine.stop()
            Api.configure(prefs.pcUrl, prefs.pcToken)
            watch = scope.launch { checkPc() }
        } else {
            engine.start()
            watch = scope.launch {
                engine.state.collectLatest { s ->
                    when (s) {
                        is LocalEngine.State.Ready -> { Api.configure(s.base, s.token); phase = Phase.Ready; problem = ""; refreshStatus(); startPolling() }
                        is LocalEngine.State.Failed -> { phase = Phase.Failed; problem = s.message; log = s.log }
                        LocalEngine.State.Starting, LocalEngine.State.Stopped -> if (phase != Phase.Failed) phase = Phase.Starting
                    }
                }
            }
        }
    }

    fun restartEngine() { if (isPc) start() else { phase = Phase.Starting; engine.restart() } }

    private suspend fun checkPc() {
        if (prefs.pcUrl.isBlank()) { phase = Phase.Unreachable; problem = "No computer address yet"; return }
        try {
            val st = Api.get("/api/auth/state", 8000)
            when {
                st.bool("enabled") && st.bool("setup") -> phase = Phase.NeedsSetup
                st.bool("enabled") && !st.bool("ok") -> phase = Phase.NeedsLogin
                else -> { phase = Phase.Ready; problem = ""; refreshStatus(); startPolling() }
            }
        } catch (e: Exception) {
            phase = Phase.Unreachable
            problem = e.message ?: "Could not reach the computer"
        }
    }

    /**
     * Tries a computer address without switching to it (no credentials are sent). Returns its auth state
     * ({enabled, setup, ok}); throws with a readable message when it does not answer like A.S.T.R.O AI.
     */
    suspend fun probe(url: String): JSONObject {
        val u = normalizeUrl(url)
        if (u.isEmpty()) throw ApiError(0, "Type the computer's address")
        val st = Api.get("$u/api/auth/state", 6000)
        if (!st.has("enabled")) throw ApiError(0, "This address does not answer like A.S.T.R.O AI")
        return st
    }

    /** Signs in to the computer (or creates its first password) and switches to it. */
    suspend fun signIn(url: String, password: String, create: Boolean) {
        val u = normalizeUrl(url)
        // Absolute address: the current connection stays untouched if the password is wrong.
        val j = Api.post(u + (if (create) "/api/auth/setup" else "/api/auth/login"), jsonOf("password" to password), 20_000)
        val token = j.str("token")
        if (token.length < 32) throw ApiError(0, "The computer did not return a session")
        prefs.pcUrl = u; prefs.pcToken = token; prefs.connMode = "pc"
        start()
    }

    /** Uses a computer that has no password (ASTRO_AUTH=off on a trusted network). */
    fun useOpenPc(url: String) { prefs.pcUrl = normalizeUrl(url); prefs.pcToken = ""; prefs.connMode = "pc"; start() }

    fun useBuiltIn() {
        if (prefs.connMode == "pc" && prefs.pcToken.isNotEmpty()) scope.launch { try { Api.post("/api/auth/logout") } catch (_: Exception) { } }
        prefs.connMode = "builtin"; prefs.pcToken = ""
        start()
    }

    fun normalizeUrl(raw: String): String {
        var s = raw.trim().trimEnd('/')
        if (s.isEmpty()) return s
        if (!s.startsWith("http://") && !s.startsWith("https://")) s = "http://$s"
        if (Regex("^https?://[^/:]+$").matches(s)) s += ":4173"
        return s
    }

    private fun startPolling() {
        poll?.cancel()
        poll = scope.launch {
            var n = 0
            while (isActive) {
                delay(60_000)
                refreshStatus()
                if (n++ % 2 == 0) refreshHealth()
            }
        }
        scope.launch { delay(4000); refreshHealth() }
    }

    suspend fun refreshStatus() {
        try {
            status = Api.get("/api/status", 20_000)
            statusAt = System.currentTimeMillis()
            if (phase != Phase.Ready) { phase = Phase.Ready; problem = "" }
        } catch (e: ApiError) {
            if (e.status == 401 && isPc) { phase = Phase.NeedsLogin; problem = e.message ?: "" }
        } catch (_: Exception) { /* keep the last status */ }
    }

    suspend fun refreshHealth() {
        try { health = Api.get("/api/models/health", 15_000).optJSONObject("models") ?: JSONObject() } catch (_: Exception) { }
    }

    val models: List<JSONObject> get() = status.arr("models").objects()
    val providers: List<JSONObject> get() = status.arr("providers").objects()
    val version: String get() = status.str("version")
}
