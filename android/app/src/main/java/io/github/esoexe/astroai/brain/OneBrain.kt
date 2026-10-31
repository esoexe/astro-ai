package io.github.esoexe.astroai.brain

import io.github.esoexe.astroai.AstroApp
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.clip
import io.github.esoexe.astroai.core.int
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.num
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject

/**
 * One Brain: every model thinks at the same time as ONE brain. With 3+ minds it works as a team: a lead writes a
 * short plan, members answer in roles, reviewers check the merged drafts and the strongest model writes the final
 * answer. In "All" mode (the default) every free model takes part and, with "wait for all" on, nobody is skipped
 * for being slow: only Stop, "Merge now" or a 20-minute safety limit end the thinking.
 * Same algorithm as the web app (web/app.js, runBrainDraft).
 */
class OneBrain(private val app: AstroApp, private val chat: ChatController) {
    private val prefs get() = app.prefs

    companion object {
        /** "One Brain · all 26 minds" for headers and pickers. */
        fun label(app: io.github.esoexe.astroai.AstroApp, voice: Boolean = false): String = try { tr("One Brain") + " · " + OneBrain(app, app.chat).sizeLabel(voice) } catch (_: Exception) { tr("One Brain") }
        private val CODE_RANK = listOf("deepseek-v4[^ ]*pro", "qwen3-coder-plus", "glm-5\\.3(?!-flash)", "deepseek-v4", "qwen3\\.8-max", "kimi-k2", "devstral", "minimax-m3(?!\\.1-flash)", "qwen3\\.5-397b", "nemotron-3-ultra", "qwen3-coder", "gpt-oss-120b", "codestral", "glm-5\\.3-flash", "minimax-m3", "qwen3\\.8", "llama-3[._]3-70b").map { Regex(it, RegexOption.IGNORE_CASE) }
        private val LEAD_RANK = listOf("glm-5\\.3(?!-flash)", "deepseek-v4", "qwen3\\.5-397b", "nemotron-3-ultra", "gpt-oss-120b", "minimax-m3", "glm-5\\.3-flash", "qwen3\\.[68]", "llama-3[._]3-70b", "mistral-small", "step-5", "nemotron-3\\.5-super", "muse", "gpt-oss-20b").map { Regex(it, RegexOption.IGNORE_CASE) }
        private val ROLES_CODE = listOf("Architect: design the structure, files and data flow, then write the solution.", "Engineer: write complete, working, runnable code.", "Tester: write the solution and point out bugs, edge cases and how to verify it works.",
            "Reviewer: write the solution with a focus on correctness, security and clean code.", "UX designer: write the solution with a focus on a polished, usable result.", "Simplifier: write the simplest solution that fully works.")
        private val ROLES_GENERAL = listOf("Analyst: break the problem down and reason it through step by step.", "Domain expert: give precise, expert-level facts and details.", "Skeptic: question assumptions, catch errors and say what is uncertain.",
            "Explainer: make the answer clear and easy to understand, with an example.", "Fact-checker: verify claims, numbers and dates; flag anything doubtful.", "Creative thinker: add useful ideas or angles others may miss.", "Practical advisor: focus on concrete, actionable steps.")
        /** Daily coding bench results (model_rank.json from the engine), loaded once. */
        @Volatile private var benchRank: JSONObject? = null
        private var benchLoading = false

        suspend fun loadBench() {
            if (benchRank != null || benchLoading) return
            benchLoading = true
            try { benchRank = Api.get("/model_rank.json", 15_000).optJSONObject("models") ?: JSONObject() } catch (_: Exception) { } finally { benchLoading = false }
        }
    }

    // ---------------- who thinks ----------------
    val all: Boolean get() = prefs.brainMinds == 0
    fun patient(voice: Boolean) = all && !voice && prefs.brainWaitAll
    fun wanted(voice: Boolean): Int { val n = prefs.brainMinds; if (n == 0) return Int.MAX_VALUE; return if (voice) minOf(3, n) else n }

