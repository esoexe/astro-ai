package io.github.esoexe.astroai.ui.screens

import android.app.DatePickerDialog
import android.app.TimePickerDialog
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.core.MemoryItem
import io.github.esoexe.astroai.core.Note
import io.github.esoexe.astroai.core.Reminder
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.TaskItem
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Badge
import io.github.esoexe.astroai.ui.Card
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.ConfirmDialog
import io.github.esoexe.astroai.ui.EmptyState
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.IconBtn
import io.github.esoexe.astroai.ui.Markdown
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.PromptDialog
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.TopBar
import io.github.esoexe.astroai.ui.ago
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import org.json.JSONObject
import java.text.DateFormat
import java.time.LocalDate
import java.util.Calendar
import java.util.Date

@Composable
private fun SearchBox(q: String, onChange: (String) -> Unit, hint: String) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 6.dp).clip(RoundedCornerShape(12.dp)).background(c.surface).border(1.dp, c.border, RoundedCornerShape(12.dp))
        .padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Ic(AppIcons.search, null, tint = c.faint, size = 18.dp)
        Spacer(Modifier.width(10.dp))
        Box(Modifier.weight(1f)) {
            if (q.isEmpty()) Text(hint, color = c.faint, style = MaterialTheme.typography.bodyMedium)
            BasicTextField(q, onChange, Modifier.fillMaxWidth(), singleLine = true, textStyle = MaterialTheme.typography.bodyMedium.copy(color = c.text), cursorBrush = SolidColor(c.accent))
        }
        if (q.isNotEmpty()) IconBtn(AppIcons.x, T("Clear"), { onChange("") }, size = 16.dp)
    }
}

// ---------------- conversations ----------------

