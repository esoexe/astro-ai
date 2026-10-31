package io.github.esoexe.astroai.ui.screens

import io.github.esoexe.astroai.MicPermission
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Base64
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
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
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.brain.ActionCard
import io.github.esoexe.astroai.brain.BrainInfo
import io.github.esoexe.astroai.brain.Doc
import io.github.esoexe.astroai.brain.Draft
import io.github.esoexe.astroai.brain.Msg
import io.github.esoexe.astroai.brain.OneBrain
import io.github.esoexe.astroai.brain.now
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Badge
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.ConfirmDialog
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.IconBtn
import io.github.esoexe.astroai.ui.Markdown
import io.github.esoexe.astroai.ui.OrbitLogo
import io.github.esoexe.astroai.ui.PromptDialog
import io.github.esoexe.astroai.ui.PulseDot
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.TopBar
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import io.github.esoexe.astroai.ui.secs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.ByteArrayOutputStream

val MODES = listOf("general" to "General", "coding" to "Coding", "writing" to "Writing", "research" to "Research", "tutor" to "Tutor", "planning" to "Planning")

fun modelLabel(id: String): String = when (id) {
    "brain" -> tr("One Brain")
    "auto" -> tr("Auto")
    else -> app.backend.models.firstOrNull { it.str("id") == id }?.let { it.str("name").ifEmpty { it.str("label") } } ?: id.substringAfter(':')
}

