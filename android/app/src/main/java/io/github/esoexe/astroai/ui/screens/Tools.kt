package io.github.esoexe.astroai.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.esoexe.astroai.brain.Draft
import io.github.esoexe.astroai.brain.Payload
import io.github.esoexe.astroai.brain.now
import io.github.esoexe.astroai.core.Note
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Badge
import io.github.esoexe.astroai.ui.Card
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.EmptyState
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.IconBtn
import io.github.esoexe.astroai.ui.Markdown
import io.github.esoexe.astroai.ui.Mono
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import io.github.esoexe.astroai.ui.secs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ---------------- reader ----------------

private class ReaderDoc(val title: String, val url: String, val text: String)
private object ReaderState { var doc by mutableStateOf<ReaderDoc?>(null); var results = mutableStateListOf<org.json.JSONObject>() }

private fun askAbout(doc: ReaderDoc, instruction: String) {
    app.chat.newChat(); app.ui.go("chat")
    val body = tr(instruction) + "\n\n" + "# ${doc.title}\n${doc.url}\n\n" + doc.text.take(10_000)
    app.chat.send(body)
}

@Composable
fun ReaderScreen(onMenu: () -> Unit) {
    val scope = rememberCoroutineScope()
    var input by remember { mutableStateOf(app.ui.readerUrl) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf("") }
    var question by remember { mutableStateOf("") }
    val doc = ReaderState.doc

    fun go(raw: String) {
        val v = raw.trim(); if (v.isEmpty()) return
        error = ""; busy = true
        scope.launch {
            try {
                if (Regex("^(https?://)?[\\w-]+(\\.[\\w-]+)+(/\\S*)?$").matches(v)) {
                    val url = if (v.startsWith("http")) v else "https://$v"
                    val j = Api.post("/api/web/read", jsonOf("url" to url, "max_chars" to 60_000), 60_000)
                    if (j.str("error").isNotEmpty() && j.str("text").isEmpty()) throw IllegalStateException(j.str("error"))
                    ReaderState.doc = ReaderDoc(j.str("title").ifEmpty { url }, j.str("url").ifEmpty { url }, j.str("text"))
                    ReaderState.results.clear()
                } else {
                    val j = Api.post("/api/web/search", jsonOf("query" to v, "count" to 8), 40_000)
                    ReaderState.results.clear(); ReaderState.results.addAll(j.optJSONArray("results")?.objects().orEmpty())
                    ReaderState.doc = null
                    if (ReaderState.results.isEmpty()) error = j.str("error").ifEmpty { tr("Nothing found") }
                }
            } catch (e: CancellationException) { throw e } catch (e: Exception) { error = e.message ?: tr("Could not open it") }
            busy = false
        }
    }
    LaunchedEffect(app.ui.readerUrl) { if (app.ui.readerUrl.isNotEmpty()) { input = app.ui.readerUrl; go(app.ui.readerUrl); app.ui.readerUrl = "" } }
    val pickFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            busy = true; error = ""
            try { val d = withContext(Dispatchers.IO) { readDocument(uri) }; ReaderState.doc = ReaderDoc(d.name, "", d.text); ReaderState.results.clear() }
            catch (e: Exception) { error = e.message ?: "" }
            busy = false
        }
    }

    Screen(T("Reader"), onMenu, T("Read any page or file, then summarise, ask or save"), actions = {
        IconBtn(AppIcons.file, T("Open a file"), { pickFile.launch(arrayOf("*/*")) }, tint = c.text)
    }) {
        Column(Modifier.fillMaxSize().imePadding()) {
            Row(Modifier.padding(horizontal = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.weight(1f)) { Field(input, { input = it }, "", placeholder = T("Paste a link, or search the web"), keyboard = KeyboardType.Uri, onDone = { go(input) }) }
                Spacer(Modifier.width(8.dp))
                Primary(T("Go"), { go(input) }, enabled = !busy && input.isNotBlank())
            }
            if (busy) Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) { ThinkingDots(); Spacer(Modifier.width(10.dp)); Text(T("Reading…"), color = c.muted) }
            if (error.isNotEmpty()) Text(error, Modifier.padding(16.dp), color = c.danger, style = MaterialTheme.typography.bodyMedium)
            when {
                doc != null -> Column(Modifier.weight(1f)) {
                    Row(Modifier.padding(horizontal = 14.dp, vertical = 8.dp).horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Chip(T("Summarise"), icon = AppIcons.sparkles, onClick = { askAbout(doc, "Summarise this in clear bullet points, then give the key takeaway.") })
                        Chip(T("Explain simply"), icon = AppIcons.cap, onClick = { askAbout(doc, "Explain this simply, as to a curious beginner.") })
                        Chip(T("Save as note"), icon = AppIcons.note, onClick = {
                            app.store.notes.add(0, Note(title = doc.title.take(200), body = (if (doc.url.isNotEmpty()) doc.url + "\n\n" else "") + doc.text.take(50_000))); app.store.saveNotes()
                            app.ui.toast(tr("Saved to Notes"))
                        })
                        if (doc.url.isNotEmpty()) Chip(T("Open in browser"), icon = AppIcons.ext, onClick = { app.ui.openUrl(doc.url) })
                        Chip(T("Copy"), icon = AppIcons.copy, onClick = { app.ui.copy(doc.text); app.ui.toast(tr("Copied")) })
                    }
                    Row(Modifier.padding(horizontal = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.weight(1f)) { Field(question, { question = it }, "", placeholder = T("Ask a question about it"), onDone = { if (question.isNotBlank()) askAbout(doc, question) }) }
                        IconBtn(AppIcons.send, T("Ask"), { if (question.isNotBlank()) askAbout(doc, question) }, tint = c.accent)
                    }
                    LazyColumn(Modifier.weight(1f), contentPadding = PaddingValues(18.dp)) {
                        item {
                            Text(doc.title, style = MaterialTheme.typography.headlineSmall, color = c.text)
                            if (doc.url.isNotEmpty()) Text(io.github.esoexe.astroai.brain.Text.hostOf(doc.url), style = MaterialTheme.typography.labelMedium, color = c.accent)
                            Text(T("# words", doc.text.split(Regex("\\s+")).size), style = MaterialTheme.typography.labelSmall, color = c.faint, modifier = Modifier.padding(top = 2.dp, bottom = 12.dp))
                        }
                        items(doc.text.split(Regex("\n{2,}")).filter { it.isNotBlank() }.take(600)) { p ->
                            SelectionContainer { Text(p.trim(), Modifier.padding(bottom = 12.dp), style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 26.sp), color = c.text) }
                        }
                    }
                }
                ReaderState.results.isNotEmpty() -> LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(ReaderState.results) { r ->
                        Card(Modifier.fillMaxWidth(), onClick = { input = r.str("url"); go(r.str("url")) }) {
                            Column(Modifier.padding(14.dp)) {
                                Text(r.str("title"), style = MaterialTheme.typography.titleSmall, color = c.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
                                Text(io.github.esoexe.astroai.brain.Text.hostOf(r.str("url")), style = MaterialTheme.typography.labelSmall, color = c.accent)
                                if (r.str("snippet").isNotEmpty()) Text(r.str("snippet"), style = MaterialTheme.typography.bodySmall, color = c.muted, maxLines = 3, overflow = TextOverflow.Ellipsis)
                            }
                        }
                    }
                }
                !busy && error.isEmpty() -> EmptyState(AppIcons.read, T("Read anything"), T("Paste a link to a page or open a PDF, Word or text file. Words without a link search the web."))
            }
        }
    }
}

