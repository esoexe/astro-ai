package io.github.esoexe.astroai.core

import android.content.Context
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.net.ApiError
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

data class Note(val id: String = makeId(), var title: String, var body: String, var updatedAt: Long = System.currentTimeMillis()) {
    fun toJson() = jsonOf("id" to id, "title" to title, "body" to body, "updatedAt" to updatedAt)
    companion object {
        fun from(o: JSONObject) = Note(o.str("id", 80).ifEmpty { makeId() }, o.str("title", 200), o.str("body", 50_000), o.long("updatedAt").takeIf { it > 0 } ?: System.currentTimeMillis())
    }
}

data class TaskItem(val id: String = makeId(), var title: String, var due: String = "", var priority: String = "normal", var done: Boolean = false, val createdAt: Long = System.currentTimeMillis()) {
    fun toJson() = jsonOf("id" to id, "title" to title, "due" to due, "priority" to priority, "done" to done, "createdAt" to createdAt)
    companion object {
        fun from(o: JSONObject) = TaskItem(o.str("id", 80).ifEmpty { makeId() }, o.str("title", 180), o.str("due", 10).takeIf { Regex("\\d{4}-\\d{2}-\\d{2}").matches(it) } ?: "",
            o.str("priority").takeIf { it in setOf("low", "normal", "high") } ?: "normal", o.bool("done"), o.long("createdAt").takeIf { it > 0 } ?: System.currentTimeMillis())
    }
}

data class MemoryItem(val id: String = makeId(), val fact: String, val at: Long = System.currentTimeMillis()) {
    fun toJson() = jsonOf("id" to id, "fact" to fact, "at" to at)
    companion object { fun from(o: JSONObject) = MemoryItem(o.str("id", 80).ifEmpty { makeId() }, o.str("fact", 300), o.long("at").takeIf { it > 0 } ?: System.currentTimeMillis()) }
}

data class Reminder(val id: String = makeId(), val text: String, val at: Long, var fired: Boolean = false) {
    fun toJson() = jsonOf("id" to id, "text" to text, "at" to at, "fired" to fired)
    companion object { fun from(o: JSONObject) = Reminder(o.str("id", 80).ifEmpty { makeId() }, o.str("text", 200), o.long("at"), o.bool("fired")) }
}

/**
 * Chats, notes, tasks, memory and reminders. Each list is one JSON file in the app's private storage with the
 * same content as the web app's localStorage key, so "Sync with my computer" (/api/sync) exchanges them as-is.
 * Conversations stay in the web format (JSONObject) and are converted only when a chat is opened.
 */
class Store(ctx: Context, private val prefs: Prefs) {
    companion object {
        val LIMITS = mapOf("chats" to 60, "messages" to 120, "drafts" to 6, "draftChars" to 60_000, "thinkingChars" to 12_000, "history" to 24, "msgChars" to 12_000)
        val SYNC_KEYS = listOf("chats", "notes", "tasks", "memory", "reminders")
    }