@Composable
fun ConversationsScreen(onMenu: () -> Unit) {
    var q by remember { mutableStateOf("") }
    var renaming by remember { mutableStateOf<String?>(null) }
    val all = app.store.conversations.sortedWith(compareByDescending<JSONObject> { it.bool("pinned") }.thenByDescending { it.long("updatedAt") })
    val shown = if (q.isBlank()) all else all.filter { cv -> cv.str("title").contains(q, true) || cv.optJSONArray("messages")?.toString()?.contains(q, true) == true }
    Screen(T("Chats"), onMenu, T("# saved on this device", all.size), actions = {
        IconBtn(AppIcons.plus, T("New chat"), { app.chat.newChat(); app.ui.go("chat") }, tint = c.text)
    }) {
        Column {
            SearchBox(q, { q = it }, T("Search chats"))
            if (shown.isEmpty()) EmptyState(AppIcons.chat, if (q.isBlank()) T("No chats yet") else T("Nothing found"), if (q.isBlank()) T("Your conversations are saved here, on this phone.") else "")
            LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(shown, key = { it.str("id") }) { cv ->
                    val id = cv.str("id")
                    var menu by remember { mutableStateOf(false) }
                    Card(Modifier.fillMaxWidth(), onClick = { app.chat.open(id); app.ui.go("chat") }, selected = app.chat.activeId == id) {
                        Row(Modifier.padding(start = 14.dp, top = 10.dp, bottom = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    if (cv.bool("pinned")) { Ic(AppIcons.pin, null, tint = c.accent, size = 14.dp); Spacer(Modifier.width(6.dp)) }
                                    Text(cv.str("title").ifEmpty { T("New chat") }, style = MaterialTheme.typography.bodyLarge, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                }
                                Text(T("# messages · #", cv.optJSONArray("messages")?.length() ?: 0, ago(cv.long("updatedAt"))), style = MaterialTheme.typography.bodySmall, color = c.muted)
                            }
                            Box {
                                IconBtn(AppIcons.more, T("More"), { menu = true })
                                DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                                    DropdownMenuItem({ Text(if (cv.bool("pinned")) T("Unpin") else T("Pin")) }, { menu = false; app.chat.togglePin(id) })
                                    DropdownMenuItem({ Text(T("Rename")) }, { menu = false; renaming = id })
                                    DropdownMenuItem({ Text(T("Export as Markdown")) }, {
                                        menu = false
                                        val msgs = cv.optJSONArray("messages")?.let { a -> (0 until a.length()).mapNotNull { io.github.esoexe.astroai.brain.Msg.from(a.getJSONObject(it)) } }.orEmpty()
                                        app.ui.shareText("${cv.str("title").take(40).ifEmpty { "chat" }}.md", app.chat.markdown(msgs))
                                    })
                                    DropdownMenuItem({ Text(T("Delete"), color = c.danger) }, { menu = false; app.chat.delete(id) })
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    renaming?.let { id -> PromptDialog(T("Rename chat"), app.store.conversation(id)?.str("title") ?: "", T("Title"), onConfirm = { app.chat.rename(id, it) }, onDismiss = { renaming = null }) }
}

// ---------------- notes ----------------

@Composable
fun NotesScreen(onMenu: () -> Unit) {
    val store = app.store
    var q by remember { mutableStateOf("") }
    var editing by remember { mutableStateOf<String?>(null) }
    val open = editing?.let { id -> store.notes.firstOrNull { it.id == id } }
    if (open != null) { NoteEditor(open) { editing = null }; return }
    val list = store.notes.sortedByDescending { it.updatedAt }.filter { q.isBlank() || it.title.contains(q, true) || it.body.contains(q, true) }
    Screen(T("Notes"), onMenu, T("# notes", store.notes.size), actions = {
        IconBtn(AppIcons.plus, T("New note"), {
            val n = Note(title = tr("Untitled"), body = ""); store.notes.add(0, n); store.saveNotes(); editing = n.id
        }, tint = c.text)
    }) {
        Column {
            SearchBox(q, { q = it }, T("Search notes"))
            if (list.isEmpty()) EmptyState(AppIcons.note, if (q.isBlank()) T("No notes yet") else T("Nothing found"), T("Write one here, or ask in chat: “make a note about…”"))
            LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(list, key = { it.id }) { n ->
                    Card(Modifier.fillMaxWidth(), onClick = { editing = n.id }) {
                        Column(Modifier.padding(14.dp)) {
                            Text(n.title.ifEmpty { T("Untitled") }, style = MaterialTheme.typography.titleMedium, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            if (n.body.isNotBlank()) Text(n.body.replace(Regex("\\s+"), " ").take(160), style = MaterialTheme.typography.bodyMedium, color = c.muted, maxLines = 2, overflow = TextOverflow.Ellipsis)
                            Text(ago(n.updatedAt), style = MaterialTheme.typography.labelSmall, color = c.faint, modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun NoteEditor(note: Note, onClose: () -> Unit) {
    val store = app.store
    var title by remember(note.id) { mutableStateOf(note.title) }
    var body by remember(note.id) { mutableStateOf(note.body) }
    var preview by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf(false) }
    fun save() {
        val i = store.notes.indexOfFirst { it.id == note.id }
        if (i < 0) return
        if (title != note.title || body != note.body) { store.notes[i] = note.copy(title = title.trim().ifEmpty { tr("Untitled") }, body = body, updatedAt = System.currentTimeMillis()); store.saveNotes() }
    }
    fun close() { save(); onClose() }
    BackHandler { close() }
    Column(Modifier.fillMaxSize().navigationBarsPadding().imePadding()) {
        TopBar(T("Note"), onBack = { close() }) {
            IconBtn(if (preview) AppIcons.edit else AppIcons.eye, if (preview) T("Edit") else T("Preview"), { preview = !preview }, tint = c.text)
            IconBtn(AppIcons.sparkles, T("Ask about this note"), {
                save(); app.chat.newChat(); app.ui.go("chat")
                app.ui.incomingText = tr("About my note “#”:", title) + "\n\n" + body.take(6000) + "\n\n"
            }, tint = c.text)
            IconBtn(AppIcons.export, T("Share"), { save(); app.ui.shareText("${title.take(40).ifEmpty { "note" }}.md", "# $title\n\n$body") }, tint = c.text)
            IconBtn(AppIcons.trash, T("Delete"), { deleting = true }, tint = c.danger)
        }
        Column(Modifier.weight(1f).padding(horizontal = 18.dp)) {
            BasicTextField(title, { title = it }, Modifier.fillMaxWidth().padding(vertical = 8.dp), textStyle = MaterialTheme.typography.headlineSmall.copy(color = c.text), cursorBrush = SolidColor(c.accent), singleLine = true)
            if (preview) Column(Modifier.weight(1f).verticalScroll(rememberScrollState())) { Markdown(body.ifBlank { "_" + tr("Empty") + "_" }) }
            else Box(Modifier.weight(1f)) {
                if (body.isEmpty()) Text(T("Write… (Markdown works)"), color = c.faint, style = MaterialTheme.typography.bodyLarge)
                BasicTextField(body, { body = it }, Modifier.fillMaxSize(), textStyle = MaterialTheme.typography.bodyLarge.copy(color = c.text), cursorBrush = SolidColor(c.accent))
            }
        }
    }
    if (deleting) ConfirmDialog(T("Delete this note?"), T("You can undo right after."), T("Delete"), {
        val i = store.notes.indexOfFirst { it.id == note.id }
        if (i >= 0) {
            val removed = store.notes.removeAt(i); store.saveNotes(); onClose()
            app.ui.toast(tr("Note deleted"), action = tr("Undo")) { store.notes.add(i.coerceAtMost(store.notes.size), removed); store.saveNotes() }
        }
    }, { deleting = false })
}

// ---------------- tasks + reminders ----------------

@Composable
fun TasksScreen(onMenu: () -> Unit) {
    val store = app.store
    val ctx = LocalContext.current
    var filter by remember { mutableStateOf("open") }
    var newTitle by remember { mutableStateOf("") }
    var newDue by remember { mutableStateOf("") }
    var newPriority by remember { mutableStateOf("normal") }
    var reminderFor by remember { mutableStateOf<String?>(null) }
    val today = LocalDate.now().toString()
    val list = store.tasks.filter {
        when (filter) { "open" -> !it.done; "today" -> !it.done && it.due.isNotEmpty() && it.due <= today; "done" -> it.done; else -> true }
    }.sortedWith(compareBy<TaskItem>({ it.done }, { if (it.due.isEmpty()) "9999" else it.due }, { when (it.priority) { "high" -> 0; "normal" -> 1; else -> 2 } }))

    fun pickDate(onPicked: (String) -> Unit) {
        val cal = Calendar.getInstance()
        DatePickerDialog(ctx, { _, y, m, d -> onPicked("%04d-%02d-%02d".format(y, m + 1, d)) }, cal.get(Calendar.YEAR), cal.get(Calendar.MONTH), cal.get(Calendar.DAY_OF_MONTH)).show()
    }
    fun add() {
        val t = newTitle.trim(); if (t.isEmpty()) return
        store.tasks.add(0, TaskItem(title = t.take(180), due = newDue, priority = newPriority)); store.saveTasks()
        newTitle = ""; newDue = ""; newPriority = "normal"
    }

    Screen(T("Tasks"), onMenu, T("# open · # done", store.tasks.count { !it.done }, store.tasks.count { it.done }), actions = {
        if (store.tasks.any { it.done }) IconBtn(AppIcons.trash, T("Clear done"), {
            val done = store.tasks.filter { it.done }
            store.tasks.removeAll(done.toSet()); store.saveTasks()
            app.ui.toast(tr("Cleared # done tasks", done.size), action = tr("Undo")) { store.tasks.addAll(done); store.saveTasks() }
        })
    }) {
        LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(12.dp)) {
                        Field(newTitle, { newTitle = it }, "", placeholder = T("Add a task…"), onDone = { add() })
                        Row(Modifier.padding(top = 8.dp).horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Chip(if (newDue.isEmpty()) T("Due date") else newDue, selected = newDue.isNotEmpty(), icon = AppIcons.history, onClick = { if (newDue.isEmpty()) pickDate { newDue = it } else newDue = "" })
                            listOf("low" to "Low", "normal" to "Normal", "high" to "High").forEach { (k, l) -> Chip(T(l), selected = newPriority == k, onClick = { newPriority = k }) }
                            Primary(T("Add"), { add() }, enabled = newTitle.isNotBlank())
                        }
                    }
                }
            }
            item {
                Row(Modifier.horizontalScroll(rememberScrollState()).padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf("open" to "Open", "today" to "Due", "done" to "Done", "all" to "All").forEach { (k, l) -> Chip(T(l), selected = filter == k, onClick = { filter = k }) }
                }
            }
            if (list.isEmpty()) item { EmptyState(AppIcons.task, T("Nothing here"), T("Add tasks above, or ask in chat: “add a task to…”")) }
            items(list, key = { it.id }) { t -> TaskRow(t, today, onRemind = { reminderFor = t.title }) }
            item { RemindersSection(onAdd = { reminderFor = "" }) }
            item { MemorySection() }
        }
    }
    reminderFor?.let { text -> ReminderDialog(text) { reminderFor = null } }
}

@Composable
private fun TaskRow(t: TaskItem, today: String, onRemind: () -> Unit) {
    val store = app.store
    var menu by remember { mutableStateOf(false) }
    fun update(x: TaskItem) { val i = store.tasks.indexOfFirst { it.id == t.id }; if (i >= 0) { store.tasks[i] = x; store.saveTasks() } }
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Checkbox(t.done, { update(t.copy(done = it)) }, colors = CheckboxDefaults.colors(checkedColor = c.accent, checkmarkColor = c.onAccent, uncheckedColor = c.faint))
            Column(Modifier.weight(1f).padding(vertical = 10.dp)) {
                Text(t.title, style = MaterialTheme.typography.bodyLarge, color = if (t.done) c.faint else c.text, textDecoration = if (t.done) TextDecoration.LineThrough else null)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(top = 2.dp)) {
                    if (t.due.isNotEmpty()) Badge(if (t.due == today) tr("today") else t.due, if (!t.done && t.due < today) c.danger else if (t.due == today) c.accent else c.muted)
                    if (t.priority == "high") Badge(tr("high"), c.danger)
                    if (t.priority == "low") Badge(tr("low"), c.muted)
                }
            }
            Box {
                IconBtn(AppIcons.more, T("More"), { menu = true })
                DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                    DropdownMenuItem({ Text(T("Remind me")) }, { menu = false; onRemind() }, leadingIcon = { Ic(AppIcons.bell) })
                    listOf("high" to "High priority", "normal" to "Normal priority", "low" to "Low priority").forEach { (k, l) ->
                        DropdownMenuItem({ Text(T(l)) }, { menu = false; update(t.copy(priority = k)) }, trailingIcon = { if (t.priority == k) Ic(AppIcons.check, tint = c.accent) })
                    }
                    if (t.due.isNotEmpty()) DropdownMenuItem({ Text(T("Remove due date")) }, { menu = false; update(t.copy(due = "")) })
                    DropdownMenuItem({ Text(T("Delete"), color = c.danger) }, {
                        menu = false
                        val i = store.tasks.indexOfFirst { it.id == t.id }
                        if (i >= 0) { val r = store.tasks.removeAt(i); store.saveTasks(); app.ui.toast(tr("Task deleted"), action = tr("Undo")) { store.tasks.add(i.coerceAtMost(store.tasks.size), r); store.saveTasks() } }
                    })
                }
            }
        }
    }
}

@Composable
private fun RemindersSection(onAdd: () -> Unit) {
    val store = app.store
    val upcoming = store.reminders.filter { !it.fired }.sortedBy { it.at }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SectionTitle(T("Reminders"), Modifier.weight(1f))
            Text(T("Add"), Modifier.clip(RoundedCornerShape(8.dp)).clickable(onClick = onAdd).padding(8.dp), color = c.accent, style = MaterialTheme.typography.labelLarge)
        }
        if (upcoming.isEmpty()) Text(T("No reminders. Ask in chat: “remind me at 6 to…”"), style = MaterialTheme.typography.bodySmall, color = c.faint)
        upcoming.forEach { r ->
            Row(Modifier.fillMaxWidth().padding(vertical = 4.dp).clip(RoundedCornerShape(12.dp)).background(c.surface).padding(start = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Ic(AppIcons.bell, null, tint = c.accent, size = 16.dp)
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f).padding(vertical = 8.dp)) {
                    Text(r.text, style = MaterialTheme.typography.bodyMedium, color = c.text)
                    Text(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(r.at)), style = MaterialTheme.typography.labelSmall, color = c.muted)
                }
                IconBtn(AppIcons.x, T("Cancel reminder"), {
                    app.reminders.cancel(r.id, r.text)
                    val i = store.reminders.indexOf(r)
                    store.reminders.remove(r); store.saveReminders()
                    app.ui.toast(tr("Reminder cancelled"), action = tr("Undo")) { store.reminders.add(i.coerceIn(0, store.reminders.size), r); store.saveReminders() }
                }, size = 16.dp)
            }
        }
    }
}

@Composable
private fun MemorySection() {
    val store = app.store
    var adding by remember { mutableStateOf(false) }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SectionTitle(T("What A.S.T.R.O AI remembers about you"), Modifier.weight(1f))
            Text(T("Add"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { adding = true }.padding(8.dp), color = c.accent, style = MaterialTheme.typography.labelLarge)
        }
        if (store.memory.isEmpty()) Text(T("Nothing yet. Say “remember that I…” in chat."), style = MaterialTheme.typography.bodySmall, color = c.faint)
        store.memory.forEach { m ->
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(12.dp)).background(c.surface).padding(start = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Ic(AppIcons.memory, null, tint = c.muted, size = 16.dp)
                Spacer(Modifier.width(10.dp))
                Text(m.fact, Modifier.weight(1f).padding(vertical = 8.dp), style = MaterialTheme.typography.bodyMedium, color = c.text)
                IconBtn(AppIcons.x, T("Forget"), { store.memory.remove(m); store.saveMemory() }, size = 16.dp)
            }
        }
    }
    if (adding) PromptDialog(T("Remember"), "", T("A fact about you"), T("Save"), onConfirm = {
        store.memory.add(MemoryItem(fact = it.trim().take(300))); while (store.memory.size > 80) store.memory.removeAt(0); store.saveMemory()
    }, onDismiss = { adding = false })
}

