package io.github.esoexe.astroai.brain

import io.github.esoexe.astroai.AstroApp
import io.github.esoexe.astroai.core.MemoryItem
import io.github.esoexe.astroai.core.Note
import io.github.esoexe.astroai.core.Reminder
import io.github.esoexe.astroai.core.TaskItem
import io.github.esoexe.astroai.core.arr
import io.github.esoexe.astroai.core.clip
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import org.json.JSONObject
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.time.OffsetDateTime
import java.time.LocalDateTime
import java.time.ZoneId
import java.util.Date
import java.util.Locale

/**
 * Things the assistant can do in the app when it writes an ```actions block: notes, tasks, memory, reminders,
 * settings, web search and reading. Deleting asks first; most actions can be undone. Read actions (search,
 * read_note, web_search...) feed their results back so the assistant can answer with them (up to 2 rounds).
 */
class Actions(private val app: AstroApp, private val chat: ChatController) {
    private val store get() = app.store
    private val prefs get() = app.prefs

    class Result(val text: String, val sub: String = "", val undo: (() -> Unit)? = null, val confirm: (() -> String)? = null,
                 val confirmLabel: String = "Confirm", val danger: Boolean = true, val url: String = "", val result: String? = null)

    private fun lower(s: String?) = (s ?: "").trim().lowercase()
    private fun s(a: JSONObject, vararg keys: String, max: Int = 50_000): String { for (k in keys) { val v = a.str(k, max); if (v.isNotEmpty()) return v }; return "" }

    private fun <T> findByTitle(list: List<T>, title: String, key: (T) -> String): T? {
        val q = lower(title)
        if (q.isEmpty()) return null
        return list.firstOrNull { lower(key(it)) == q } ?: list.firstOrNull { lower(key(it)).contains(q) } ?: list.firstOrNull { lower(key(it)).length > 2 && q.contains(lower(key(it))) }
    }