@Composable
fun ChatScreen(onMenu: () -> Unit) {
    val chat = app.chat
    val prefs = app.prefs
    val list = rememberLazyListState()
    var picker by remember { mutableStateOf(false) }
    var menu by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf(false) }
    val title = chat.activeId?.let { app.store.conversation(it)?.str("title") }?.ifEmpty { null } ?: "A.S.T.R.O AI"
    val sub = if (prefs.model == "brain") OneBrain.label(app) else modelLabel(prefs.model)

    // Follow the answer while it streams, unless the user scrolled up.
    val atBottom by remember { derivedStateOf { val l = list.layoutInfo; l.visibleItemsInfo.lastOrNull()?.index?.let { it >= l.totalItemsCount - 2 } ?: true } }
    val lastLen = chat.messages.lastOrNull()?.text?.length ?: 0
    LaunchedEffect(chat.messages.size) { if (chat.messages.isNotEmpty()) list.animateScrollToItem(chat.messages.lastIndex) }
    LaunchedEffect(lastLen) { if (atBottom && chat.messages.isNotEmpty()) list.scrollToItem(chat.messages.lastIndex, Int.MAX_VALUE / 2) }

    Column(Modifier.fillMaxSize().imePadding()) {
        TopBar(title, sub, onMenu = onMenu) {
            IconBtn(AppIcons.brain, T("Model"), { picker = true }, tint = c.text)
            Box {
                IconBtn(AppIcons.more, T("More"), { menu = true }, tint = c.text)
                DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                    DropdownMenuItem({ Text(T("New chat")) }, { menu = false; chat.newChat() }, leadingIcon = { Ic(AppIcons.plus) })
                    if (chat.activeId != null) {
                        val pinned = app.store.conversation(chat.activeId!!)?.bool("pinned") == true
                        DropdownMenuItem({ Text(if (pinned) T("Unpin") else T("Pin")) }, { menu = false; chat.togglePin(chat.activeId!!) }, leadingIcon = { Ic(AppIcons.pin) })
                        DropdownMenuItem({ Text(T("Rename")) }, { menu = false; renaming = true }, leadingIcon = { Ic(AppIcons.edit) })
                        DropdownMenuItem({ Text(T("Export as Markdown")) }, { menu = false; app.ui.shareText("chat.md", chat.markdown()) }, leadingIcon = { Ic(AppIcons.export) })
                        DropdownMenuItem({ Text(T("Delete"), color = c.danger) }, { menu = false; deleting = true }, leadingIcon = { Ic(AppIcons.trash, tint = c.danger) })
                    }
                    HorizontalDivider(color = c.border)
                    DropdownMenuItem({ Text(T("Live voice")) }, { menu = false; app.ui.go("live") }, leadingIcon = { Ic(AppIcons.wave) })
                    DropdownMenuItem({ Text(T("All chats")) }, { menu = false; app.ui.go("chats") }, leadingIcon = { Ic(AppIcons.history) })
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            if (chat.messages.isEmpty()) Welcome()
            else LazyColumn(state = list, modifier = Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 14.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)) {
                itemsIndexed(chat.messages, key = { i, m -> System.identityHashCode(m).toString() + i }) { i, m ->
                    if (m.role == "user") UserMessage(m, i) else AssistantMessage(m, i)
                }
            }
        }
        Composer()
    }
    if (picker) ModelPicker { picker = false }
    if (renaming) chat.activeId?.let { id -> PromptDialog(T("Rename chat"), app.store.conversation(id)?.str("title") ?: "", T("Title"), onConfirm = { chat.rename(id, it) }, onDismiss = { renaming = false }) }
    if (deleting) chat.activeId?.let { id -> ConfirmDialog(T("Delete this chat?"), T("You can undo right after."), T("Delete"), { chat.delete(id) }, { deleting = false }) }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Welcome() {
    val hour = java.util.Calendar.getInstance().get(java.util.Calendar.HOUR_OF_DAY)
    val name = app.prefs.name.trim()
    val greet = when (hour) { in 5..11 -> T("Good morning"); in 12..17 -> T("Good afternoon"); else -> T("Good evening") }
    val ideas = listOf(
        T("Explain a hard idea simply") to "Explain how vaccines train the immune system, simply.",
        T("Write and run code") to "Write a Python function that checks if a word is a palindrome, with tests.",
        T("Plan my week") to "Help me plan a balanced week: work, study, sport and rest.",
        T("Remember something") to "Remind me tomorrow at 9 to call the bank.",
        T("Summarise a web page") to "Summarise https://en.wikipedia.org/wiki/Sahara in 5 bullet points.",
        T("Practise a language") to "Let's practise French conversation. Correct my mistakes gently.",
    )
    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        OrbitLogo(84.dp)
        Spacer(Modifier.height(18.dp))
        Text(if (name.isNotEmpty()) "$greet, $name" else greet, style = MaterialTheme.typography.headlineMedium, color = c.text)
        Spacer(Modifier.height(6.dp))
        Text(T("Ask anything. All models think together as one brain."), style = MaterialTheme.typography.bodyMedium, color = c.muted, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        Spacer(Modifier.height(22.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ideas.forEach { (label, prompt) -> Chip(label, onClick = { app.chat.send(tr(prompt)) }) }
        }
        Spacer(Modifier.height(18.dp))
        Row(Modifier.clip(RoundedCornerShape(50)).background(c.accentSoft).clickable { app.ui.go("live") }.padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            PulseDot(c.accent)
            Spacer(Modifier.width(10.dp))
            Text(T("Talk live with A.S.T.R.O AI"), color = c.text, style = MaterialTheme.typography.labelLarge)
        }
    }
}

@OptIn(ExperimentalFoundationApi::class, ExperimentalLayoutApi::class)
@Composable
private fun UserMessage(m: Msg, idx: Int) {
    var editing by remember { mutableStateOf(false) }
    var menu by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.End) {
        m.image?.let { url -> DataImage(url, Modifier.widthIn(max = 220.dp).heightIn(max = 220.dp).clip(RoundedCornerShape(14.dp))); Spacer(Modifier.height(6.dp)) }
        if (m.files.isNotEmpty()) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) { m.files.forEach { Badge("📎 $it", c.muted) } }
        Box {
            Text(m.content, Modifier.widthIn(max = 320.dp).clip(RoundedCornerShape(18.dp, 18.dp, 4.dp, 18.dp)).background(c.userBubble)
                .combinedClickable(onClick = {}, onLongClick = { menu = true }).padding(horizontal = 14.dp, vertical = 10.dp),
                style = MaterialTheme.typography.bodyLarge, color = c.text)
            DropdownMenu(menu, { menu = false }, containerColor = c.surface) {
                DropdownMenuItem({ Text(T("Copy")) }, { menu = false; app.ui.copy(m.content); app.ui.toast(tr("Copied")) }, leadingIcon = { Ic(AppIcons.copy) })
                DropdownMenuItem({ Text(T("Edit and resend")) }, { menu = false; editing = true }, enabled = !app.chat.loading, leadingIcon = { Ic(AppIcons.edit) })
            }
        }
    }
    if (editing) PromptDialog(T("Edit message"), m.content, "", T("Send"), onConfirm = { app.chat.editAndResend(idx, it) }, onDismiss = { editing = false })
}