// ---------------- knowledge ----------------

@Composable
fun KnowledgeScreen(onMenu: () -> Unit) {
    val chat = app.chat
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf("") }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) scope.launch {
            for (u in uris.take(10)) {
                busy = tr("Reading #…", nameOf(u))
                try { chat.documents.add(withContext(Dispatchers.IO) { readDocument(u) }) } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
            }
            busy = ""
        }
    }
    Screen(T("Knowledge"), onMenu, T("Files the answers can quote from"), actions = {
        IconBtn(AppIcons.upload, T("Add files"), { pick.launch(arrayOf("*/*")) }, tint = c.text)
    }) {
        LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            item {
                Text(T("Add PDFs, Word files, notes or code. A.S.T.R.O AI finds the relevant parts and cites them in its answers. Files stay attached to the current chat and are read on this phone."),
                    style = MaterialTheme.typography.bodyMedium, color = c.muted)
                Spacer(Modifier.height(12.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Primary(T("Add files"), { pick.launch(arrayOf("*/*")) }, icon = AppIcons.upload)
                    if (chat.documents.isNotEmpty()) Secondary(T("Ask about them"), { app.ui.go("chat") }, icon = AppIcons.chat)
                }
                if (busy.isNotEmpty()) Row(Modifier.padding(top = 12.dp), verticalAlignment = Alignment.CenterVertically) { ThinkingDots(); Spacer(Modifier.width(8.dp)); Text(busy, color = c.muted) }
            }
            if (chat.documents.isEmpty() && busy.isEmpty()) item { EmptyState(AppIcons.book, T("No knowledge files yet")) }
            items(chat.documents.toList()) { d ->
                Card(Modifier.fillMaxWidth()) {
                    Row(Modifier.padding(start = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                        Ic(AppIcons.file, null, tint = c.accent)
                        Column(Modifier.weight(1f).padding(12.dp)) {
                            Text(d.name, style = MaterialTheme.typography.bodyLarge, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            Text(T("# characters", "%,d".format(d.text.length)), style = MaterialTheme.typography.bodySmall, color = c.muted)
                        }
                        IconBtn(AppIcons.trash, T("Remove"), { chat.documents.remove(d) })
                    }
                }
            }
        }
    }
}

// ---------------- calculator ----------------

private object CalcState { val history = mutableStateListOf<Pair<String, String>>() }

@Composable
fun CalculatorScreen(onMenu: () -> Unit) {
    val scope = rememberCoroutineScope()
    var expr by remember { mutableStateOf("") }
    var result by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    fun calc() {
        val e = expr.trim(); if (e.isEmpty()) return
        scope.launch {
            try {
                val j = Api.post("/api/calc", jsonOf("expression" to e), 15_000)
                result = j.opt("result")?.toString().orEmpty().ifEmpty { j.str("error") }
                if (j.str("error").isNotEmpty() && j.opt("result") == null) { error = j.str("error"); result = "" } else { error = ""; CalcState.history.add(0, e to result) }
            } catch (ex: Exception) { error = ex.message ?: ""; result = "" }
        }
    }
    val keys = listOf(
        listOf("C", "(", ")", "÷"), listOf("7", "8", "9", "×"), listOf("4", "5", "6", "−"), listOf("1", "2", "3", "+"),
        listOf("0", ".", "^", "="), listOf("sqrt(", "sin(", "cos(", "⌫"), listOf("pi", "log(", "%", "ln("),
    )
    Screen(T("Calculator"), onMenu, T("Exact maths, also usable from chat")) {
        Column(Modifier.fillMaxSize().padding(14.dp)) {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(c.surface).border(1.dp, c.border, RoundedCornerShape(16.dp)).padding(16.dp), horizontalAlignment = Alignment.End) {
                Field(expr, { expr = it }, "", mono = true, placeholder = "2^10 / (3 + 5)", onDone = { calc() })
                Spacer(Modifier.height(8.dp))
                SelectionContainer { Text(if (error.isNotEmpty()) error else if (result.isNotEmpty()) "= $result" else " ", style = MaterialTheme.typography.headlineMedium.copy(fontFamily = Mono), color = if (error.isNotEmpty()) c.danger else c.text, textAlign = TextAlign.End) }
            }
            Spacer(Modifier.height(12.dp))
            keys.forEach { row ->
                Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    row.forEach { k ->
                        val accent = k == "="; val op = k in setOf("÷", "×", "−", "+", "^", "C", "⌫")
                        Box(Modifier.weight(1f).height(48.dp).clip(RoundedCornerShape(12.dp)).background(if (accent) c.accent else if (op) c.surface2 else c.surface)
                            .clickable {
                                when (k) {
                                    "C" -> { expr = ""; result = ""; error = "" }
                                    "⌫" -> expr = expr.dropLast(1)
                                    "=" -> calc()
                                    "÷" -> expr += "/"; "×" -> expr += "*"; "−" -> expr += "-"
                                    else -> expr += k
                                }
                            }, contentAlignment = Alignment.Center) {
                            Text(k, style = MaterialTheme.typography.titleMedium, color = if (accent) c.onAccent else c.text)
                        }
                    }
                }
            }
            if (CalcState.history.isNotEmpty()) {
                SectionTitle(T("History"))
                LazyColumn(Modifier.weight(1f)) {
                    items(CalcState.history.take(50)) { (e, r) ->
                        Row(Modifier.fillMaxWidth().clickable { expr = e; result = r }.padding(vertical = 6.dp)) {
                            Text(e, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono), color = c.muted, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            Text("= $r", style = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono), color = c.text)
                        }
                    }
                }
            }
        }
    }
}