    private val models: List<JSONObject> get() = app.backend.models
    private fun isLocal(m: JSONObject) = m.str("kind") == "local" || m.bool("local")
    private fun blocked(m: JSONObject): String = when (val v = m.opt("blocked")) { is String -> v; true -> "unavailable"; else -> "" }

    fun eligible(includeLocal: Boolean): List<JSONObject> {
        // Paid routes cost money on every message: only after the user opts in per platform, and then only Auto models.
        val paidOk = { m: JSONObject -> m.str("provider") != "astro-all" && (!(m.bool("paid") || m.str("provider").contains("paid")) || (m.bool("paid") && m.bool("auto"))) }
        if (prefs.brainLocalOnly) return models.filter { isLocal(it) }
        return models.filter { paidOk(it) && (includeLocal || !isLocal(it)) && (prefs.brainAllowTrain || !it.bool("trains")) }
    }

    fun pickMinds(n: Int): List<JSONObject> {
        val everyone = n == Int.MAX_VALUE
        val pat = everyone && patient(false)
        val ready = eligible(everyone).filter { pat || blocked(it).isEmpty() }
        val groups = LinkedHashMap<String, MutableList<JSONObject>>()
        for (m in ready) groups.getOrPut(m.str("provider")) { mutableListOf() }.add(m)
        for (list in groups.values) {
            list.sortByDescending { if (it.bool("auto")) 1 else 0 }
            val cap = list[0].int("parallel")
            if (!pat && cap > 0 && list.size > cap) while (list.size > cap) list.removeAt(list.size - 1)
        }
        val lists = groups.values.toList()
        val open = lists.filter { !it[0].bool("shared") && !isLocal(it[0]) }
        val shared = lists.filter { it[0].bool("shared") && !isLocal(it[0]) }
        val local = lists.filter { isLocal(it[0]) }
        val out = mutableListOf<JSONObject>()
        var round = 0
        while (out.size < n) {
            var added = false
            for (list in open) if (out.size < n && round < list.size) { out.add(list[round]); added = true }
            if (!added) break
            round++
        }
        if (everyone) { for (list in shared + local) out.addAll(list); return out }
        for (list in shared) if (out.size < n) out.add(list[0])
        return out
    }

    fun resting(): List<JSONObject> = if (all && !patient(false)) eligible(true).filter { blocked(it).isNotEmpty() } else emptyList()

    fun sizeLabel(voice: Boolean = false): String {
        val n = pickMinds(wanted(voice)).size
        return if (all) tr("all # minds", n) else tr("# minds", n)
    }

    // ---------------- ranking ----------------
    private fun healthPenalty(id: String): Int {
        val h = app.backend.health.optJSONObject(id) ?: return 0
        var p = 0
        val recent = System.currentTimeMillis() - h.long("last_at") < 30 * 60 * 1000
        if (recent && h.int("fails_in_row") >= 2) p += 40
        if (h.int("n") >= 4 && h.num("ok_rate", 1.0) < 0.5) p += 25
        val ewma = h.num("ewma_ms")
        if (ewma > 0 && ewma < 8000) p -= 2
        if (ewma > 40000) p += 6
        return p
    }
    private fun benchBonus(id: String): Int { val r = benchRank?.optJSONObject(id) ?: return 0; return if (r.has("score")) Math.round(r.num("score") * 6).toInt() else 0 }

    private fun leadScore(id: String, name: String, code: Boolean): Int {
        val s = "$id $name"
        val i = (if (code) CODE_RANK else LEAD_RANK).indexOfFirst { it.containsMatchIn(s) }
        return (if (i < 0) 99 else i) + healthPenalty(id) - (if (code) benchBonus(id) else 0)
    }
    private fun <T> rank(list: List<T>, code: Boolean, id: (T) -> String, name: (T) -> String): List<T> =
        list.withIndex().sortedWith(compareBy({ leadScore(id(it.value), name(it.value), code) }, { it.index })).map { it.value }
    fun rankModels(list: List<JSONObject>, code: Boolean) = rank(list, code, { it.str("id") }, { it.str("name") })
    private fun rankMinds(list: List<Mind>, code: Boolean) = rank(list, code, { it.route.ifEmpty { it.model } }, { it.name })