@Composable
private fun ReminderDialog(initial: String, onDismiss: () -> Unit) {
    val ctx = LocalContext.current
    var text by remember { mutableStateOf(initial) }
    var at by remember { mutableStateOf(System.currentTimeMillis() + 3600_000L) }
    androidx.compose.material3.AlertDialog(onDismissRequest = onDismiss, containerColor = c.surface,
        title = { Text(T("New reminder")) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Field(text, { text = it }, T("Remind me to…"))
                Chip(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(at)), icon = AppIcons.bell, onClick = {
                    val cal = Calendar.getInstance().apply { timeInMillis = at }
                    DatePickerDialog(ctx, { _, y, m, d ->
                        TimePickerDialog(ctx, { _, h, min ->
                            at = Calendar.getInstance().apply { set(y, m, d, h, min, 0) }.timeInMillis
                        }, cal.get(Calendar.HOUR_OF_DAY), cal.get(Calendar.MINUTE), android.text.format.DateFormat.is24HourFormat(ctx)).show()
                    }, cal.get(Calendar.YEAR), cal.get(Calendar.MONTH), cal.get(Calendar.DAY_OF_MONTH)).show()
                })
            }
        },
        confirmButton = {
            androidx.compose.material3.TextButton({
                if (text.isNotBlank() && at > System.currentTimeMillis()) {
                    app.store.reminders.add(Reminder(text = text.trim().take(200), at = at)); app.store.saveReminders()
                    app.reminders.askPermission()
                    app.ui.toast(tr("Reminder set")); onDismiss()
                } else app.ui.toast(tr("Pick a time in the future"), "error")
            }) { Text(T("Save"), color = c.accent) }
        },
        dismissButton = { androidx.compose.material3.TextButton(onDismiss) { Text(T("Cancel"), color = c.muted) } })
}

@Suppress("unused") private val dot = Modifier.size(1.dp).clip(CircleShape).height(1.dp)
