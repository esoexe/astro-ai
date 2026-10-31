package io.github.esoexe.astroai.ui.screens

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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
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
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.ConfirmDialog
import io.github.esoexe.astroai.ui.EmptyState
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Highlighter
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.IconBtn
import io.github.esoexe.astroai.ui.Mono
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.PromptDialog
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.TopBar
import io.github.esoexe.astroai.ui.ago
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import io.github.esoexe.astroai.ui.secs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.json.JSONObject

/** Workspace state shared with chat ("Open in Workspace") and the agent. */
object Ws {
    var projects by mutableStateOf<List<String>>(emptyList())
    var languages by mutableStateOf<List<String>>(emptyList())
    var previewKey = ""
    var files by mutableStateOf<List<JSONObject>>(emptyList())
    var dirs by mutableStateOf<List<String>>(emptyList())
    var openFile by mutableStateOf<String?>(null)
    var tab by mutableStateOf("files")
    val out = mutableStateListOf<Pair<String, String>>()   // kind (cmd, out, err, sys, exit) to text
    var running by mutableStateOf(false)
    var runJob: Job? = null
    val project: String get() = app.prefs.wsProject

    suspend fun loadProjects() {
        val j = Api.get("/api/fs/projects")
        projects = j.optJSONArray("projects")?.objects().orEmpty().sortedByDescending { it.long("mtime") }.map { it.str("name") }
        languages = j.optJSONArray("languages")?.strings().orEmpty()
        previewKey = j.str("preview_key")
        if (project !in projects) app.prefs.wsProject = projects.firstOrNull() ?: ""
    }

    suspend fun ensureProject(): String {
        if (projects.isEmpty()) loadProjects()
        if (project.isEmpty()) {
            Api.post("/api/fs/project/create", jsonOf("project" to "phone", "starter" to false))
            app.prefs.wsProject = "phone"; loadProjects()
        }
        return project
    }

    suspend fun loadTree() {
        if (project.isEmpty()) return
        val j = Api.post("/api/fs/tree", jsonOf("project" to project))
        files = j.optJSONArray("files")?.objects().orEmpty().sortedBy { it.str("path").lowercase() }
        dirs = j.optJSONArray("dirs")?.strings().orEmpty()
    }

    fun push(kind: String, s: String) {
        val clean = s.replace(Regex("\u001B\\[[0-?]*[ -/]*[@-~]"), "").replace("\r\n", "\n")
        val last = out.lastOrNull()
        if (last != null && last.first == kind && (kind == "out" || kind == "err") && last.second.length < 20_000) out[out.size - 1] = kind to (last.second + clean) else out.add(kind to clean)
        while (out.size > 400) out.removeAt(0)
    }

    /** Runs a file or a shell command in the project, streaming its output. */
    fun run(body: JSONObject, title: String) {
        runJob?.cancel()
        tab = "output"; running = true
        push("cmd", "▶ $title")
        runJob = app.chat.scope.launch {
            try {
                Api.sse("/api/run", body.put("project", project).put("timeout", body.optInt("timeout", 60))).collect { m ->
                    when {
                        m.opt("start") is String && body.has("file") -> push("sys", "$ " + m.getString("start"))
                        m.opt("out") is String -> push("out", m.getString("out"))
                        m.opt("err") is String -> push("err", m.getString("err"))
                        m.has("exit") -> {
                            val code = m.int("exit")
                            val stats = m.optJSONObject("stats")
                            val mem = stats?.long("max_rss_kb")?.takeIf { it > 0 }?.let { " · ${it / 1024} MB" } ?: ""
                            push("exit", (if (code == 0) tr("Finished in #", secs(m.long("ms"))) else tr("Exit code # · #", code, secs(m.long("ms")))) + mem + (m.str("error").takeIf { it.isNotEmpty() }?.let { " · $it" } ?: ""))
                        }
                    }
                }
            } catch (e: CancellationException) { push("sys", tr("Stopped")); throw e } catch (e: Exception) { push("err", (e.message ?: "Failed") + "\n") }
            finally { running = false; try { loadTree() } catch (_: Exception) { } }
        }
    }
}