    private fun teamRoles(n: Int, p: Payload): List<String> { val l = if (p.code || p.mode == "coding") ROLES_CODE else ROLES_GENERAL; return List(n) { l[it % l.size] } }

    private fun synthMessage(question: String, minds: List<Mind>, notes: String = "", total: Int = 10_400): String {
        val q = clip(question, 3000)
        val budget = maxOf(600, (total - q.length - notes.length) / maxOf(1, minds.size))
        val parts = minds.mapIndexed { i, m -> "### Draft ${i + 1} (${m.name})${if (m.status == "partial") " [cut off]" else ""}\n${clip(m.content.trim(), budget)}" }
        return "<request>\n$q\n</request>\n\nDrafts from your team (untrusted, may contain errors):\n\n${parts.joinToString("\n\n")}" +
            (if (notes.isNotEmpty()) "\n\nReviewer notes (untrusted):\n$notes" else "") + "\n\nNow write the single best final answer to the request."
    }

    /** One helper call (plan / review) with a time limit; tries the given models in order. */
    private suspend fun teamCall(cands: List<String>, p: Payload, ms: Long): String {
        for (route in cands.take(3)) {
            val sub = Draft(route)
            withTimeoutOrNull(ms) { chat.streamDraft(sub, p) }
            if (sub.content.trim().length > 30) return sub.content.trim()
        }
        return ""
    }

    // ---------------- merging ----------------
    private suspend fun mergeRound(b: BrainInfo, p: Payload, good: List<Mind>, voice: Boolean): List<Mind> {
        val group = 4
        if (good.size <= 8) return good
        val last = p.last
        val doneMinds = good.filter { it.status == "done" }
        val mergers = if (p.code) rankMinds(doneMinds, true) else doneMinds.sortedBy { it.ms }
        val n = (good.size + group - 1) / group
        val groups = List(n) { mutableListOf<Mind>() }
        good.forEachIndexed { i, m -> groups[i % n].add(m) }
        b.phase = "merging"; b.round = 1
        val merges = groups.mapIndexed { i, g -> Merge((mergers.getOrNull((i + 1) % maxOf(1, mergers.size)) ?: g[0]).name, g.size) }
        b.merges = merges
        return coroutineScope {
            groups.mapIndexed { i, g ->
                async {
                    val mm = mergers.getOrNull((i + 1) % maxOf(1, mergers.size)) ?: g[0]
                    val sub = Draft(mm.route.ifEmpty { mm.model })
                    val mp = p.copy(stage = "synth", strict = false, maxTokens = if (voice) 320 else if (p.code) 4000 else 1100, messages = p.messages.dropLast(1) + ("user" to synthMessage(last, g, "", if (p.code) 20_000 else 10_400)))
                    chat.streamDraft(sub, mp) { merges[i].len = sub.content.length }
                    val ok = sub.status == "done" && sub.content.trim().length > 40
                    merges[i].status = if (ok) "done" else "failed"
                    if (ok) Mind(mm.model, "${mm.name} · merged ${g.size} minds", "").apply { status = "done"; content = sub.content; ms = sub.ms; route = sub.route }
                    else g.filter { it.status == "done" }.maxByOrNull { it.content.length } ?: g[0]
                }
            }.awaitAll()
        }
    }