    private fun when_(ts: Long): String = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(ts))
    private fun formatDue(v: String): String = try { DateFormat.getDateInstance(DateFormat.MEDIUM).format(SimpleDateFormat("yyyy-MM-dd", Locale.US).parse(v)!!) } catch (_: Exception) { v }

    private fun parseTime(s: String): Long {
        if (s.isBlank()) return 0
        return try { OffsetDateTime.parse(s).toInstant().toEpochMilli() } catch (_: Exception) {
            try { LocalDateTime.parse(s.removeSuffix("Z")).atZone(ZoneId.systemDefault()).toInstant().toEpochMilli() } catch (_: Exception) { 0 }
        }
    }

    private suspend fun run(tool: String, a: JSONObject, voice: Boolean): Result = when (tool) {
        "create_note" -> {
            val title = clip(s(a, "title"), 100).ifEmpty { "Untitled note" }
            val note = Note(title = title, body = clip(s(a, "body", "text", "content"), 50_000))
            store.notes.add(0, note); store.saveNotes()
            Result(tr("Created note “#”", title), clip(note.body.replace(Regex("\\s+"), " "), 100), undo = { store.notes.remove(note); store.saveNotes() })
        }
        "append_note" -> {
            val note = findByTitle(store.notes, s(a, "title")) { it.title }
            if (note == null) run("create_note", jsonOf("title" to s(a, "title"), "body" to s(a, "text", "body")), voice) else {
                val before = note.body
                val i = store.notes.indexOf(note)
                val updated = note.copy(body = clip(note.body + (if (note.body.isNotEmpty()) "\n\n" else "") + clip(s(a, "text", "body"), 20_000), 50_000), updatedAt = System.currentTimeMillis())
                store.notes[i] = updated; store.saveNotes()
                Result(tr("Added to note “#”", note.title.ifEmpty { "Untitled" }), clip(s(a, "text", "body"), 100), undo = { val j = store.notes.indexOfFirst { it.id == note.id }; if (j >= 0) { store.notes[j] = updated.copy(body = before); store.saveNotes() } })
            }
        }
        "delete_note" -> {
            val note = findByTitle(store.notes, s(a, "title")) { it.title } ?: throw IllegalStateException(tr("No note called “#”", s(a, "title")))
            Result(tr("Delete note “#”?", note.title.ifEmpty { "Untitled" }), confirmLabel = "Delete", confirm = { store.notes.remove(note); store.saveNotes(); tr("Deleted note “#”", note.title.ifEmpty { "Untitled" }) })
        }
        "create_task" -> {
            val title = clip(s(a, "title", "text"), 180).trim()
            if (title.isEmpty()) throw IllegalStateException(tr("The task needs a title"))
            val due = s(a, "due").takeIf { Regex("\\d{4}-\\d{2}-\\d{2}").matches(it) } ?: ""
            val pr = s(a, "priority").takeIf { it in setOf("low", "normal", "high") } ?: "normal"
            val task = TaskItem(title = title, due = due, priority = pr)
            store.tasks.add(0, task); store.saveTasks()
            Result(tr("Added task “#”", title), listOf(if (pr != "normal") tr("# priority", pr) else "", if (due.isNotEmpty()) tr("due #", formatDue(due)) else "").filter { it.isNotEmpty() }.joinToString(" · "),
                undo = { store.tasks.removeAll { it.id == task.id }; store.saveTasks() })
        }
        "complete_task" -> {
            val task = findByTitle(store.tasks.filter { !it.done }, s(a, "title")) { it.title } ?: findByTitle(store.tasks, s(a, "title")) { it.title }
                ?: throw IllegalStateException(tr("No task matching “#”", s(a, "title")))
            val i = store.tasks.indexOf(task); val was = task.done
            store.tasks[i] = task.copy(done = true); store.saveTasks()
            Result(tr("Completed “#”", task.title), undo = { val j = store.tasks.indexOfFirst { it.id == task.id }; if (j >= 0) { store.tasks[j] = store.tasks[j].copy(done = was); store.saveTasks() } })
        }
        "delete_task" -> {
            val task = findByTitle(store.tasks, s(a, "title")) { it.title } ?: throw IllegalStateException(tr("No task matching “#”", s(a, "title")))
            Result(tr("Delete task “#”?", task.title), confirmLabel = "Delete", confirm = { store.tasks.removeAll { it.id == task.id }; store.saveTasks(); tr("Deleted task “#”", task.title) })
        }
        "remember" -> {
            val fact = clip(s(a, "fact", "text"), 300).trim()
            if (fact.isEmpty()) throw IllegalStateException(tr("Nothing to remember"))
            val item = MemoryItem(fact = fact)
            store.memory.removeAll { lower(it.fact) == lower(fact) }
            store.memory.add(0, item)
            while (store.memory.size > 80) store.memory.removeAt(store.memory.size - 1)
            store.saveMemory()
            Result(tr("Saved to memory"), fact, undo = { store.memory.removeAll { it.id == item.id }; store.saveMemory() })
        }
        "forget" -> {
            val q = lower(s(a, "fact", "text"))
            val removed = if (q.isNotEmpty()) store.memory.filter { lower(it.fact).contains(q) || q.contains(lower(it.fact)) } else emptyList()
            if (removed.isEmpty()) throw IllegalStateException(tr("Nothing matching in memory"))
            store.memory.removeAll(removed); store.saveMemory()
            Result(tr("Forgot # memory item(s)", removed.size), removed.joinToString("; ") { it.fact }, undo = { store.memory.addAll(0, removed); store.saveMemory() })
        }
        "set_reminder" -> {
            var at = 0L
            val minutes = a.opt("minutes")?.toString()?.toDoubleOrNull()
            if (minutes != null && minutes.isFinite()) at = System.currentTimeMillis() + (maxOf(0.1, minutes) * 60_000).toLong()
            else if (s(a, "at").isNotEmpty()) at = parseTime(s(a, "at"))
            if (at == 0L || at < System.currentTimeMillis() - 60_000) throw IllegalStateException(tr("The reminder time is unclear"))
            val r = Reminder(text = clip(s(a, "text", "title"), 200).ifEmpty { "Reminder" }, at = at)
            store.reminders.add(r); store.saveReminders()
            app.reminders.askPermission()
            Result(tr("Reminder: “#”", r.text), tr("# · a notification on this phone", when_(at)), undo = { store.reminders.removeAll { it.id == r.id }; store.saveReminders() })
        }
        "set_theme" -> {
            val t = lower(s(a, "theme"))
            if (t !in setOf("light", "dark", "system")) throw IllegalStateException(tr("Unknown theme"))
            val prev = prefs.theme; prefs.theme = t
            Result(tr("Theme set to #", t), undo = { prefs.theme = prev })
        }
        "set_mode" -> {
            val m = lower(s(a, "mode")).let { if (it == "code") "coding" else it }
            if (m !in setOf("general", "writing", "tutor", "coding", "planning", "research")) throw IllegalStateException(tr("Unknown mode"))
            val prev = prefs.mode; prefs.mode = m
            Result(tr("Switched to # mode", m), undo = { prefs.mode = prev })
        }
        "set_language" -> {
            val l = mapOf("english" to "English", "french" to "French", "arabic" to "Arabic", "auto" to "auto")[lower(s(a, "language"))] ?: throw IllegalStateException(tr("Unknown language"))   // i18n-ignore: values for the engine
            val prev = prefs.language; prefs.language = l
            Result(tr("Response language: #", l), undo = { prefs.language = prev })
        }
        "open" -> {
            val v = lower(s(a, "view"))
            val target = mapOf("chat" to "chat", "assistant" to "chat", "notes" to "notes", "tasks" to "tasks", "knowledge" to "knowledge", "calculator" to "calculator",
                "lab" to "lab", "reader" to "reader", "agent" to "agent", "workspace" to "workspace", "settings" to "settings", "providers" to "settings:providers", "live" to "live")[v]
                ?: throw IllegalStateException(tr("Unknown view “#”", v))
            if (voice && v != "live") Result(tr("Open #", v), confirmLabel = "Open", danger = false, confirm = { app.ui.closeLive(); app.ui.go(target); tr("Opened #", v) })
            else { app.ui.goSoon(target); Result(tr("Opened #", v)) }
        }
        "new_chat" -> Result(tr("Start a new chat?"), confirmLabel = "Start", danger = false, confirm = { app.ui.post { chat.newChat() }; tr("Started a new chat") })
        "export_chat" -> { app.ui.shareText("chat.md", chat.markdown()); Result(tr("Exported this chat as Markdown")) }
        "open_url" -> {
            val u = s(a, "url").trim()
            if (!u.startsWith("https://", ignoreCase = true)) throw IllegalStateException(tr("Only https links can be opened"))
            Result(tr("Link ready"), u, url = u)
        }
        "speak" -> { val t = clip(s(a, "text"), 1000); if (t.isNotEmpty() && !voice) app.speech.speak(t); Result(tr("Spoke aloud"), clip(t, 100)) }
        "calculate" -> {
            val expr = clip(s(a, "expression"), 300)
            val j = Api.post("/api/calc", jsonOf("expression" to expr), 30_000)
            Result("$expr = ${j.str("result")}", tr("Exact calculator"), result = j.str("result"))
        }
        "search" -> {
            val q = lower(s(a, "query"))
            if (q.isEmpty()) throw IllegalStateException(tr("Empty search"))
            val lines = mutableListOf<String>()
            for (c in store.conversations) {
                val msgs = c.arr("messages").objects()
                val hit = msgs.firstOrNull { lower(it.str("content")).contains(q) }
                if (lower(c.str("title")).contains(q) || hit != null) {
                    val snippet = hit?.str("content")?.let { t -> val i = lower(t).indexOf(q); ": …" + t.substring(maxOf(0, i - 80), minOf(t.length, i + 160)).replace(Regex("\\s+"), " ") + "…" } ?: ""
                    lines.add("Chat “${c.str("title")}”$snippet")
                }
                if (lines.size >= 6) break
            }
            for (n in store.notes) if (lower("${n.title} ${n.body}").contains(q)) lines.add("Note “${n.title.ifEmpty { "Untitled" }}”: ${clip(n.body.replace(Regex("\\s+"), " "), 200)}")
            for (t in store.tasks) if (lower(t.title).contains(q)) lines.add("Task “${t.title}” (${if (t.done) "done" else "open"}${if (t.due.isNotEmpty()) ", due ${t.due}" else ""})")
            Result(tr("Searched your workspace for “#”", s(a, "query")), tr("# result(s)", lines.size), result = lines.take(14).joinToString("\n").ifEmpty { "No matches." })
        }
        "read_note" -> {
            val note = findByTitle(store.notes, s(a, "title")) { it.title }
            if (note == null) Result(tr("No note called “#”", s(a, "title")), result = "Not found. Notes: ${store.notes.joinToString(", ") { it.title.ifEmpty { "Untitled" } }.ifEmpty { "none" }}")
            else Result(tr("Read note “#”", note.title.ifEmpty { "Untitled" }), result = "# ${note.title.ifEmpty { "Untitled" }}\n${clip(note.body, 6000)}")
        }
        "list_tasks" -> {
            val f = lower(s(a, "filter")).ifEmpty { "open" }
            val list = store.tasks.filter { f == "all" || (if (f == "done") it.done else !it.done) }
            Result(tr("Checked # tasks", f), tr("# task(s)", list.size), result = list.take(40).joinToString("\n") { "- ${it.title}${if (it.done) " (done)" else ""}${if (it.priority != "normal") " [${it.priority}]" else ""}${if (it.due.isNotEmpty()) " due ${it.due}" else ""}" }.ifEmpty { "No tasks." })
        }
        "list_notes" -> Result(tr("Checked your notes"), tr("# note(s)", store.notes.size), result = store.notes.take(40).joinToString("\n") { "- ${it.title.ifEmpty { "Untitled" }} (${it.body.length} chars)" }.ifEmpty { "No notes." })
        "web_search" -> {
            val q = clip(s(a, "query"), 300).trim()
            if (q.isEmpty()) throw IllegalStateException(tr("Empty search"))
            val j = Api.post("/api/web/search", jsonOf("query" to q, "count" to 6), 60_000)
            val list = j.arr("results").objects()
            Result(tr("Searched the web for “#”", q), tr("# result(s) · #", list.size, j.str("engine").ifEmpty { "web" }),
                result = list.mapIndexed { i, r -> "[${i + 1}] ${r.str("title")}\n${r.str("url")}\n${r.str("snippet")}" }.joinToString("\n\n").ifEmpty { "No results." })
        }
        "read_url" -> {
            val u = s(a, "url").trim()
            if (!Regex("^https?://", RegexOption.IGNORE_CASE).containsMatchIn(u)) throw IllegalStateException(tr("Give a full http(s) address"))
            val j = Api.post("/api/web/read", jsonOf("url" to u, "max_chars" to 12_000), 60_000)
            Result(tr("Read “#”", j.str("title").ifEmpty { Text.hostOf(u) }), u, result = "# ${j.str("title").ifEmpty { u }}\nSource: ${j.str("url").ifEmpty { u }}\n\n${clip(j.str("text"), 9000)}")
        }
        "create_file" -> {
            val name = clip(s(a, "name", "filename").ifEmpty { "astro-file.txt" }, 120).replace(Regex("[\\\\/:*?\"<>|]+"), "-").ifEmpty { "astro-file.txt" }
            val content = clip(s(a, "content", "text", "body", max = 2_000_000), 2_000_000)
            app.ui.shareText(name, content)
            Result(tr("Created file “#”", name), tr("# characters · share or save it", content.length))
        }
        "open_reader" -> {
            val u = s(a, "url").trim()
            if (!Regex("^https?://", RegexOption.IGNORE_CASE).containsMatchIn(u)) throw IllegalStateException(tr("Give a full http(s) address"))
            app.ui.readerUrl = u; app.ui.goSoon("reader")
            Result(tr("Opened in Reader"), u)
        }
        else -> throw IllegalStateException(tr("Unknown action “#”", tool))
    }

    suspend fun process(msg: Msg, d: Draft, payload: Payload, voice: Boolean, depth: Int = 0) {
        val ex = Text.extractActions(d.content)
        d.content = ex.clean
        if (ex.actions.isEmpty()) return
        val reads = mutableListOf<String>()
        for (raw in ex.actions) {
            val tool = lower(raw.str("tool"))
            val args = raw.optJSONObject("args")?.let { extra -> JSONObject(raw.toString()).also { o -> extra.keys().forEach { k -> o.put(k, extra.get(k)) } } } ?: raw
            try {
                val r = run(tool, args, voice)
                d.actions.add(ActionCard(tool, r.text, r.sub, if (r.confirm != null) "pending" else "done", r.undo, r.confirm, r.confirmLabel, r.danger, r.url))
                if (r.result != null) reads.add("$tool ${JSONObject.quote(s(args, "query", "url", "title", "expression", "filter"))}:\n${r.result}")
            } catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (e: Exception) {
                d.actions.add(ActionCard(tool, tr("Couldn't #", tool.replace('_', ' ')), e.message ?: "", "failed"))
            }
        }
        if (reads.isEmpty() || depth >= 2) return
        val base = d.content
        val sub = Draft("auto")
        d.status = "streaming"
        val follow = payload.copy(stage = null, strict = false, maxTokens = if (voice) 400 else null, workspace = if (prefs.agentTools) chat.workspaceSnapshot(voice) else "",
            messages = (payload.messages + ("assistant" to clip(base.ifEmpty { "Let me check your workspace." }, 6000)) +
                ("user" to "[A.S.T.R.O AI action results]\n${clip(reads.joinToString("\n\n"), 9000)}\n\nUsing these results, answer my previous message now. Do not repeat these read actions.")).takeLast(24))
        chat.streamDraft(sub, follow) { d.content = base + (if (base.isNotEmpty()) "\n\n" else "") + sub.content }
        d.content = base + (if (base.isNotEmpty() && sub.content.isNotEmpty()) "\n\n" else "") + sub.content
        d.status = "done"
        if (sub.status == "error" && sub.content.isEmpty()) app.ui.toast(tr("Couldn't finish: #", sub.error), kind = "error")
        process(msg, d, follow, voice, depth + 1)
    }
}