private val RUNNABLE = setOf("py", "js", "mjs", "ts", "c", "cpp", "cc", "java", "go", "rs", "sh", "rb", "php", "lua", "kt")

@Composable
fun WorkspaceScreen(onMenu: () -> Unit) {
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf("") }
    var projMenu by remember { mutableStateOf(false) }
    var creating by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        try { Ws.loadProjects(); Ws.loadTree() } catch (e: Exception) { error = e.message ?: "" }
        loading = false
        if (Ws.openFile != null) Ws.tab = "code"
    }
    Column(Modifier.fillMaxSize().navigationBarsPadding().imePadding()) {
        TopBar(T("Workspace"), Ws.project.ifEmpty { T("No project") }, onMenu = onMenu) {
            Box {
                IconBtn(AppIcons.folder, T("Projects"), { projMenu = true }, tint = c.text)
                DropdownMenu(projMenu, { projMenu = false }, containerColor = c.surface) {
                    Ws.projects.forEach { p ->
                        DropdownMenuItem({ Text(p) }, {
                            projMenu = false; app.prefs.wsProject = p; Ws.openFile = null; Ws.out.clear(); Ws.tab = "files"
                            app.chat.scope.launch { try { Ws.loadTree() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
                        }, trailingIcon = { if (p == Ws.project) Ic(AppIcons.check, tint = c.accent) })
                    }
                    HorizontalDivider(color = c.border)
                    DropdownMenuItem({ Text(T("New project")) }, { projMenu = false; creating = true }, leadingIcon = { Ic(AppIcons.plus) })
                }
            }
            IconBtn(AppIcons.agent, T("Agent on this project"), { app.ui.go("agent") }, tint = c.text)
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf("files" to "Files", "code" to "Code", "output" to "Output", "tools" to "Tools").forEach { (k, l) ->
                Chip(T(l) + if (k == "output" && Ws.running) " •" else "", selected = Ws.tab == k, onClick = { Ws.tab = k })
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when {
                loading -> Row(Modifier.padding(20.dp)) { ThinkingDots() }
                error.isNotEmpty() -> EmptyState(AppIcons.alert, T("Could not open the workspace"), error)
                else -> when (Ws.tab) {
                    "code" -> EditorPane()
                    "output" -> OutputPane()
                    "tools" -> ToolsPane()
                    else -> FilesPane()
                }
            }
        }
    }
    if (creating) PromptDialog(T("New project"), "", T("Name (letters, numbers, - and _)"), T("Create"), onConfirm = { raw ->
        val name = raw.trim().replace(Regex("[^A-Za-z0-9_-]+"), "-").trim('-').take(60)
        app.chat.scope.launch {
            try { Api.post("/api/fs/project/create", jsonOf("project" to name, "starter" to false)); app.prefs.wsProject = name; Ws.loadProjects(); Ws.loadTree(); Ws.openFile = null; Ws.tab = "files" }
            catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        }
    }, onDismiss = { creating = false })
}

@Composable
private fun FilesPane() {
    var newFile by remember { mutableStateOf(false) }
    var newDir by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf<String?>(null) }
    var deleting by remember { mutableStateOf<String?>(null) }
    fun refresh() { app.chat.scope.launch { try { Ws.loadTree() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } } }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.padding(horizontal = 12.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(T("New file"), icon = AppIcons.newFile, onClick = { newFile = true })
            Chip(T("New folder"), icon = AppIcons.newFolder, onClick = { newDir = true })
            Chip(T("Refresh"), icon = AppIcons.refresh, onClick = { refresh() })
        }
        if (Ws.files.isEmpty() && Ws.dirs.isEmpty()) EmptyState(AppIcons.folderOpen, T("This project is empty"), T("Create a file, or send code from chat with “Open in Workspace”."))
        LazyColumn(contentPadding = PaddingValues(12.dp)) {
            val entries = (Ws.dirs.map { it to true } + Ws.files.map { it.str("path") to false }).sortedBy { it.first.lowercase() }
            items(entries, key = { it.first + it.second }) { (path, isDir) ->
                val depth = path.count { it == '/' }
                var menu by remember { mutableStateOf(false) }
                Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(if (Ws.openFile == path) c.accentSoft else Color.Transparent)
                    .clickable(enabled = !isDir) { Ws.openFile = path; Ws.tab = "code" }.padding(start = (8 + depth * 14).dp, top = 2.dp, bottom = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                    Ic(if (isDir) AppIcons.folder else AppIcons.file, null, tint = if (isDir) c.accent else c.muted, size = 16.dp)
                    Spacer(Modifier.width(8.dp))
                    Text(path.substringAfterLast('/'), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono, fontSize = 13.sp), color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (!isDir) Ws.files.firstOrNull { it.str("path") == path }?.let { f -> Text(size(f.long("size")), style = MaterialTheme.typography.labelSmall, color = c.faint) }
                    Box {
                        IconBtn(AppIcons.more, T("More"), { menu = true }, size = 16.dp)
                        DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                            if (!isDir && path.substringAfterLast('.', "").lowercase() in RUNNABLE) DropdownMenuItem({ Text(T("Run")) }, { menu = false; Ws.run(jsonOf("file" to path), path) }, leadingIcon = { Ic(AppIcons.play) })
                            if (!isDir && path.endsWith(".html")) DropdownMenuItem({ Text(T("Preview in browser")) }, { menu = false; preview(path) }, leadingIcon = { Ic(AppIcons.eye) })
                            DropdownMenuItem({ Text(T("Rename")) }, { menu = false; renaming = path }, leadingIcon = { Ic(AppIcons.edit) })
                            if (!isDir) DropdownMenuItem({ Text(T("Share")) }, {
                                menu = false
                                app.chat.scope.launch { try { val j = Api.post("/api/fs/read", jsonOf("project" to Ws.project, "path" to path)); app.ui.shareText(path.substringAfterLast('/'), j.str("content")) } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
                            }, leadingIcon = { Ic(AppIcons.export) })
                            DropdownMenuItem({ Text(T("Delete"), color = c.danger) }, { menu = false; deleting = path }, leadingIcon = { Ic(AppIcons.trash, tint = c.danger) })
                        }
                    }
                }
            }
        }
    }
    if (newFile) PromptDialog(T("New file"), "", T("Path, e.g. main.py or src/app.js"), T("Create"), onConfirm = { p ->
        app.chat.scope.launch { try { Api.post("/api/fs/write", jsonOf("project" to Ws.project, "path" to p.trim(), "content" to "")); Ws.loadTree(); Ws.openFile = p.trim(); Ws.tab = "code" } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
    }, onDismiss = { newFile = false })
    if (newDir) PromptDialog(T("New folder"), "", T("Folder path"), T("Create"), onConfirm = { p ->
        app.chat.scope.launch { try { Api.post("/api/fs/mkdir", jsonOf("project" to Ws.project, "path" to p.trim())); Ws.loadTree() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
    }, onDismiss = { newDir = false })
    renaming?.let { from -> PromptDialog(T("Rename"), from, T("New path"), T("Rename"), onConfirm = { to ->
        app.chat.scope.launch { try { Api.post("/api/fs/rename", jsonOf("project" to Ws.project, "path" to from, "to" to to.trim())); if (Ws.openFile == from) Ws.openFile = to.trim(); Ws.loadTree() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
    }, onDismiss = { renaming = null }) }
    deleting?.let { p -> ConfirmDialog(T("Delete #?", p), T("A checkpoint is made first, so you can restore it from Tools."), T("Delete"), {
        app.chat.scope.launch {
            try {
                Api.post("/api/ide/history/create", jsonOf("project" to Ws.project, "label" to "Before deleting $p"))
                Api.post("/api/fs/delete", jsonOf("project" to Ws.project, "path" to p)); if (Ws.openFile == p) Ws.openFile = null; Ws.loadTree()
            } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        }
    }, { deleting = null }) }
}

private fun size(b: Long) = when { b < 1024 -> "$b B"; b < 1024 * 1024 -> "${b / 1024} KB"; else -> "%.1f MB".format(b / 1048576.0) }

private fun preview(path: String) {
    if (Ws.previewKey.isEmpty() || Api.base.isEmpty()) return
    val url = "${Api.base}/preview/${Ws.previewKey}/${Uri.encode(Ws.project)}/" + path.split('/').joinToString("/") { Uri.encode(it) }
    app.ui.openUrl(url)
}

private typealias Uri = android.net.Uri

private class CodeHighlight(private val lang: String, private val dark: Boolean) : VisualTransformation {
    override fun filter(text: AnnotatedString): TransformedText =
        if (text.length > 40_000) TransformedText(text, OffsetMapping.Identity) else TransformedText(Highlighter.highlight(text.text, lang, dark), OffsetMapping.Identity)
}

@Composable
private fun EditorPane() {
    val path = Ws.openFile
    if (path == null) { EmptyState(AppIcons.code, T("No file open"), T("Pick a file in Files.")); return }
    var value by remember(path) { mutableStateOf(TextFieldValue("")) }
    var saved by remember(path) { mutableStateOf("") }
    var loading by remember(path) { mutableStateOf(true) }
    var binary by remember(path) { mutableStateOf(false) }
    var ask by remember { mutableStateOf(false) }
    LaunchedEffect(path) {
        try {
            val j = Api.post("/api/fs/read", jsonOf("project" to Ws.project, "path" to path))
            binary = j.bool("binary"); saved = j.str("content"); value = TextFieldValue(saved)
        } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        loading = false
    }
    val dirty = value.text != saved
    fun save(then: (() -> Unit)? = null) {
        val content = value.text
        app.chat.scope.launch {
            try { Api.post("/api/fs/write", jsonOf("project" to Ws.project, "path" to path, "content" to content)); saved = content; then?.invoke() }
            catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        }
    }
    val ext = path.substringAfterLast('.', "").lowercase()
    val line = value.text.take(value.selection.start).count { it == '\n' } + 1
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(path + if (dirty) " •" else "", Modifier.weight(1f), style = MaterialTheme.typography.labelLarge.copy(fontFamily = Mono), color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(T("Ln #", line), style = MaterialTheme.typography.labelSmall, color = c.faint)
            IconBtn(AppIcons.save, T("Save"), { save() }, enabled = dirty, tint = c.accent)
            if (ext in RUNNABLE) IconBtn(AppIcons.play, T("Run"), { save { Ws.run(jsonOf("file" to path), path) } }, tint = c.ok)
            if (ext == "html") IconBtn(AppIcons.eye, T("Preview in browser"), { save { preview(path) } })
            IconBtn(AppIcons.sparkles, T("Ask AI about this file"), { ask = true })
        }
        if (loading) ThinkingDots()
        else if (binary) EmptyState(AppIcons.file, T("This file is binary"), T("It can't be edited here."))
        else Box(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 8.dp).clip(RoundedCornerShape(10.dp)).background(c.codeBg).border(1.dp, c.border, RoundedCornerShape(10.dp))
            .verticalScroll(rememberScrollState()).padding(10.dp)) {
            val colors = c
            BasicTextField(value, { value = it }, Modifier.fillMaxWidth(),
                textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono, fontSize = 13.sp, lineHeight = 19.sp, color = colors.text),
                cursorBrush = SolidColor(colors.accent), visualTransformation = remember(ext, colors.dark) { CodeHighlight(ext, colors.dark) },
                keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(autoCorrectEnabled = false, keyboardType = androidx.compose.ui.text.input.KeyboardType.Ascii))
        }
        // Symbols that are slow to type on a phone keyboard.
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(6.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            listOf("⇥", "{", "}", "(", ")", "[", "]", ";", ":", "=", "\"", "'", "<", ">", "/", "\\", "|", "&", "!", "_", "#").forEach { k ->
                Box(Modifier.clip(RoundedCornerShape(8.dp)).background(c.surface2).clickable {
                    val ins = if (k == "⇥") "    " else k
                    val s = value.selection.start; val e = value.selection.end
                    value = TextFieldValue(value.text.substring(0, s) + ins + value.text.substring(e), androidx.compose.ui.text.TextRange(s + ins.length))
                }.padding(horizontal = 12.dp, vertical = 8.dp)) { Text(k, style = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono), color = c.text) }
            }
        }
    }
    if (ask) PromptDialog(T("Ask AI about #", path.substringAfterLast('/')), "", T("e.g. Find the bug, or add comments"), T("Ask"), onConfirm = { q ->
        app.prefs.mode = "coding"; app.chat.newChat(); app.ui.go("chat")
        app.chat.send("$q\n\nFile `$path` (project ${Ws.project}):\n```$ext\n${value.text.take(9000)}\n```")
    }, onDismiss = { ask = false })
}

@Composable
private fun OutputPane() {
    var cmd by remember { mutableStateOf("") }
    val list = rememberLazyListState()
    LaunchedEffect(Ws.out.size, Ws.out.lastOrNull()?.second?.length) { if (Ws.out.isNotEmpty()) list.scrollToItem(Ws.out.lastIndex) }
    Column(Modifier.fillMaxSize()) {
        SelectionContainer(Modifier.weight(1f)) {
            LazyColumn(state = list, modifier = Modifier.fillMaxSize().padding(horizontal = 8.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFF0D0D10)), contentPadding = PaddingValues(10.dp)) {
                if (Ws.out.isEmpty()) item { Text(T("Run a file, or type a command below (one at a time; colour codes are removed)."), color = Color(0xFF8A8F98), style = MaterialTheme.typography.bodySmall) }
                items(Ws.out.size) { i ->
                    val (k, s) = Ws.out[i]
                    Text(s.trimEnd('\n'), style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono, fontSize = 12.5.sp, lineHeight = 18.sp),
                        color = when (k) { "err" -> Color(0xFFFF9C9C); "cmd" -> Color(0xFFF3A679); "sys", "exit" -> Color(0xFF8A8F98); else -> Color(0xFFE6E6E6) })
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("$", style = MaterialTheme.typography.bodyLarge.copy(fontFamily = Mono), color = c.accent, modifier = Modifier.padding(end = 8.dp))
            Box(Modifier.weight(1f)) { Field(cmd, { cmd = it }, "", mono = true, placeholder = "ls -la", onDone = { if (cmd.isNotBlank() && !Ws.running) { Ws.run(jsonOf("cmd" to cmd, "timeout" to 120), cmd); cmd = "" } }) }
            if (Ws.running) IconBtn(AppIcons.stop, T("Stop"), { Ws.runJob?.cancel() }, tint = c.danger)
            else IconBtn(AppIcons.send, T("Run"), { if (cmd.isNotBlank()) { Ws.run(jsonOf("cmd" to cmd, "timeout" to 120), cmd); cmd = "" } }, tint = c.accent)
            IconBtn(AppIcons.trash, T("Clear"), { Ws.out.clear() })
        }
        if (Ws.languages.isNotEmpty()) Text(T("Can run here: #", Ws.languages.joinToString(", ")), Modifier.padding(start = 12.dp, bottom = 6.dp), style = MaterialTheme.typography.labelSmall, color = c.faint)
    }
}

@Composable
private fun ToolsPane() {
    var query by remember { mutableStateOf("") }
    var hits by remember { mutableStateOf<List<JSONObject>>(emptyList()) }
    var git by remember { mutableStateOf<JSONObject?>(null) }
    var checkpoints by remember { mutableStateOf<List<JSONObject>>(emptyList()) }
    var commitMsg by remember { mutableStateOf("") }
    var restoring by remember { mutableStateOf<JSONObject?>(null) }
    fun reload() {
        app.chat.scope.launch {
            try { git = Api.post("/api/ide/git", jsonOf("project" to Ws.project, "op" to "status")) } catch (_: Exception) { }
            try { checkpoints = Api.post("/api/ide/history/list", jsonOf("project" to Ws.project)).optJSONArray("checkpoints")?.objects().orEmpty() } catch (_: Exception) { }
        }
    }
    LaunchedEffect(Ws.project) { reload() }
    LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        item {
            SectionTitle(T("Search in project"))
            Field(query, { query = it }, "", placeholder = T("Text to find"), onDone = {
                app.chat.scope.launch { try { hits = Api.post("/api/ide/search", jsonOf("project" to Ws.project, "query" to query)).optJSONArray("files")?.objects().orEmpty() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
            })
        }
        items(hits) { f ->
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { Ws.openFile = f.str("path"); Ws.tab = "code" }.padding(6.dp)) {
                Text(f.str("path"), style = MaterialTheme.typography.labelLarge.copy(fontFamily = Mono), color = c.accent)
                f.optJSONArray("hits")?.objects().orEmpty().take(5).forEach { h -> Text("${h.int("line")}: ${h.str("text").trim()}", style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.muted, maxLines = 1, overflow = TextOverflow.Ellipsis) }
            }
        }
        item {
            SectionTitle(T("Checkpoints"))
            Text(T("Snapshots of the whole project. The agent makes one before it changes anything."), style = MaterialTheme.typography.bodySmall, color = c.muted)
            Spacer(Modifier.height(6.dp))
            Secondary(T("Make a checkpoint now"), {
                app.chat.scope.launch { try { Api.post("/api/ide/history/create", jsonOf("project" to Ws.project, "label" to tr("Manual checkpoint"))); app.ui.toast(tr("Checkpoint saved")); reload() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
            }, icon = AppIcons.history)
        }
        items(checkpoints.take(30), key = { it.str("id") }) { cp ->
            Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.surface).padding(start = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f).padding(vertical = 8.dp)) {
                    Text(cp.str("label").ifEmpty { cp.str("kind") }, style = MaterialTheme.typography.bodyMedium, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(T("# files · #", cp.int("files"), ago(cp.long("ts"))), style = MaterialTheme.typography.labelSmall, color = c.muted)
                }
                Text(T("Restore"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { restoring = cp }.padding(10.dp), color = c.accent, style = MaterialTheme.typography.labelMedium)
            }
        }
        item {
            SectionTitle(T("Git"))
            val g = git
            when {
                g == null -> ThinkingDots()
                !g.bool("repo") -> Secondary(T("Start a Git repository"), {
                    app.chat.scope.launch { try { Api.post("/api/ide/git", jsonOf("project" to Ws.project, "op" to "init")); reload() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
                }, icon = AppIcons.git)
                else -> Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Badge(g.str("branch").ifEmpty { "main" }); Spacer(Modifier.width(8.dp))
                        Text(T("# changed files", g.optJSONArray("files")?.length() ?: 0), style = MaterialTheme.typography.bodySmall, color = c.muted)
                    }
                    g.optJSONArray("files")?.objects().orEmpty().take(20).forEach { f -> Text("${f.str("x")}${f.str("y")}  ${f.str("path")}", style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.muted) }
                    if ((g.optJSONArray("files")?.length() ?: 0) > 0) {
                        Spacer(Modifier.height(6.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.weight(1f)) { Field(commitMsg, { commitMsg = it }, "", placeholder = T("Commit message")) }
                            Spacer(Modifier.width(8.dp))
                            Primary(T("Commit"), {
                                app.chat.scope.launch {
                                    try { val r = Api.post("/api/ide/git", jsonOf("project" to Ws.project, "op" to "commit", "message" to commitMsg.ifBlank { "Update from phone" }, "all" to true)); app.ui.toast(tr("Committed #", r.str("hash"))); commitMsg = ""; reload() }
                                    catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
                                }
                            })
                        }
                    }
                }
            }
        }
        item {
            SectionTitle(T("Project"))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Secondary(T("Share as ZIP"), {
                    app.chat.scope.launch { try { app.ui.shareBytes("${Ws.project}.zip", Api.bytes("/api/ide/zip", jsonOf("project" to Ws.project))) } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
                }, icon = AppIcons.download)
            }
        }
    }
    restoring?.let { cp -> ConfirmDialog(T("Restore this checkpoint?"), T("Files go back to “#”. A checkpoint of the current state is made first.", cp.str("label")), T("Restore"), {
        app.chat.scope.launch {
            try {
                Api.post("/api/ide/history/create", jsonOf("project" to Ws.project, "label" to tr("Before restoring")))
                Api.post("/api/ide/history/restore", jsonOf("project" to Ws.project, "id" to cp.str("id"))); Ws.loadTree(); Ws.openFile = null; reload(); app.ui.toast(tr("Restored"))
            } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
        }
    }, { restoring = null }, danger = false) }
}