// ---------------- model lab ----------------

private class LabRun(val id: String, val name: String, val provider: String) { val d = Draft(id) }
private object LabState { var prompt by mutableStateOf(""); val runs = mutableStateListOf<LabRun>(); var job: Job? = null; var running by mutableStateOf(false) }

@Composable
fun LabScreen(onMenu: () -> Unit) {
    val prefs = app.prefs
    val models = app.backend.models.filter { !it.bool("paid") }
    var filter by remember { mutableStateOf("") }
    val selected = remember { mutableStateListOf<String>().also { l -> l.addAll(prefs.labSelected.split(',').filter { it.isNotBlank() && models.any { m -> m.str("id") == it } }) } }
    if (selected.isEmpty() && models.isNotEmpty()) selected.addAll(models.filter { io.github.esoexe.astroai.ui.screens.labBlocked(it).isEmpty() }.take(4).map { it.str("id") })
    var ticker by remember { mutableLongStateOf(now()) }
    LaunchedEffect(LabState.running) { while (LabState.running) { ticker = now(); delay(500) } }
    fun save() { prefs.labSelected = selected.joinToString(",") }

    fun run() {
        val p = LabState.prompt.trim(); if (p.isEmpty() || selected.isEmpty()) return
        LabState.runs.clear()
        LabState.runs.addAll(selected.mapNotNull { id -> models.firstOrNull { it.str("id") == id }?.let { LabRun(id, it.str("name").ifEmpty { it.str("label") }, it.str("provider_name")) } })
        LabState.running = true
        LabState.job = app.chat.scope.launch {
            val payload = Payload(listOf("user" to p), mode = prefs.mode, language = prefs.language, stage = "expert", strict = true, maxTokens = 1400)
            try {
                coroutineScope { LabState.runs.map { r -> async { try { r.d.started = now(); app.chat.streamDraft(r.d, payload); if (r.d.ms == 0L) r.d.ms = now() - r.d.started } catch (e: CancellationException) { throw e } catch (e: Exception) { r.d.status = "error"; r.d.error = e.message ?: "" } } }.awaitAll() }
            } finally {
                LabState.runs.forEach { if (it.d.status == "streaming") { it.d.status = "stopped" } }
                LabState.running = false
            }
        }
    }

    Screen(T("Model Lab"), onMenu, T("# free models · test them side by side", models.size), actions = {
        if (LabState.runs.isNotEmpty() && !LabState.running) IconBtn(AppIcons.export, T("Export"), {
            app.ui.shareText("model-lab.md", "# Model Lab\n\n**Prompt:** ${LabState.prompt}\n\n" + LabState.runs.joinToString("\n\n") { "## ${it.name} (${it.provider}) · ${secs(it.d.ms)}\n\n${it.d.content.ifEmpty { it.d.error }}" })
        }, tint = c.text)
    }) {
        LazyColumn(contentPadding = PaddingValues(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            item {
                Field(LabState.prompt, { LabState.prompt = it }, T("Prompt"), singleLine = false, minLines = 3, placeholder = T("e.g. Explain recursion in 3 sentences."))
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (LabState.running) Primary(T("Stop"), { LabState.job?.cancel() }, icon = AppIcons.stop)
                    else Primary(T("Run on # model(s)", selected.size), { run() }, enabled = selected.isNotEmpty() && LabState.prompt.isNotBlank(), icon = AppIcons.play)
                }
            }
            if (LabState.runs.isNotEmpty()) {
                item { SectionTitle(T("Results")) }
                items(LabState.runs.sortedBy { if (it.d.status == "done") it.d.ms else Long.MAX_VALUE }, key = { "r" + it.id }) { r ->
                    val d = r.d
                    Card(Modifier.fillMaxWidth()) {
                        Column(Modifier.padding(14.dp)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(r.name, style = MaterialTheme.typography.titleSmall, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    Text(r.provider, style = MaterialTheme.typography.labelSmall, color = c.muted)
                                }
                                when (d.status) {
                                    "streaming" -> { ThinkingDots(); Spacer(Modifier.width(8.dp)); Text(secs(ticker - d.started), style = MaterialTheme.typography.labelSmall, color = c.muted) }
                                    "done" -> Badge(secs(d.ms), c.ok)
                                    else -> Badge(d.status, c.danger)
                                }
                            }
                            if (d.content.isNotEmpty()) { Spacer(Modifier.height(8.dp)); Box(Modifier.heightIn(max = 420.dp)) { Markdown(d.content, streaming = d.status == "streaming") } }
                            if (d.error.isNotEmpty()) Text(d.error, style = MaterialTheme.typography.bodySmall, color = c.danger, modifier = Modifier.padding(top = 6.dp))
                        }
                    }
                }
            }
            item {
                SectionTitle(T("Models"))
                Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Chip(T("Fastest 4"), onClick = { selected.clear(); selected.addAll(models.filter { labBlocked(it).isEmpty() }.take(4).map { it.str("id") }); save() })
                    Chip(T("8"), onClick = { selected.clear(); selected.addAll(models.filter { labBlocked(it).isEmpty() }.take(8).map { it.str("id") }); save() })
                    Chip(T("All ready"), onClick = { selected.clear(); selected.addAll(models.filter { labBlocked(it).isEmpty() }.map { it.str("id") }); save() })
                    Chip(T("None"), onClick = { selected.clear(); save() })
                }
                Spacer(Modifier.height(6.dp))
                Field(filter, { filter = it }, "", placeholder = T("Filter models"))
            }
            val shown = models.filter { filter.isBlank() || "${it.str("name")} ${it.str("label")} ${it.str("provider_name")} ${it.str("id")}".contains(filter, true) }
            items(shown, key = { "m" + it.str("id") }) { m ->
                val id = m.str("id"); val blocked = labBlocked(m)
                Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).clickable { if (id in selected) selected.remove(id) else selected.add(id); save() }.padding(vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    androidx.compose.material3.Checkbox(id in selected, { if (it) selected.add(id) else selected.remove(id); save() },
                        colors = androidx.compose.material3.CheckboxDefaults.colors(checkedColor = c.accent, checkmarkColor = c.onAccent, uncheckedColor = c.faint))
                    Column(Modifier.weight(1f)) {
                        Text(m.str("name").ifEmpty { m.str("label") }, style = MaterialTheme.typography.bodyMedium, color = if (blocked.isEmpty()) c.text else c.faint, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(listOf(m.str("provider_name"), blocked).filter { it.isNotEmpty() }.joinToString(" · "), style = MaterialTheme.typography.labelSmall, color = c.muted, maxLines = 1)
                    }
                }
            }
        }
    }
}

fun labBlocked(m: org.json.JSONObject): String = when (val v = m.opt("blocked")) { is String -> v; true -> tr("unavailable"); else -> "" }

@Suppress("unused") private val keepFw = FontWeight.Normal
@Suppress("unused") private val keepAr = Modifier.aspectRatio(1f)