    // ---------------- the brain ----------------
    suspend fun run(d: Draft, payload: Payload, voice: Boolean) = coroutineScope {
        loadBench()
        val want = wanted(voice)
        val pool = pickMinds(if (all) Int.MAX_VALUE else want + 4)
        if (pool.size < 2) { d.requestedNow = "auto"; chat.streamDraft(d, payload); return@coroutineScope }
        val first = if (all) pool else pool.take(want)
        val spares = ArrayDeque(if (all) emptyList() else pool.drop(want))
        val started = now()
        val pat = all && patient(voice)
        val team = !voice && first.size >= 3
        val b = BrainInfo(all, pat, team, first.size, started)
        d.brain = b
        val last = payload.last
        var teamMessages = payload.messages
        if (team) {
            val brief = teamCall(rankModels(first.filter { blocked(it).isEmpty() }, payload.code).map { it.str("id") }, payload.copy(stage = "plan", strict = false, maxTokens = 500), 45_000)
            if (brief.isNotEmpty()) teamMessages = payload.messages.dropLast(1) + ("user" to "$last\n\n<team_brief from=\"lead\">\n${clip(brief, 2500)}\n</team_brief>")
            b.phase = "minds"
        }
        val expert = payload.copy(messages = teamMessages, stage = if (team) "team" else "expert", roles = if (team) teamRoles(first.size, payload) else null, strict = true,
            maxTokens = if (voice) 360 else if (payload.code) 5000 else 1400, patient = pat)
        val graceMs = (if (voice) minOf(3, prefs.brainGrace) else prefs.brainGrace) * 1000L
        val hardMs = if (pat) 20 * 60 * 1000L else if (voice) 25_000L else if (all) 50_000L else 70_000L

        val finished = CompletableDeferred<Unit>()
        val jobs = mutableListOf<Pair<Mind, Job>>()
        var pending = 0
        var graceJob: Job? = null
        fun finish() {
            if (finished.isCompleted) return
            finished.complete(Unit)
            graceJob?.cancel()
            for ((mind, j) in jobs) if (mind.status == "thinking" || mind.status == "writing") { mind.cut = true; j.cancel() }
        }
        b.finishNow = { finish() }
        val hard = launch { delay(hardMs); finish() }

        lateinit var launchOne: (JSONObject) -> Unit
        fun settle(m: JSONObject, mind: Mind, sub: Draft) {
            pending--
            mind.ms = sub.ms; mind.content = sub.content; mind.route = sub.route.ifEmpty { m.str("id") }
            if (sub.status == "done") mind.status = "done"
            else if (mind.cut && sub.content.length > 200) mind.status = "partial"
            else {
                mind.status = if (mind.cut) "cut" else "failed"
                mind.error = if (mind.cut) tr("too slow, skipped") else clip(sub.error.ifEmpty { "failed" }, 160)
                val alive = b.minds.count { it.status in setOf("thinking", "writing", "done") }
                if (!finished.isCompleted && !mind.cut && spares.isNotEmpty() && alive < want && now() - started < (if (voice) 8000 else 20_000)) launchOne(spares.removeFirst())
            }
            if (mind.status == "done" && d.early == null && prefs.brainFastFirst && mind.content.trim().length > 40) d.early = mind.name to mind.content
            if (mind.status == "done" && graceJob == null && !finished.isCompleted && !pat) {
                // All mode: every mind gets at least 30 s before slow ones are skipped.
                val graceEnd = maxOf(now() + graceMs, if (all && !voice) started + 30_000 else 0L)
                graceJob = launch {
                    delay(graceMs)
                    while (isActive && !finished.isCompleted) {
                        val writing = b.minds.any { it.status == "writing" }
                        val voiceDone = voice && now() >= graceEnd + 4000
                        if (now() >= graceEnd && (!writing || voiceDone)) { finish(); break }
                        delay(700)
                    }
                }
            }
            if (pending == 0) finish()
        }
        fun makeMind(m: JSONObject): Pair<Mind, Draft> {
            val mind = Mind(m.str("id"), m.str("name").ifEmpty { m.str("label") }, m.str("provider_name"))
            b.minds.add(mind)
            pending++
            return mind to Draft(m.str("id"))
        }
        fun onUpdate(mind: Mind, sub: Draft) {
            mind.content = sub.content
            mind.note = if (sub.content.isEmpty()) sub.notice else ""
            if (sub.content.isNotEmpty() && mind.status == "thinking") mind.status = "writing"
        }
        // A spare (replacing a failed mind) uses its own request.
        launchOne = { m ->
            val (mind, sub) = makeMind(m)
            val j = launch {
                try { chat.streamDraft(sub, expert) { onUpdate(mind, sub) } }
                catch (_: CancellationException) { }
                finally { withContext(NonCancellable) { settle(m, mind, sub) } }
            }
            jobs.add(mind to j)
        }
        // The first batch shares ONE multiplexed request, so every mind really runs at the same time.
        fun launchMany(list: List<JSONObject>) {
            val entries = list.map { m -> Triple(m, makeMind(m).first, Draft(m.str("id"))) }
            val closed = BooleanArray(entries.size)
            val errors = Array(entries.size) { "" }
            val done = BooleanArray(entries.size)
            fun close(i: Int, aborted: Boolean) {
                if (closed[i]) return
                closed[i] = true
                val (m, mind, sub) = entries[i]
                sub.ms = now() - sub.started
                when {
                    sub.content.isNotEmpty() && done[i] -> sub.status = "done"
                    sub.content.isNotEmpty() -> { sub.status = "stopped"; sub.error = errors[i].ifEmpty { "interrupted" } }
                    else -> { sub.status = "error"; sub.error = errors[i].ifEmpty { if (aborted) "Stopped" else tr("The model returned an empty answer.") } }
                }
                settle(m, mind, sub)
            }
            val ids = JSONArray().also { a -> entries.forEach { a.put(it.first.str("id")) } }
            val body = expert.toJson().put("models", ids)
            val j = launch {
                var aborted = false
                try {
                    Api.sse("/api/brain/stream", body).collect { msg ->
                        val i = msg.optInt("mind", -1)
                        if (i < 0 || i >= entries.size || closed[i]) return@collect
                        val (_, mind, sub) = entries[i]
                        if (msg.bool("end")) { close(i, false); return@collect }
                        val ev = msg.optJSONObject("ev") ?: return@collect
                        val delta = ev.opt("delta")
                        when {
                            delta is String -> { sub.content = clip(sub.content + delta, 60_000); onUpdate(mind, sub) }
                            ev.has("thinking") -> Unit
                            ev.opt("notice") is String -> { sub.notice = ev.getString("notice"); onUpdate(mind, sub) }
                            ev.has("error") && ev.opt("error") != false -> errors[i] = ev.str("error")
                            ev.bool("done") -> { done[i] = true; sub.model = ev.str("model", 200); sub.route = ev.str("route", 200) }
                        }
                    }
                } catch (_: CancellationException) { aborted = true }
                catch (e: Exception) { for (k in errors.indices) if (errors[k].isEmpty()) errors[k] = e.message ?: "Connection failed" }
                finally { withContext(NonCancellable) { entries.indices.forEach { close(it, aborted) } } }
            }
            entries.forEach { jobs.add(it.second to j) }
        }

        if (first.size > 1) launchMany(first) else first.forEach { launchOne(it) }
        // Models that are resting (daily limit, outage) are shown so the whole brain is visible.
        for (m in resting()) b.minds.add(Mind(m.str("id"), m.str("name").ifEmpty { m.str("label") }, m.str("provider_name")).apply { status = "resting"; error = clip(blocked(m).ifEmpty { "resting" }, 160) })

        finished.await()
        hard.cancel(); graceJob?.cancel()
        withTimeoutOrNull(5000) { jobs.map { it.second }.distinct().joinAll() }

        val good = b.minds.filter { (it.status == "done" || it.status == "partial") && it.content.isNotBlank() }
        if (good.isEmpty()) {
            b.phase = "done"; b.single = true
            d.notice = tr("No mind of the brain could answer, so A.S.T.R.O AI switched to the fastest available route")
            d.requestedNow = "auto"
            chat.streamDraft(d, payload)
            return@coroutineScope
        }
        if (good.size == 1) {
            b.phase = "done"; b.single = true; b.ms = now() - started
            d.content = good[0].content; d.model = good[0].name; d.route = good[0].route; d.status = "done"; d.ms = b.ms
            return@coroutineScope
        }
        val sorted = good.sortedBy { it.ms }
        val codeReq = payload.code
        val merger = rankMinds(sorted.filter { it.status == "done" }, codeReq).firstOrNull() ?: sorted[0]
        var inputs = mergeRound(b, payload, sorted, voice)
        while (inputs.size > 8) inputs = mergeRound(b, payload, inputs, voice)
        val total = if (codeReq) 22_000 else 14_000
        var notes = ""
        if (team && inputs.size >= 2) {
            b.phase = "review"
            val reviewers = rankMinds(sorted.filter { it.status == "done" && it !== merger }, codeReq).take(2)
            val rp = payload.copy(stage = "review", strict = false, maxTokens = if (codeReq) 900 else 700, messages = payload.messages.dropLast(1) + ("user" to synthMessage(last, inputs, "", total)))
            val got = reviewers.map { r -> async { teamCall(listOf(r.route.ifEmpty { r.model }), rp, 90_000) } }.awaitAll()
            notes = got.filter { it.isNotEmpty() }.mapIndexed { i, x -> "### Reviewer ${i + 1}\n${clip(x, 2500)}" }.joinToString("\n\n")
        }
        b.phase = "final"; b.round = if (b.merges != null) 2 else 0; b.mergedBy = merger.name
        val synth = payload.copy(stage = "synth", strict = false, maxTokens = if (voice) 420 else if (payload.code) 8000 else null,
            messages = payload.messages.dropLast(1) + ("user" to synthMessage(last, inputs, notes, total)))
        d.requestedNow = merger.route.ifEmpty { merger.model }
        d.content = ""
        val fin = Draft(d.requestedNow)
        chat.streamDraft(fin, synth) { d.content = fin.content }
        d.content = fin.content; d.status = fin.status; d.error = fin.error; d.model = fin.model; d.route = fin.route; d.provider = fin.provider; d.truncated = fin.truncated
        takeConfidence(d)
        d.early = null
        b.ms = now() - started
        // Code: a failed merge gets one retry with the next-best coder (2-minute cap) before falling back.
        val backup = if (codeReq && d.status == "error") rankMinds(sorted.filter { it.status == "done" && it !== merger }, true).firstOrNull() else null
        if (backup != null) {
            val again = Draft(backup.route.ifEmpty { backup.model })
            b.mergedBy = backup.name
            withTimeoutOrNull(120_000) { chat.streamDraft(again, synth) { d.content = again.content } }
            if (again.status == "done" && again.content.isNotBlank()) {
                d.content = again.content; d.status = "done"; d.error = ""; d.model = again.model; d.route = again.route; d.provider = again.provider
                takeConfidence(d)
            }
        }
        if (d.status == "error") {
            val best = if (codeReq && merger.status == "done" && merger.content.isNotEmpty()) merger
                else inputs.fold(inputs[0]) { a, m -> if (m.status == "done" && m.content.length > a.content.length) m else a }
            d.content = best.content; d.status = "done"; d.error = ""; d.model = best.name
            b.single = true
            d.notice = tr("The final merge failed, so this is the strongest merged or single answer")
        }
        b.phase = "done"
        noteTime(b.ms)
    }

    private fun takeConfidence(d: Draft) {
        val c = Text.confidenceOf(d.content)
        if (c.isNotEmpty()) d.confidence = c
        d.content = Text.stripConfidence(Text.stripReasoningLeak(d.content)).trimEnd()
    }

    // Past One Brain answer times (last 20), so the wait shows an honest estimate.
    fun times(): List<Long> = try { val a = JSONArray(prefs.brainTimes); (0 until a.length()).map { a.getLong(it) }.filter { it in 501 until 20 * 60_000 } } catch (_: Exception) { emptyList() }
    private fun noteTime(ms: Long) { if (ms > 500) prefs.brainTimes = JSONArray((times() + ms).takeLast(20)).toString() }
}
