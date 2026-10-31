package io.github.esoexe.astroai.ui.screens

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.int
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.strings
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
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
import io.github.esoexe.astroai.ui.Mono
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.PulseDot
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.ToggleRow
import io.github.esoexe.astroai.ui.TopBar
import io.github.esoexe.astroai.ui.ago
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import io.github.esoexe.astroai.ui.secs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject

private object AgentState {
    var open by mutableStateOf<String?>(null)
    var meta by mutableStateOf(JSONObject())
    val events = mutableStateListOf<JSONObject>()
    var todos by mutableStateOf<List<JSONObject>>(emptyList())
    var running by mutableStateOf(false)
    var since = 0L
    var models by mutableStateOf<List<JSONObject>>(emptyList())
}

private suspend fun agentCall(action: String, body: JSONObject): JSONObject = Api.post("/api/agent/$action", body, 60_000)

private fun applyView(v: JSONObject) {
    AgentState.meta = v
    AgentState.running = v.bool("running")
    v.optJSONArray("todos")?.let { AgentState.todos = it.objects() }
    val evs = v.optJSONArray("events")?.objects().orEmpty()
    for (e in evs) {
        val seq = e.long("seq")
        if (seq <= AgentState.since) continue
        AgentState.since = seq
        AgentState.events.add(e)
    }
    while (AgentState.events.size > 1500) AgentState.events.removeAt(0)
}

@Composable
fun AgentScreen(onMenu: () -> Unit) {
    val id = AgentState.open
    if (id != null) SessionView(id) else AgentHome(onMenu)
}

@Composable
private fun AgentHome(onMenu: () -> Unit) {
    val prefs = app.prefs
    var prompt by remember { mutableStateOf("") }
    var isolate by remember { mutableStateOf(false) }
    var review by remember { mutableStateOf(true) }
    var sessions by remember { mutableStateOf<List<JSONObject>>(emptyList()) }
    var info by remember { mutableStateOf("") }
    var starting by remember { mutableStateOf(false) }
    var modelMenu by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        try {
            Ws.ensureProject()
            if (AgentState.models.isEmpty()) AgentState.models = agentCall("models", JSONObject()).optJSONArray("all")?.objects().orEmpty()
            val l = agentCall("list", jsonOf("project" to Ws.project))
            sessions = l.optJSONArray("sessions")?.objects().orEmpty().sortedByDescending { it.long("updated") }
            info = tr("# running · up to # at once", l.int("running"), l.int("max"))
        } catch (e: Exception) { info = e.message ?: "" }
    }
    fun start() {
        val p = prompt.trim(); if (p.isEmpty()) return
        starting = true
        app.chat.scope.launch {
            try {
                val v = agentCall("start", jsonOf("project" to Ws.ensureProject(), "prompt" to p, "mode" to prefs.agentMode, "model" to prefs.agentModel.ifEmpty { "auto" }, "review" to review, "isolate" to isolate, "interactive" to true, "max_steps" to 60))
                AgentState.events.clear(); AgentState.since = 0; applyView(v)
                AgentState.open = v.str("id"); prompt = ""
            } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
            starting = false
        }
    }
    Screen(T("Agent"), onMenu, T("Works on # by itself until the job is done", Ws.project.ifEmpty { T("a project") })) {
        LazyColumn(Modifier.imePadding(), contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(14.dp)) {
                        Field(prompt, { prompt = it }, T("What should the agent do?"), singleLine = false, minLines = 3, placeholder = T("e.g. Build a to-do web page with local storage and tests"))
                        Spacer(Modifier.height(10.dp))
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf("auto" to "Auto", "plan" to "Plan", "ask" to "Ask first").forEach { (k, l) -> Chip(T(l), selected = prefs.agentMode == k, onClick = { prefs.agentMode = k }) }
                            Box {
                                Chip(if (prefs.agentModel.ifEmpty { "auto" } == "auto") T("Team + fast executor") else prefs.agentModel.substringAfter(':'), icon = AppIcons.brain, onClick = { modelMenu = true })
                                DropdownMenu(modelMenu, { modelMenu = false }, containerColor = c.surface, modifier = Modifier.heightIn(max = 420.dp)) {
                                    DropdownMenuItem({ Text(T("Team + fast executor (recommended)")) }, { modelMenu = false; prefs.agentModel = "auto" })
                                    AgentState.models.filter { !it.bool("paid") }.forEach { m ->
                                        DropdownMenuItem({ Text(m.str("label"), maxLines = 1, overflow = TextOverflow.Ellipsis) }, { modelMenu = false; prefs.agentModel = m.str("id") }, enabled = m.opt("blocked") == false || m.str("blocked").isEmpty())
                                    }
                                }
                            }
                        }
                        Text(when (prefs.agentMode) { "plan" -> T("Plan: reads the project and writes a plan, changes nothing."); "ask" -> T("Ask first: asks before every change or command."); else -> T("Auto: does everything by itself, with a log and an undo checkpoint.") },
                            style = MaterialTheme.typography.bodySmall, color = c.muted, modifier = Modifier.padding(top = 8.dp))
                        ToggleRow(T("Work on a copy"), T("Changes stay separate until you merge them"), isolate, { isolate = it })
                        ToggleRow(T("Team review at the end"), T("Other models check the result before it says done"), review, { review = it })
                        Primary(if (starting) T("Starting…") else T("Start the agent"), { start() }, Modifier.fillMaxWidth(), enabled = prompt.isNotBlank() && !starting, icon = AppIcons.play)
                    }
                }
            }
            item { Row(verticalAlignment = Alignment.CenterVertically) { SectionTitle(T("Sessions"), Modifier.weight(1f)); Text(info, style = MaterialTheme.typography.labelSmall, color = c.faint) } }
            if (sessions.isEmpty()) item { EmptyState(AppIcons.agent, T("No agent sessions yet"), T("Give it a job above. It plans, writes files, runs them and checks its work.")) }
            items(sessions, key = { it.str("id") }) { s ->
                Card(Modifier.fillMaxWidth(), onClick = { AgentState.events.clear(); AgentState.since = 0; AgentState.meta = s; AgentState.open = s.str("id") }) {
                    Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                        StatusDot(s.str("status"))
                        Spacer(Modifier.width(10.dp))
                        Column(Modifier.weight(1f)) {
                            Text(s.str("title").ifEmpty { T("Agent session") }, style = MaterialTheme.typography.bodyLarge, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            Text(T("# · # steps · # files · #", T(s.str("mode")), s.int("steps"), s.int("files"), ago(s.long("updated"))), style = MaterialTheme.typography.bodySmall, color = c.muted)
                        }
                        if (s.bool("isolated") && !s.bool("merged") && !s.bool("discarded")) Badge(tr("copy"))
                    }
                }
            }
        }
    }
}

