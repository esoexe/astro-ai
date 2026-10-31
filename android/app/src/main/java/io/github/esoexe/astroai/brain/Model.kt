package io.github.esoexe.astroai.brain

import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import io.github.esoexe.astroai.core.Store
import io.github.esoexe.astroai.core.arr
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.clip
import io.github.esoexe.astroai.core.int
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.makeId
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.strings
import org.json.JSONObject

fun now(): Long = SystemClock.elapsedRealtime()

/** One model of the One Brain while it thinks. */
class Mind(val model: String, val name: String, val provider: String) {
    var status by mutableStateOf("thinking")     // thinking, writing, done, partial, failed, cut, resting
    var content by mutableStateOf("")
    var note by mutableStateOf("")
    var ms by mutableLongStateOf(0L)
    var error = ""
    var route = ""
    var cut = false
    fun toJson() = jsonOf("name" to name, "provider" to provider, "status" to status, "ms" to ms, "content" to clip(content, 4000), "error" to error)
}

class Merge(val name: String, val size: Int) {
    var status by mutableStateOf("writing")
    var len by mutableIntStateOf(0)
}

class BrainInfo(val all: Boolean, val patient: Boolean, val team: Boolean, val total: Int, val started: Long = now()) {
    var phase by mutableStateOf(if (team) "plan" else "minds")   // plan, minds, merging, review, final, done
    val minds: SnapshotStateList<Mind> = mutableStateListOf()
    var merges by mutableStateOf<List<Merge>?>(null)
    var round by mutableIntStateOf(0)
    var mergedBy by mutableStateOf("")
    var ms by mutableLongStateOf(0L)
    var single by mutableStateOf(false)
    /** "Merge now": stop waiting for slow minds. */
    var finishNow: (() -> Unit)? = null
}

class ActionCard(
    val tool: String,
    val text: String,
    val sub: String = "",
    status: String,
    val undo: (() -> Unit)? = null,
    val run: (() -> String)? = null,
    val confirmLabel: String = "Confirm",
    val danger: Boolean = true,
    val url: String = "",
) {
    var status by mutableStateOf(status)     // done, pending, failed, cancelled, undone
    var shownText by mutableStateOf(text)
}

class Draft(val requested: String, val rewrite: String = "") {
    val id = makeId()
    var started = now()
    var content by mutableStateOf("")
    var thinking = ""
    var notice by mutableStateOf("")
    var status by mutableStateOf("streaming")   // streaming, done, stopped, error
    var model by mutableStateOf("")
    var route = ""
    var provider = ""
    var ms by mutableLongStateOf(0L)
    var error by mutableStateOf("")
    var brain by mutableStateOf<BrainInfo?>(null)
    val actions: SnapshotStateList<ActionCard> = mutableStateListOf()
    var confidence by mutableStateOf("")
    var truncated = false
    /** First finished mind, shown while the brain keeps working. */
    var early by mutableStateOf<Pair<String, String>?>(null)
    /** Background code check result (after the answer). */
    var check by mutableStateOf<String?>(null)
    var requestedNow = requested

    fun toJson(): JSONObject = jsonOf(
        "content" to content, "notice" to notice, "model" to model, "route" to route, "provider" to provider, "ms" to ms,
        "thinking" to "", "thinkingMs" to 0, "status" to status, "error" to error, "rewrite" to rewrite,
        "brain" to brain?.let { b -> jsonOf("mergedBy" to b.mergedBy, "ms" to b.ms, "single" to b.single, "minds" to b.minds.map { it.toJson() }) },
        "actions" to actions.map { jsonOf("tool" to it.tool, "text" to it.shownText, "sub" to it.sub, "status" to it.status, "url" to it.url) },
        "confidence" to confidence.ifEmpty { null },
    )