@Composable
fun DataImage(url: String, modifier: Modifier = Modifier) {
    val bmp = remember(url) {
        try { val b64 = url.substringAfter("base64,", ""); val bytes = Base64.decode(b64, Base64.DEFAULT); BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap() } catch (_: Exception) { null }
    }
    if (bmp != null) Image(bmp, null, modifier, contentScale = ContentScale.Crop)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AssistantMessage(m: Msg, idx: Int) {
    val d = m.current
    Column(Modifier.fillMaxWidth().animateContentSize()) {
        if (m.compare && m.drafts.size > 1) {
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                m.drafts.forEachIndexed { i, x -> Chip(modelLabel(x.model.ifEmpty { x.requested }), selected = i == m.active, onClick = { m.active = i; app.chat.persist() }) }
            }
            Spacer(Modifier.height(8.dp))
        }
        if (d == null) { ThinkingDots(); return@Column }
        d.brain?.let { b -> if (d.status == "streaming" || b.phase != "done") BrainPanel(d, b) }
        if (d.notice.isNotEmpty()) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(bottom = 6.dp)) {
                Ic(AppIcons.spark, null, tint = c.accent, size = 14.dp); Spacer(Modifier.width(6.dp))
                Text(d.notice, style = MaterialTheme.typography.bodySmall, color = c.muted)
            }
        }
        val early = d.early
        when {
            d.content.isNotEmpty() -> SelectionContainer { Markdown(d.content, streaming = d.status == "streaming", onOpenCode = { lang, code -> openInWorkspace(lang, code) }) }
            early != null && d.status == "streaming" -> Column {
                Text(T("First answer (from #) while the brain merges", early.first), style = MaterialTheme.typography.labelSmall, color = c.faint)
                Spacer(Modifier.height(4.dp))
                Markdown(early.second, Modifier.background(c.surface.copy(alpha = 0.4f)))
            }
            d.status == "streaming" -> if (d.brain == null) ThinkingDots()
        }
        if (d.status == "error") {
            Row(Modifier.fillMaxWidth().padding(top = 6.dp).clip(RoundedCornerShape(12.dp)).background(c.danger.copy(alpha = 0.1f)).padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Ic(AppIcons.alert, null, tint = c.danger, size = 18.dp); Spacer(Modifier.width(10.dp))
                Text(d.error.ifEmpty { T("Something went wrong") }, Modifier.weight(1f), color = c.text, style = MaterialTheme.typography.bodyMedium)
                Text(T("Retry"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { app.chat.regenerate(idx) }.padding(8.dp), color = c.accent, fontWeight = FontWeight.SemiBold)
            }
        }
        if (d.actions.isNotEmpty()) { Spacer(Modifier.height(8.dp)); d.actions.forEach { ActionCardView(it) } }
        if (m.sources.isNotEmpty() && d.status != "streaming") {
            Spacer(Modifier.height(8.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                m.sources.forEachIndexed { i, u -> Chip("${i + 1} · ${io.github.esoexe.astroai.brain.Text.hostOf(u)}", icon = AppIcons.globe, onClick = { app.ui.readerUrl = u; app.ui.go("reader") }) }
            }
        }
        if (d.status != "streaming") MessageActions(m, d, idx)
    }
}