    private val dir = File(ctx.filesDir, "store").apply { mkdirs() }
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "astro-store").apply { isDaemon = true } }

    val conversations: SnapshotStateList<JSONObject> = mutableStateListOf()
    val notes: SnapshotStateList<Note> = mutableStateListOf()
    val tasks: SnapshotStateList<TaskItem> = mutableStateListOf()
    val memory: SnapshotStateList<MemoryItem> = mutableStateListOf()
    val reminders: SnapshotStateList<Reminder> = mutableStateListOf()

    /** Called after reminders change, so alarms are rescheduled. */
    var onRemindersChanged: (() -> Unit)? = null

    init { loadAll() }

    private fun file(key: String) = File(dir, "$key.json")
    private fun readArray(key: String): JSONArray = try { JSONArray(file(key).readText()) } catch (_: Exception) { JSONArray() }

    fun loadAll() {
        conversations.clear(); conversations.addAll(readArray("chats").objects().filter { it.str("id").isNotEmpty() && it.optJSONArray("messages") != null })
        notes.clear(); notes.addAll(readArray("notes").objects().map(Note::from))
        tasks.clear(); tasks.addAll(readArray("tasks").objects().map(TaskItem::from).filter { it.title.isNotBlank() })
        memory.clear(); memory.addAll(readArray("memory").objects().map(MemoryItem::from).filter { it.fact.isNotBlank() }.take(80))
        reminders.clear(); reminders.addAll(readArray("reminders").objects().map(Reminder::from).filter { it.text.isNotBlank() && it.at > 0 })
    }

    /** The JSON text of one list, exactly as the web app stores it. */
    fun snapshot(key: String): String = when (key) {
        "chats" -> JSONArray(conversations.toList()).toString()
        "notes" -> JSONArray(notes.map { it.toJson() }).toString()
        "tasks" -> JSONArray(tasks.map { it.toJson() }).toString()
        "memory" -> JSONArray(memory.map { it.toJson() }).toString()
        "reminders" -> JSONArray(reminders.map { it.toJson() }).toString()
        else -> "[]"
    }

    private fun write(key: String) {
        val text = snapshot(key)
        io.execute {
            val f = file(key); val tmp = File(dir, "$key.json.tmp")
            try { tmp.writeText(text); if (!tmp.renameTo(f)) { f.delete(); tmp.renameTo(f) } } catch (_: Exception) { }
        }
    }

    fun saveChats() = write("chats")
    fun saveNotes() = write("notes")
    fun saveTasks() = write("tasks")
    fun saveMemory() = write("memory")
    fun saveReminders() { write("reminders"); onRemindersChanged?.invoke() }

    /** Waits for pending writes (before an export or sync). */
    fun flush() { try { io.submit {}.get() } catch (_: Exception) { } }

    fun upsertConversation(c: JSONObject) {
        val i = conversations.indexOfFirst { it.str("id") == c.str("id") }
        if (i >= 0) conversations.removeAt(i)
        conversations.add(0, c)
        while (conversations.size > LIMITS.getValue("chats")) {
            val drop = conversations.indexOfLast { !it.bool("pinned") }.takeIf { it >= 0 } ?: (conversations.size - 1)
            conversations.removeAt(drop)
        }
        saveChats()
    }

    fun conversation(id: String): JSONObject? = conversations.firstOrNull { it.str("id") == id }

    // ---------------- sync with A.S.T.R.O AI on the computer (/api/sync) ----------------

    private fun signature(data: JSONObject): String {
        val s = data.toString(); var h = 0
        for (ch in s) h = h * 31 + ch.code
        return "${s.length}:$h"
    }

    private fun syncData(): JSONObject = JSONObject().also { o -> SYNC_KEYS.forEach { o.put(it, snapshot(it)) } }

    /** Saves this phone's lists on the server. Returns a message for the user, or null when nothing changed. */
    fun pushBlocking(force: Boolean = false): String? {
        flush()
        val data = syncData()
        val sig = signature(data)
        if (!force && sig == prefs.syncSig) return null
        val updated = System.currentTimeMillis()
        try {
            Api.requestBlocking("POST", "/api/sync", jsonOf("updated" to updated, "data" to data, "base" to prefs.syncUpdated, "force" to force), 30_000)
        } catch (e: ApiError) {
            if (e.status == 409) return "Newer data from another device is on the server"
            throw e
        }
        prefs.syncUpdated = updated; prefs.syncSig = sig
        return "This device is saved on the server"
    }

    /** What the server has: (updated, data) or null. */
    fun remoteBlocking(): Pair<Long, JSONObject>? {
        val j = Api.requestBlocking("GET", "/api/sync", null, 30_000)
        val updated = j.long("updated")
        val data = j.optJSONObject("data") ?: return null
        return if (updated > 0) updated to data else null
    }

    /** Replaces the local lists with the server copy (on the main thread). */
    fun applyRemote(updated: Long, data: JSONObject) {
        for (k in SYNC_KEYS) {
            val s = data.optString(k, "")
            if (s.isNotEmpty()) try { JSONArray(s); file(k).writeText(s) } catch (_: Exception) { }
        }
        loadAll()
        prefs.syncUpdated = updated
        prefs.syncSig = signature(syncData())
        onRemindersChanged?.invoke()
    }

    val hasLocalData: Boolean get() = conversations.isNotEmpty() || notes.isNotEmpty() || tasks.isNotEmpty()
}