@Composable
private fun StatusDot(status: String) {
    val col = when (status) { "running", "working" -> c.accent; "done" -> c.ok; "error", "failed" -> c.danger; "waiting" -> c.warn; else -> c.faint }
    if (status == "running" || status == "working") PulseDot(col, 10.dp) else Box(Modifier.size(10.dp).clip(CircleShape).background(col))
}

@Composable
private fun SessionView(id: String) {
    var input by remember { mutableStateOf("") }
    var undoAsk by remember { mutableStateOf(false) }
    var diff by remember { mutableStateOf<String?>(null) }
    var menu by remember { mutableStateOf(false) }
    val list = rememberLazyListState()
    BackHandler { AgentState.open = null }
    LaunchedEffect(id) {
        try { applyView(agentCall("get", jsonOf("id" to id, "since" to AgentState.since))) } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        var failures = 0
        while (isActive) {
            try {
                val v = agentCall("events", jsonOf("id" to id, "since" to AgentState.since, "wait" to (if (AgentState.running) 20 else 25)))
                applyView(v); failures = 0
                if (!AgentState.running) delay(1500)
            } catch (e: CancellationException) { throw e } catch (_: Exception) { failures++; delay((failures * 2000L).coerceAtMost(15_000)) }
        }
    }
    LaunchedEffect(AgentState.events.size) { if (AgentState.events.isNotEmpty()) list.animateScrollToItem(AgentState.events.size) }
    val meta = AgentState.meta
    fun act(action: String, body: JSONObject = JSONObject(), done: String = "") {
        app.chat.scope.launch {
            try { val r = agentCall(action, body.put("id", id)); if (r.has("events")) applyView(r); if (done.isNotEmpty()) app.ui.toast(done); if (action == "merge" || action == "undo") try { Ws.loadTree() } catch (_: Exception) { } }
            catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        }
    }
    val waitingQ = AgentState.events.lastOrNull { it.str("type") == "question" || it.str("type") == "approval" }?.takeIf { q -> AgentState.events.none { it.str("type") == "answered" && it.str("id") == q.str("id") } }

    Column(Modifier.fillMaxSize().navigationBarsPadding().imePadding()) {
        TopBar(meta.str("title").ifEmpty { T("Agent session") }, listOf(T(meta.str("mode")), meta.str("status"), secs(meta.long("ms"))).filter { it.isNotEmpty() }.joinToString(" · "), onBack = { AgentState.open = null }) {
            if (AgentState.running) IconBtn(AppIcons.stop, T("Stop"), { act("stop") }, tint = c.danger)
            Box {
                IconBtn(AppIcons.more, T("More"), { menu = true }, tint = c.text)
                DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                    DropdownMenuItem({ Text(T("Show changes")) }, { menu = false; app.chat.scope.launch { try { diff = agentCall("diff", jsonOf("id" to id)).str("diff").ifEmpty { tr("No changes") } } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } } }, leadingIcon = { Ic(AppIcons.diff) })
                    if (meta.bool("isolated") && !meta.bool("merged") && !meta.bool("discarded")) {
                        DropdownMenuItem({ Text(T("Merge into #", meta.str("base"))) }, { menu = false; act("merge", done = tr("Merged")) }, leadingIcon = { Ic(AppIcons.git) })
                        DropdownMenuItem({ Text(T("Discard the copy")) }, { menu = false; act("discard") }, leadingIcon = { Ic(AppIcons.trash) })
                    }
                    DropdownMenuItem({ Text(T("Undo everything")) }, { menu = false; undoAsk = true }, leadingIcon = { Ic(AppIcons.undo) })
                    DropdownMenuItem({ Text(T("Open the files")) }, { menu = false; app.ui.go("workspace") }, leadingIcon = { Ic(AppIcons.folderOpen) })
                }
            }
        }
        if (AgentState.todos.isNotEmpty()) {
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                AgentState.todos.forEach { t ->
                    val st = t.str("status")
                    Badge((if (st == "done") "✓ " else if (st == "doing") "● " else "○ ") + t.str("text").take(40), if (st == "done") c.ok else if (st == "doing") c.accent else c.muted)
                }
            }
        }
        LazyColumn(state = list, modifier = Modifier.weight(1f).fillMaxWidth(), contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(AgentState.events, key = { "e" + it.long("seq") }) { e -> EventView(e, id) }
            item {
                if (AgentState.running && waitingQ == null) Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(vertical = 6.dp)) {
                    ThinkingDots(); Spacer(Modifier.width(10.dp))
                    Text(T("Working · step #", meta.int("steps")), style = MaterialTheme.typography.bodySmall, color = c.muted)
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) { Field(input, { input = it }, "", placeholder = if (AgentState.running) T("Add an instruction (queued)") else T("Ask for more changes"), singleLine = false) }
            IconBtn(AppIcons.send, T("Send"), { if (input.isNotBlank()) { act("send", jsonOf("prompt" to input.trim())); input = "" } }, tint = c.accent)
        }
    }
    if (undoAsk) ConfirmDialog(T("Undo this session?"), T("The project goes back to the checkpoint made before the agent started."), T("Undo"), { act("undo", done = tr("Undone")) }, { undoAsk = false })
    diff?.let { d ->
        androidx.compose.material3.AlertDialog(onDismissRequest = { diff = null }, containerColor = c.surface, title = { Text(T("Changes")) },
            text = {
                SelectionContainer {
                    LazyColumn(Modifier.heightIn(max = 480.dp)) {
                        items(d.split('\n').take(4000)) { l ->
                            Text(l, style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono, fontSize = 11.5.sp), softWrap = false,
                                color = when { l.startsWith("+") && !l.startsWith("+++") -> c.ok; l.startsWith("-") && !l.startsWith("---") -> c.danger; l.startsWith("@@") || l.startsWith("diff") -> c.accent; else -> c.muted })
                        }
                    }
                }
            },
            confirmButton = { androidx.compose.material3.TextButton({ diff = null }) { Text(T("Close"), color = c.accent) } })
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EventView(e: JSONObject, sessionId: String) {
    fun answer(qid: String, a: String) { app.chat.scope.launch { try { applyView(agentCall("answer", jsonOf("id" to sessionId, "qid" to qid, "answer" to a))) } catch (ex: Exception) { app.ui.toast(ex.message ?: "", "error") } } }
    val answered = { qid: String -> AgentState.events.firstOrNull { it.str("type") == "answered" && it.str("id") == qid }?.str("answer") }
    when (e.str("type")) {
        "user" -> Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            Text(e.str("text"), Modifier.clip(RoundedCornerShape(16.dp)).background(c.userBubble).padding(12.dp), color = c.text, style = MaterialTheme.typography.bodyLarge)
        }
        "say" -> SelectionContainer { Markdown(e.str("text")) }
        "tool" -> {
            val done = AgentState.events.firstOrNull { it.str("type") == "tool_done" && it.str("id") == e.str("id") }
            Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.surface).padding(horizontal = 10.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                when {
                    done == null -> PulseDot(c.accent, 8.dp)
                    done.bool("ok") -> Ic(AppIcons.check, null, tint = c.ok, size = 15.dp)
                    else -> Ic(AppIcons.x, null, tint = c.danger, size = 15.dp)
                }
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(e.str("label").ifEmpty { e.str("tool") }, style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    done?.str("summary")?.takeIf { it.isNotEmpty() }?.let { Text(it, style = MaterialTheme.typography.labelSmall, color = c.muted, maxLines = 3, overflow = TextOverflow.Ellipsis) }
                }
            }
        }
        "tool_done", "answered", "status", "todo" -> {}
        "note" -> Text("• " + e.str("text"), style = MaterialTheme.typography.bodySmall, color = c.muted)
        "question" -> Card(Modifier.fillMaxWidth(), selected = answered(e.str("id")) == null) {
            Column(Modifier.padding(14.dp)) {
                Text(e.str("question"), style = MaterialTheme.typography.bodyLarge, color = c.text)
                val a = answered(e.str("id"))
                if (a != null) Text(T("You answered: #", a), style = MaterialTheme.typography.bodySmall, color = c.muted, modifier = Modifier.padding(top = 6.dp))
                else {
                    var custom by remember { mutableStateOf("") }
                    FlowRow(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        e.optJSONArray("options")?.strings().orEmpty().forEach { o -> Chip(o, onClick = { answer(e.str("id"), o) }) }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.weight(1f)) { Field(custom, { custom = it }, "", placeholder = T("Your answer")) }
                        IconBtn(AppIcons.send, T("Send"), { if (custom.isNotBlank()) answer(e.str("id"), custom) }, tint = c.accent)
                    }
                }
            }
        }
        "approval" -> Card(Modifier.fillMaxWidth(), selected = answered(e.str("id")) == null) {
            Column(Modifier.padding(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Ic(AppIcons.hand, null, tint = if (e.str("risky").isNotEmpty()) c.danger else c.accent, size = 18.dp); Spacer(Modifier.width(8.dp))
                    Text(e.str("text"), style = MaterialTheme.typography.bodyLarge, color = c.text)
                }
                if (e.str("preview").isNotEmpty()) Text(e.str("preview").take(1500), Modifier.padding(top = 8.dp).fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(c.codeBg).padding(10.dp),
                    style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.muted, maxLines = 14, overflow = TextOverflow.Ellipsis)
                val a = answered(e.str("id"))
                if (a != null) Text(T("You answered: #", a), style = MaterialTheme.typography.bodySmall, color = c.muted, modifier = Modifier.padding(top = 6.dp))
                else Row(Modifier.padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Primary(T("Allow"), { answer(e.str("id"), "allow") })
                    Secondary(T("Allow all"), { answer(e.str("id"), "allow all") })
                    Secondary(T("Deny"), { answer(e.str("id"), "deny") }, danger = true)
                }
            }
        }
        "review" -> Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.surface).padding(10.dp)) {
            Badge(e.str("verdict"), if (e.str("verdict").startsWith("PASS") || e.str("verdict") == "OK") c.ok else c.warn)
            Spacer(Modifier.width(8.dp))
            Text(e.str("text").take(600), style = MaterialTheme.typography.bodySmall, color = c.muted)
        }
        "done" -> Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(c.accentSoft).border(1.dp, c.accent.copy(alpha = 0.4f), RoundedCornerShape(14.dp)).padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Ic(AppIcons.check, null, tint = c.ok); Spacer(Modifier.width(8.dp))
                Text(T("Done · # steps · #", e.int("steps"), secs(e.long("ms"))), style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold), color = c.text)
            }
            Spacer(Modifier.height(8.dp))
            SelectionContainer { Markdown(e.str("text")) }
            val files = e.optJSONArray("files")?.let { a -> (0 until a.length()).map { i -> a.opt(i).let { if (it is JSONObject) it.str("path") else it.toString() } } }.orEmpty()
            if (files.isNotEmpty()) FlowRow(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                files.take(30).forEach { f -> Chip(f, icon = AppIcons.file, onClick = { Ws.openFile = f; Ws.tab = "code"; app.ui.go("workspace") }) }
            }
        }
        "error" -> Text(e.str("text"), Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.danger.copy(alpha = 0.1f)).padding(12.dp), color = c.danger, style = MaterialTheme.typography.bodyMedium)
        else -> {}
    }
}