@Composable
private fun BrainPanel(d: Draft, b: BrainInfo) {
    var ticker by remember { mutableLongStateOf(now()) }
    LaunchedEffect(d.status) { while (d.status == "streaming") { ticker = now(); delay(500) } }
    val phase = when (b.phase) {
        "plan" -> T("Planning the team…")
        "minds" -> if (b.patient) T("Every model is answering (waiting for all)…") else T("Models are thinking together…")
        "merging" -> if (b.round > 1) T("Merging answers (round #)…", b.round) else T("Merging answers…")
        "review" -> T("Reviewing the merged answer…")
        "final" -> T("Writing the final answer…")
        else -> T("Done")
    }
    val done = b.minds.count { it.status in setOf("done", "partial") }
    Column(Modifier.fillMaxWidth().padding(bottom = 10.dp).clip(RoundedCornerShape(14.dp)).background(c.surface).border(1.dp, c.border, RoundedCornerShape(14.dp)).padding(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OrbitLogo(22.dp)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(phase, style = MaterialTheme.typography.labelLarge, color = c.text)
                Text(T("# of # minds ready · #", done, b.minds.size.coerceAtLeast(b.total), secs(ticker - b.started)), style = MaterialTheme.typography.labelSmall, color = c.muted)
            }
            if (b.phase == "minds" && done >= 1 && d.status == "streaming") {
                Text(T("Merge now"), Modifier.clip(RoundedCornerShape(8.dp)).background(c.accentSoft).clickable { app.chat.mergeNow() }.padding(horizontal = 10.dp, vertical = 6.dp),
                    color = c.accent, style = MaterialTheme.typography.labelMedium)
            }
        }
        if (b.minds.isNotEmpty()) {
            Spacer(Modifier.height(10.dp))
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                b.minds.forEach { mind ->
                    val col = when (mind.status) { "done" -> c.ok; "partial", "cut" -> c.warn; "failed" -> c.danger; "resting" -> c.faint; else -> c.accent }
                    Row(Modifier.clip(RoundedCornerShape(50)).background(c.surface2).padding(horizontal = 10.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                        if (mind.status == "thinking" || mind.status == "writing") PulseDot(col, 7.dp) else Box(Modifier.size(7.dp).clip(CircleShape).background(col))
                        Spacer(Modifier.width(6.dp))
                        Text(mind.name, style = MaterialTheme.typography.labelSmall, color = c.text, maxLines = 1)
                    }
                }
            }
        }
        b.merges?.let { ms ->
            Spacer(Modifier.height(8.dp))
            Text(ms.joinToString(" · ") { "${it.name} ${if (it.status == "done") "✓" else "…"}" }, style = MaterialTheme.typography.labelSmall, color = c.muted, maxLines = 2, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
fun ActionCardView(a: ActionCard) {
    val icon = when {
        a.tool.contains("note") -> AppIcons.note
        a.tool.contains("task") -> AppIcons.task
        a.tool.contains("remind") -> AppIcons.bell
        a.tool == "remember" || a.tool == "forget" -> AppIcons.memory
        a.tool.contains("web") || a.tool.contains("url") -> AppIcons.globe
        a.tool.contains("file") -> AppIcons.file
        else -> AppIcons.bolt
    }
    val col = when (a.status) { "failed" -> c.danger; "pending" -> c.accent; "undone", "cancelled" -> c.faint; else -> c.ok }
    Row(Modifier.fillMaxWidth().padding(bottom = 6.dp).clip(RoundedCornerShape(12.dp)).background(c.surface).border(1.dp, c.border, RoundedCornerShape(12.dp))
        .clickable(enabled = a.url.isNotEmpty()) { app.ui.openUrl(a.url) }.padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Ic(icon, null, tint = col, size = 18.dp)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(a.shownText, style = MaterialTheme.typography.bodyMedium, color = if (a.status == "undone" || a.status == "cancelled") c.faint else c.text,
                textDecoration = if (a.status == "undone") androidx.compose.ui.text.style.TextDecoration.LineThrough else null)
            if (a.sub.isNotEmpty()) Text(a.sub, style = MaterialTheme.typography.bodySmall, color = c.muted, maxLines = 3, overflow = TextOverflow.Ellipsis)
        }
        when (a.status) {
            "pending" -> {
                Text(T("Cancel"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { a.status = "cancelled" }.padding(8.dp), color = c.muted, style = MaterialTheme.typography.labelMedium)
                Text(T(a.confirmLabel), Modifier.clip(RoundedCornerShape(8.dp)).background(if (a.danger) c.danger.copy(alpha = 0.15f) else c.accentSoft).clickable {
                    try { a.shownText = a.run?.invoke() ?: a.text; a.status = "done" } catch (e: Exception) { a.shownText = e.message ?: a.text; a.status = "failed" }
                    app.chat.persist()
                }.padding(horizontal = 10.dp, vertical = 6.dp), color = if (a.danger) c.danger else c.accent, style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold))
            }
            "done" -> if (a.undo != null) Text(T("Undo"), Modifier.clip(RoundedCornerShape(8.dp)).clickable {
                try { a.undo.invoke(); a.status = "undone" } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
            }.padding(8.dp), color = c.accent, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
private fun MessageActions(m: Msg, d: Draft, idx: Int) {
    var rewrite by remember { mutableStateOf(false) }
    var regen by remember { mutableStateOf(false) }
    var bad by remember { mutableStateOf(false) }
    val speech = app.speech
    val speakingThis = speech.speaking && speech.speakingId == d.id
    Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        if (m.drafts.size > 1 && !m.compare) {
            IconBtn(AppIcons.left, T("Previous version"), { m.active = (m.active - 1).coerceAtLeast(0); app.chat.persist() }, enabled = m.active > 0, size = 16.dp)
            Text("${m.active + 1}/${m.drafts.size}", style = MaterialTheme.typography.labelSmall, color = c.muted)
            IconBtn(AppIcons.right, T("Next version"), { m.active = (m.active + 1).coerceAtMost(m.drafts.lastIndex); app.chat.persist() }, enabled = m.active < m.drafts.lastIndex, size = 16.dp)
        }
        IconBtn(AppIcons.copy, T("Copy"), { app.ui.copy(d.content); app.ui.toast(tr("Copied")) }, size = 17.dp)
        IconBtn(if (speakingThis) AppIcons.stop else AppIcons.speaker, if (speakingThis) T("Stop") else T("Read aloud"), { if (speakingThis) speech.stopSpeaking() else speech.speak(d.content, d.id) }, tint = if (speakingThis) c.accent else c.muted, size = 17.dp)
        Box {
            IconBtn(AppIcons.redo, T("Regenerate"), { regen = true }, enabled = !app.chat.loading, size = 17.dp)
            DropdownMenu(regen, { regen = false }, containerColor = c.surface) {
                DropdownMenuItem({ Text(T("Try again")) }, { regen = false; app.chat.regenerate(idx) })
                DropdownMenuItem({ Text(T("With One Brain")) }, { regen = false; app.chat.regenerate(idx, "brain") })
                DropdownMenuItem({ Text(T("With the fastest model")) }, { regen = false; app.chat.regenerate(idx, "auto") })
                HorizontalDivider(color = c.border)
                app.chat.REWRITES.forEach { (k, label, _) -> DropdownMenuItem({ Text(T(label)) }, { regen = false; app.chat.rewrite(idx, k) }) }
            }
        }
        IconBtn(AppIcons.up, T("Good answer"), { m.feedback = if (m.feedback == "up") null else "up"; app.chat.persist() }, tint = if (m.feedback == "up") c.ok else c.muted, size = 17.dp)
        IconBtn(AppIcons.down, T("Bad answer"), { if (m.feedback == "down") { m.feedback = null; app.chat.persist() } else bad = true }, tint = if (m.feedback == "down") c.danger else c.muted, size = 17.dp)
        Spacer(Modifier.weight(1f))
        val info = buildString {
            if (d.model.isNotEmpty()) append(modelLabel(d.model))
            if (d.ms > 0) append(" · ").append(secs(d.ms))
            if (d.confidence.isNotEmpty()) append(" · ").append(d.confidence)
        }
        if (info.isNotEmpty()) Text(info, style = MaterialTheme.typography.labelSmall, color = c.faint, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = 150.dp))
    }
    if (bad) BadAnswerDialog(m, d, idx) { bad = false }
    @Suppress("UNUSED_VARIABLE") val r = rewrite
}

@Composable
private fun BadAnswerDialog(m: Msg, d: Draft, idx: Int, onDismiss: () -> Unit) {
    var note by remember { mutableStateOf("") }
    var share by remember { mutableStateOf(false) }
    androidx.compose.material3.AlertDialog(onDismissRequest = onDismiss, containerColor = c.surface,
        title = { Text(T("What was wrong?")) },
        text = {
            Column {
                io.github.esoexe.astroai.ui.Field(note, { note = it }, T("Optional note"), singleLine = false, minLines = 2)
                io.github.esoexe.astroai.ui.ToggleRow(T("Share this answer to improve A.S.T.R.O AI"), T("Sends the question and answer, without your name."), share, { share = it })
            }
        },
        confirmButton = {
            androidx.compose.material3.TextButton({
                m.feedback = "down"; app.chat.persist(); onDismiss()
                val question = app.chat.messages.getOrNull(idx - 1)?.content ?: ""
                app.chat.scope.launch {
                    try {
                        Api.post("/api/feedback", jsonOf("kind" to "bad_answer", "question" to question.take(4000), "answer" to d.content.take(8000), "model" to d.model,
                            "mode" to app.prefs.mode, "note" to note.take(1000), "share" to share), 20_000)
                        app.ui.toast(tr("Thanks, noted."), action = tr("Try again")) { app.chat.regenerate(idx, "brain") }
                    } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
                }
            }) { Text(T("Send"), color = c.accent, fontWeight = FontWeight.SemiBold) }
        },
        dismissButton = { androidx.compose.material3.TextButton(onDismiss) { Text(T("Cancel"), color = c.muted) } })
}

fun openInWorkspace(lang: String, code: String) {
    val ext = mapOf("python" to "py", "py" to "py", "javascript" to "js", "js" to "js", "typescript" to "ts", "ts" to "ts", "html" to "html", "css" to "css", "c" to "c", "cpp" to "cpp",
        "c++" to "cpp", "java" to "java", "kotlin" to "kt", "go" to "go", "rust" to "rs", "bash" to "sh", "sh" to "sh", "json" to "json", "sql" to "sql", "php" to "php", "ruby" to "rb")[lang.lowercase()] ?: "txt"
    app.chat.scope.launch {
        try {
            val project = Ws.ensureProject()
            val name = "snippet-${System.currentTimeMillis() % 100000}.$ext"
            Api.post("/api/fs/write", jsonOf("project" to project, "path" to name, "content" to code.trimEnd() + "\n"))
            Ws.openFile = name
            app.ui.go("workspace")
            app.ui.toast(tr("Saved as # in #", name, project))
        } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
    }
}

// ---------------- composer ----------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Composer() {
    val chat = app.chat
    val prefs = app.prefs
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberCoroutineScope()
    var text by remember { mutableStateOf("") }
    var attach by remember { mutableStateOf(false) }
    var modeMenu by remember { mutableStateOf(false) }
    var dictating by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf("") }

    LaunchedEffect(app.ui.incomingText) { app.ui.incomingText?.let { text = if (text.isBlank()) it else "$text\n$it"; app.ui.incomingText = null } }

    val pickImage = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch {
            busy = tr("Preparing the photo…")
            try { chat.pendingImage = withContext(Dispatchers.IO) { loadImage(uri) } } catch (e: Exception) { app.ui.toast(e.message ?: tr("Could not open the photo"), "error") }
            busy = ""
        }
    }
    val pickDoc = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) scope.launch {
            for (u in uris.take(5)) {
                busy = tr("Reading #…", nameOf(u))
                try { chat.documents.add(withContext(Dispatchers.IO) { readDocument(u) }) } catch (e: Exception) { app.ui.toast(e.message ?: tr("Could not read the file"), "error") }
            }
            busy = ""
        }
    }

    fun send() {
        val t = text.trim()
        if (t.isEmpty() || chat.loading) return
        text = ""
        chat.send(t)
    }

    Column(Modifier.fillMaxWidth().navigationBarsPadding().padding(horizontal = 10.dp, vertical = 8.dp)) {
        // attachments
        val img = chat.pendingImage
        if (img != null || chat.documents.isNotEmpty() || busy.isNotEmpty()) {
            FlowRow(Modifier.padding(bottom = 6.dp, start = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (img != null) Row(Modifier.clip(RoundedCornerShape(10.dp)).background(c.surface2).padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                    DataImage(img.second, Modifier.size(36.dp).clip(RoundedCornerShape(8.dp)))
                    IconBtn(AppIcons.x, T("Remove"), { chat.pendingImage = null }, size = 14.dp)
                }
                chat.documents.forEach { doc ->
                    Row(Modifier.clip(RoundedCornerShape(10.dp)).background(c.surface2).padding(start = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                        Ic(AppIcons.file, null, tint = c.accent, size = 14.dp); Spacer(Modifier.width(6.dp))
                        Text(doc.name, style = MaterialTheme.typography.labelMedium, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = 150.dp))
                        IconBtn(AppIcons.x, T("Remove"), { chat.documents.remove(doc) }, size = 14.dp)
                    }
                }
                if (busy.isNotEmpty()) Row(verticalAlignment = Alignment.CenterVertically) { ThinkingDots(); Spacer(Modifier.width(8.dp)); Text(busy, style = MaterialTheme.typography.labelSmall, color = c.muted) }
            }
        }
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp)).background(c.surface).border(1.dp, c.border, RoundedCornerShape(22.dp)).padding(horizontal = 6.dp, vertical = 4.dp)) {
            Box(Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 8.dp)) {
                if (text.isEmpty()) Text(if (dictating) T("Listening…") else T("Message A.S.T.R.O AI"), color = c.faint, style = MaterialTheme.typography.bodyLarge)
                BasicTextField(text, { text = it }, Modifier.fillMaxWidth().heightIn(min = 24.dp, max = 180.dp), textStyle = MaterialTheme.typography.bodyLarge.copy(color = c.text),
                    cursorBrush = SolidColor(c.accent),
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = if (prefs.enterSends) androidx.compose.ui.text.input.ImeAction.Send else androidx.compose.ui.text.input.ImeAction.Default,
                        capitalization = androidx.compose.ui.text.input.KeyboardCapitalization.Sentences),
                    keyboardActions = androidx.compose.foundation.text.KeyboardActions(onSend = { send() }))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box {
                    IconBtn(AppIcons.plus, T("Attach"), { attach = true }, tint = c.text)
                    DropdownMenu(attach, { attach = false }, containerColor = c.surface) {
                        DropdownMenuItem({ Text(T("Photo")) }, { attach = false; pickImage.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }, leadingIcon = { Ic(AppIcons.image) })
                        DropdownMenuItem({ Text(T("Document (PDF, Word, text)")) }, { attach = false; pickDoc.launch(arrayOf("*/*")) }, leadingIcon = { Ic(AppIcons.file) })
                        DropdownMenuItem({ Text(T("Knowledge files")) }, { attach = false; app.ui.go("knowledge") }, leadingIcon = { Ic(AppIcons.book) })
                    }
                }
                Box {
                    Chip(T(MODES.firstOrNull { it.first == prefs.mode }?.second ?: "General"), onClick = { modeMenu = true })
                    DropdownMenu(modeMenu, { modeMenu = false }, containerColor = c.surface) {
                        MODES.forEach { (k, label) -> DropdownMenuItem({ Text(T(label)) }, { modeMenu = false; prefs.mode = k }, trailingIcon = { if (prefs.mode == k) Ic(AppIcons.check, tint = c.accent) }) }
                    }
                }
                Spacer(Modifier.width(6.dp))
                Chip(T("Web"), selected = prefs.webSearch, icon = AppIcons.globe, onClick = { prefs.webSearch = !prefs.webSearch })
                Spacer(Modifier.weight(1f))
                if (app.speech.recognitionAvailable && text.isEmpty() && !chat.loading) {
                    IconBtn(if (dictating) AppIcons.micOff else AppIcons.mic, if (dictating) T("Stop dictation") else T("Dictate"), {
                        if (dictating) { app.speech.stopListening(); dictating = false }
                        else if (MicPermission.ensure(ctx)) {
                            dictating = true
                            app.speech.listen(null, onPartial = { text = it }, onFinal = { text = it; dictating = false }, onError = { dictating = false; if (it.isNotEmpty()) app.ui.toast(it, "error") })
                        }
                    }, tint = if (dictating) c.accent else c.muted)
                }
                when {
                    chat.loading -> SendButton(AppIcons.stop, T("Stop"), true) { chat.stop() }
                    text.isNotBlank() -> SendButton(AppIcons.send, T("Send"), true) { send() }
                    else -> SendButton(AppIcons.wave, T("Live voice"), true, live = true) { app.ui.go("live") }
                }
            }
        }
    }
}