    companion object {
        fun from(o: JSONObject): Draft = Draft(o.str("model").ifEmpty { "auto" }, o.str("rewrite")).apply {
            content = o.str("content", 60_000); notice = o.str("notice"); model = o.str("model", 200); route = o.str("route", 200)
            provider = o.str("provider", 100); ms = o.long("ms"); error = o.str("error")
            status = o.str("status").takeIf { it in setOf("done", "stopped", "error") } ?: "done"
            confidence = o.str("confidence")
            o.optJSONObject("brain")?.let { bj ->
                val b = BrainInfo(all = false, patient = false, team = false, total = 0)
                b.phase = "done"; b.mergedBy = bj.str("mergedBy"); b.ms = bj.long("ms"); b.single = bj.bool("single")
                bj.arr("minds").objects().forEach { m -> b.minds.add(Mind("", m.str("name"), m.str("provider")).apply { status = m.str("status"); ms = m.long("ms"); content = m.str("content"); error = m.str("error") }) }
                brain = b
            }
            o.arr("actions").objects().forEach { a -> actions.add(ActionCard(a.str("tool"), a.str("text"), a.str("sub"), a.str("status").let { if (it == "pending") "cancelled" else it }, url = a.str("url"))) }
        }
    }
}

class Attachment(val name: String, val kind: String, val text: String = "", val chars: Int = 0)

class Msg(val role: String, content: String = "") {
    var content by mutableStateOf(content)
    val files: SnapshotStateList<String> = mutableStateListOf()
    /** data: URL of an attached photo, until a vision model has described it. */
    var image: String? = null
    var imageName = ""
    var imageText = ""
    val drafts: SnapshotStateList<Draft> = mutableStateListOf()
    var active by mutableIntStateOf(0)
    val sources: SnapshotStateList<String> = mutableStateListOf()
    var feedback by mutableStateOf<String?>(null)
    var compare by mutableStateOf(false)

    val current: Draft? get() = drafts.getOrNull(active)
    val text: String get() = if (role == "user") content else current?.content ?: ""

    fun toJson(): JSONObject? {
        if (role == "user") return jsonOf("role" to "user", "content" to content, "files" to files.toList(),
            "imageName" to imageName.ifEmpty { null }, "imageText" to imageText.ifEmpty { null })
        val kept = drafts.filter { it.status != "streaming" && (it.content.isNotEmpty() || it.status == "error") }
        if (kept.isEmpty()) return null
        val act = kept.indexOf(drafts.getOrNull(active)).coerceAtLeast(0).coerceAtMost(kept.size - 1)
        return jsonOf("role" to "assistant", "content" to kept[act].content, "drafts" to kept.map { it.toJson() }, "active" to act,
            "sources" to sources.toList(), "feedback" to feedback)
    }

    companion object {
        fun from(o: JSONObject): Msg? {
            val role = o.str("role")
            if (role == "user") return Msg("user", o.str("content", Store.LIMITS.getValue("draftChars"))).apply {
                files.addAll(o.arr("files").strings()); imageName = o.str("imageName"); imageText = o.str("imageText", 6000)
            }
            if (role != "assistant") return null
            val m = Msg("assistant")
            val ds = o.arr("drafts").objects()
            if (ds.isEmpty()) m.drafts.add(Draft("auto").apply { content = o.str("content"); status = "done" })
            else ds.take(Store.LIMITS.getValue("drafts")).forEach { m.drafts.add(Draft.from(it)) }
            m.active = o.int("active").coerceIn(0, m.drafts.size - 1)
            m.sources.addAll(o.arr("sources").strings().take(10))
            m.feedback = o.str("feedback").ifEmpty { null }
            return m
        }
    }
}

/** What goes to /api/chat/stream (same fields as the web app). */
data class Payload(
    val messages: List<Pair<String, String>>,
    val context: String = "",
    val mode: String = "general",
    val language: String = "auto",
    val agent: Boolean = true,
    val workspace: String = "",
    val voice: Boolean = false,
    val code: Boolean = false,
    val stage: String? = null,
    val strict: Boolean? = null,
    val maxTokens: Int? = null,
    val roles: List<String>? = null,
    val patient: Boolean? = null,
) {
    val last: String get() = messages.lastOrNull()?.second ?: ""

    fun withLast(content: String) = copy(messages = messages.dropLast(1) + ("user" to content))

    fun toJson(model: String? = null, localOnly: Boolean = false, models: List<String>? = null): JSONObject = jsonOf(
        "messages" to messages.map { jsonOf("role" to it.first, "content" to it.second) },
        "context" to context, "mode" to mode, "language" to language, "agent" to agent, "workspace" to workspace,
        "voice" to voice, "code" to code, "stage" to stage, "strict" to strict, "max_tokens" to maxTokens, "roles" to roles,
        "patient" to patient, "model" to model, "local_only" to localOnly, "models" to models,
    )
}