@Composable
private fun SendButton(icon: androidx.compose.ui.graphics.vector.ImageVector, desc: String, enabled: Boolean, live: Boolean = false, onClick: () -> Unit) {
    Box(Modifier.padding(4.dp).size(40.dp).clip(CircleShape)
        .background(if (live) androidx.compose.ui.graphics.Brush.linearGradient(listOf(androidx.compose.ui.graphics.Color(0xFFF7C59F), c.accent, androidx.compose.ui.graphics.Color(0xFFE07A4F))) else SolidColor(c.accent))
        .clickable(enabled = enabled, onClick = onClick), contentAlignment = Alignment.Center) {
        Ic(icon, desc, tint = c.onAccent, size = 19.dp)
    }
}

fun nameOf(uri: Uri): String = try {
    app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { if (it.moveToFirst()) it.getString(0) else null }
} catch (_: Exception) { null } ?: uri.lastPathSegment?.substringAfterLast('/') ?: "file"

fun loadImage(uri: Uri): Pair<String, String> {
    val cr = app.contentResolver
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
    var sample = 1
    while (maxOf(bounds.outWidth, bounds.outHeight) / sample > 2560) sample *= 2
    val bmp = cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample }) } ?: throw IllegalStateException(tr("Could not open the photo"))
    val scale = minOf(1f, 1280f / maxOf(bmp.width, bmp.height))
    val out = if (scale < 1f) Bitmap.createScaledBitmap(bmp, (bmp.width * scale).toInt(), (bmp.height * scale).toInt(), true) else bmp
    val bytes = ByteArrayOutputStream().also { out.compress(Bitmap.CompressFormat.JPEG, 85, it) }.toByteArray()
    return nameOf(uri) to "data:image/jpeg;base64," + Base64.encodeToString(bytes, Base64.NO_WRAP)
}

private val TEXT_EXT = setOf("txt", "md", "markdown", "csv", "tsv", "json", "xml", "html", "htm", "css", "js", "ts", "py", "java", "kt", "c", "cpp", "h", "hpp", "cs", "go", "rs",
    "rb", "php", "sh", "yaml", "yml", "toml", "ini", "log", "sql", "srt", "tex", "swift", "dart", "lua", "r")

/** Reads a document into text: plain text here, PDF/Word/etc. through the engine's /api/extract. */
fun readDocument(uri: Uri): Doc {
    val name = nameOf(uri)
    val bytes = app.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: throw IllegalStateException(tr("Could not read the file"))
    if (bytes.size > 20 * 1024 * 1024) throw IllegalStateException(tr("# is too big (max 20 MB)", name))
    val ext = name.substringAfterLast('.', "").lowercase()
    val text = if (ext in TEXT_EXT) bytes.toString(Charsets.UTF_8) else {
        val j = Api.requestBlocking("POST", "/api/extract", jsonOf("filename" to name, "data_base64" to Base64.encodeToString(bytes, Base64.NO_WRAP)), 120_000)
        j.str("text")
    }
    if (text.isBlank()) throw IllegalStateException(tr("No text found in #", name))
    return Doc(name, text.take(400_000))
}

// ---------------- model picker ----------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ModelPicker(onDismiss: () -> Unit) {
    val prefs = app.prefs
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val models = app.backend.models
    var compareMode by remember { mutableStateOf(false) }
    val picked = remember { mutableStateListOf<String>() }
    var compareText by remember { mutableStateOf("") }
    ModalBottomSheet(onDismiss, sheetState = state, containerColor = c.surface) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp).padding(bottom = 24.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(if (compareMode) T("Compare models") else T("Choose who answers"), style = MaterialTheme.typography.titleLarge, color = c.text, modifier = Modifier.weight(1f))
                Chip(T("Compare"), selected = compareMode, icon = AppIcons.compare, onClick = { compareMode = !compareMode })
            }
            Spacer(Modifier.height(10.dp))
            if (compareMode) {
                Text(T("Pick up to 4 models; each answers the same question side by side."), style = MaterialTheme.typography.bodySmall, color = c.muted)
                io.github.esoexe.astroai.ui.Field(compareText, { compareText = it }, T("Question"), singleLine = false, minLines = 2)
                Spacer(Modifier.height(8.dp))
            } else {
                PickRow(T("One Brain"), OneBrain.label(app), prefs.model == "brain", AppIcons.brain) { prefs.model = "brain"; onDismiss() }
                PickRow(T("Auto"), T("The fastest available model, with failover"), prefs.model == "auto", AppIcons.bolt) { prefs.model = "auto"; onDismiss() }
            }
            Text(T("Models (#)", models.size).uppercase(), Modifier.padding(top = 12.dp, bottom = 4.dp), style = MaterialTheme.typography.labelSmall, color = c.faint)
            LazyColumn(Modifier.heightIn(max = 380.dp)) {
                itemsIndexed(models, key = { _, m -> m.str("id") }) { _, m ->
                    val id = m.str("id")
                    val blocked = when (val v = m.opt("blocked")) { is String -> v; true -> tr("unavailable"); else -> "" }
                    val tags = buildList {
                        add(m.str("provider_name").ifEmpty { m.str("provider") })
                        if (m.bool("local")) add(tr("on device"))
                        if (m.bool("paid")) add(tr("paid"))
                        if (m.bool("vision")) add(tr("images"))
                        if (blocked.isNotEmpty()) add(blocked)
                    }.joinToString(" · ")
                    val sel = if (compareMode) id in picked else prefs.model == id
                    PickRow(m.str("name").ifEmpty { m.str("label") }, tags, sel, null, enabled = blocked.isEmpty() || compareMode) {
                        if (compareMode) { if (id in picked) picked.remove(id) else if (picked.size < 4) picked.add(id) }
                        else { prefs.model = id; onDismiss() }
                    }
                }
            }
            if (compareMode) {
                Spacer(Modifier.height(10.dp))
                io.github.esoexe.astroai.ui.Primary(T("Compare # models", picked.size), { app.chat.compare(compareText, picked.toList()); onDismiss() }, Modifier.fillMaxWidth(),
                    enabled = picked.size >= 2 && compareText.isNotBlank() && !app.chat.loading)
            }
        }
    }
}

@Composable
private fun PickRow(title: String, sub: String, selected: Boolean, icon: androidx.compose.ui.graphics.vector.ImageVector?, enabled: Boolean = true, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(if (selected) c.accentSoft else c.surface).clickable(enabled = enabled, onClick = onClick)
        .padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) { Ic(icon, null, tint = c.accent); Spacer(Modifier.width(12.dp)) }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, color = if (enabled) c.text else c.faint, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (sub.isNotEmpty()) Text(sub, style = MaterialTheme.typography.bodySmall, color = c.muted, maxLines = 2, overflow = TextOverflow.Ellipsis)
        }
        if (selected) Ic(AppIcons.check, null, tint = c.accent, size = 18.dp)
    }
}

@Suppress("unused") private fun unusedJson(o: JSONObject) = o
