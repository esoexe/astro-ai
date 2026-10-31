package io.github.esoexe.astroai.ui.screens

import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.BuildConfig
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.int
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.objects
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Badge
import io.github.esoexe.astroai.ui.Card
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.ConfirmDialog
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.Mono
import io.github.esoexe.astroai.ui.OrbitLogo
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.ToggleRow
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

const val SITE = "https://esoexe.github.io/astro-ai"

private val TABS = listOf("general" to "General", "chat" to "Chat", "brain" to "One Brain", "voice" to "Voice", "providers" to "Providers", "connection" to "Connection", "data" to "Data", "about" to "About")

@Composable
fun SettingsScreen(onMenu: () -> Unit) {
    val ui = app.ui
    Screen(T("Settings"), onMenu) {
        Column(Modifier.fillMaxSize()) {
            // keeps the selected tab visible, also when a tab is opened from elsewhere (settings:about, ...)
            val tabRow = rememberLazyListState()
            LaunchedEffect(ui.settingsTab) {
                val i = TABS.indexOfFirst { it.first == ui.settingsTab }
                if (i >= 0) tabRow.animateScrollToItem((i - 1).coerceAtLeast(0))
            }
            LazyRow(Modifier.fillMaxWidth(), state = tabRow, contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                items(TABS, key = { it.first }) { (k, l) -> Chip(T(l), selected = ui.settingsTab == k, onClick = { ui.settingsTab = k }) }
            }
            LazyColumn(Modifier.weight(1f).imePadding(), contentPadding = PaddingValues(16.dp)) {
                item {
                    when (ui.settingsTab) {
                        "chat" -> ChatTab()
                        "brain" -> BrainTab()
                        "voice" -> VoiceTab()
                        "providers" -> ProvidersTab()
                        "connection" -> ConnectionTab()
                        "data" -> DataTab()
                        "about" -> AboutTab()
                        else -> GeneralTab()
                    }
                }
            }
        }
    }
}

@Composable
private fun Choice(title: String, options: List<Pair<String, String>>, value: String, onPick: (String) -> Unit) {
    Text(title, style = MaterialTheme.typography.bodyLarge, color = c.text, modifier = Modifier.padding(top = 10.dp, bottom = 6.dp))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        options.forEach { (k, l) -> Chip(l, selected = value == k, onClick = { onPick(k) }) }
    }
}

@Composable
private fun GeneralTab() {
    val p = app.prefs
    var name by remember { mutableStateOf(p.name) }
    SectionTitle(T("You"))
    Field(name, { name = it; p.name = it }, T("Your name (used in greetings and answers)"))
    SectionTitle(T("Language"))
    Choice(T("Interface"), listOf("" to T("Phone setting")) + I18n.LANGS.map { it.key to it.value }, p.uiLang) { p.uiLang = it; app.applyUiLanguage() }
    Choice(T("Answers"), listOf("auto" to T("Same as my message"), "English" to "English", "French" to "Français", "Arabic" to "العربية"), p.language) { p.language = it }   // i18n-ignore: native language names
    SectionTitle(T("Appearance"))
    Choice(T("Theme"), listOf("system" to T("Phone setting"), "light" to T("Light"), "dark" to T("Dark")), p.theme) { p.theme = it }
    Choice(T("Text size"), listOf("compact" to T("Compact"), "normal" to T("Normal"), "large" to T("Large")), p.density) { p.density = it }
    ToggleRow(T("Animations"), T("The live logo, brain panel and transitions"), p.motion, { p.motion = it })
}

@Composable
private fun ChatTab() {
    val p = app.prefs
    SectionTitle(T("Answers"))
    Choice(T("Default style"), MODES.map { it.first to T(it.second) }, p.mode) { p.mode = it }
    ToggleRow(T("Actions in answers"), T("Notes, tasks, reminders, memory and settings can be changed from chat, with undo"), p.agentTools, { p.agentTools = it })
    ToggleRow(T("Search the web"), T("Look things up before answering (also the Web chip in the composer)"), p.webSearch, { p.webSearch = it })
    ToggleRow(T("Check code after the answer"), T("Runs code from answers in a sandbox and warns if it fails; never slows the answer"), p.codeCheck, { p.codeCheck = it })
    ToggleRow(T("Read answers aloud"), "", p.autoSpeak, { p.autoSpeak = it })
    ToggleRow(T("Enter key sends"), T("Otherwise Enter adds a new line"), p.enterSends, { p.enterSends = it })
}

@Composable
private fun BrainTab() {
    val p = app.prefs
    val total = app.backend.models.count { !it.bool("paid") }
    Text(T("One Brain: every free model thinks about your question, then their answers are merged and reviewed into one. It is slower than one model, but more careful."),
        style = MaterialTheme.typography.bodyMedium, color = c.muted)
    SectionTitle(T("Minds"))
    Choice(T("How many models think together"), listOf("0" to T("All (#)", total), "3" to "3", "5" to "5", "8" to "8", "12" to "12"), p.brainMinds.toString()) { p.brainMinds = it.toInt() }
    ToggleRow(T("Wait for every model"), T("In All mode, slow models finish too before the merge (up to 20 minutes)"), p.brainWaitAll, { p.brainWaitAll = it }, enabled = p.brainMinds == 0)
    ToggleRow(T("Show the first answer while waiting"), T("You can read the fastest draft while the brain merges"), p.brainFastFirst, { p.brainFastFirst = it })
    Text(T("Extra wait for slower minds: # s", p.brainGrace), style = MaterialTheme.typography.bodyLarge, color = c.text, modifier = Modifier.padding(top = 10.dp))
    Slider(p.brainGrace.toFloat(), { p.brainGrace = it.toInt() }, valueRange = 2f..20f, steps = 17, colors = SliderDefaults.colors(thumbColor = c.accent, activeTrackColor = c.accent, inactiveTrackColor = c.surface2, activeTickColor = c.onAccent.copy(alpha = 0.6f), inactiveTickColor = c.muted.copy(alpha = 0.5f)))
    SectionTitle(T("Privacy"))
    ToggleRow(T("Allow providers that may train on prompts"), T("Some free providers use prompts to improve their models. Turn off to skip them."), p.brainAllowTrain, { p.brainAllowTrain = it })
    ToggleRow(T("Only models on this device or computer"), T("Never send prompts to online providers"), p.brainLocalOnly, { p.brainLocalOnly = it })
}

@Composable
private fun VoiceTab() {
    val p = app.prefs
    val voices = remember { app.speech.voices() }
    SectionTitle(T("Speaking"))
    Text(T("Speed: #", "%.1f×".format(p.voiceRate)), style = MaterialTheme.typography.bodyLarge, color = c.text)
    Slider(p.voiceRate, { p.voiceRate = it }, valueRange = 0.6f..1.6f, colors = SliderDefaults.colors(thumbColor = c.accent, activeTrackColor = c.accent, inactiveTrackColor = c.surface2, activeTickColor = c.onAccent.copy(alpha = 0.6f), inactiveTickColor = c.muted.copy(alpha = 0.5f)))
    Text(T("Pitch: #", "%.1f".format(p.voicePitch)), style = MaterialTheme.typography.bodyLarge, color = c.text)
    Slider(p.voicePitch, { p.voicePitch = it }, valueRange = 0.7f..1.4f, colors = SliderDefaults.colors(thumbColor = c.accent, activeTrackColor = c.accent, inactiveTrackColor = c.surface2, activeTickColor = c.onAccent.copy(alpha = 0.6f), inactiveTickColor = c.muted.copy(alpha = 0.5f)))
    Secondary(T("Test the voice"), { app.speech.speak(tr("Hello, I am Astro A.I. All models think together as one brain.")) }, icon = AppIcons.speaker)
    SectionTitle(T("Voice"))
    Text(T("Voices come from the phone's text-to-speech engine. Install more in Android settings."), style = MaterialTheme.typography.bodySmall, color = c.muted)
    Spacer(Modifier.height(6.dp))
    VoiceRow(T("Automatic (matches the answer's language)"), p.voiceName.isEmpty()) { p.voiceName = "" }
    voices.filter { it.locale.language in setOf("en", "fr", "ar") }.take(60).forEach { v ->
        VoiceRow("${v.locale.displayName} · ${v.name.takeLast(28)}", p.voiceName == v.name) { p.voiceName = v.name; app.speech.speak(tr("This is how I sound.")) }
    }
    SectionTitle(T("Listening"))
    Text(if (app.speech.recognitionAvailable) T("Speech recognition is available.") else T("No speech recognition service on this phone. Install Google or another voice input app."),
        style = MaterialTheme.typography.bodySmall, color = c.muted)
}

@Composable
private fun VoiceRow(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(if (selected) c.accentSoft else c.bg).clickable(onClick = onClick).padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (selected) Ic(AppIcons.check, null, tint = c.accent, size = 16.dp)
    }
}

private val GUIDE = listOf("groq" to "Very fast answers", "openrouter" to "Many free models in one key", "mistral" to "Strong code models (Codestral)", "cohere" to "Good in Arabic and French")

@Composable
private fun ProvidersTab() {
    var providers by remember { mutableStateOf<List<JSONObject>?>(null) }
    var error by remember { mutableStateOf("") }
    val tests = remember { mutableStateMapOf<String, String>() }
    LaunchedEffect(Unit) {
        try { providers = Api.get("/api/keys").optJSONArray("providers")?.objects().orEmpty() } catch (e: Exception) { error = e.message ?: "" }
    }
    Text(T("Keys stay in the engine (on this phone, or on your computer in computer mode) and are never shown again. Free tiers have daily limits; they are not unlimited."),
        style = MaterialTheme.typography.bodyMedium, color = c.muted)
    if (error.isNotEmpty()) Text(error, color = c.danger)
    val list = providers ?: run { ThinkingDots(); return }
    val connected = list.count { it.bool("connected") }
    Text(T("# of # providers connected · # models", connected, list.size, app.backend.models.size), style = MaterialTheme.typography.labelLarge, color = c.text, modifier = Modifier.padding(top = 10.dp))
    val missing = GUIDE.mapNotNull { (id, why) -> list.firstOrNull { it.str("id") == id && !it.bool("connected") }?.let { it to why } }
    if (missing.isNotEmpty()) {
        SectionTitle(T("Get more free models"))
        Text(T("Each key is free and takes about a minute. More keys = more minds in One Brain and fewer pauses."), style = MaterialTheme.typography.bodySmall, color = c.muted)
        missing.forEach { (pv, why) -> ProviderCard(pv, T(why), tests) }
    }
    SectionTitle(T("All providers"))
    list.sortedWith(compareByDescending<JSONObject> { it.bool("connected") }.thenBy { it.str("name") }).forEach { pv -> ProviderCard(pv, "", tests) }
}

@Composable
private fun ProviderCard(pv: JSONObject, why: String, tests: MutableMap<String, String>) {
    var open by remember { mutableStateOf(false) }
    var key by remember { mutableStateOf("") }
    val id = pv.str("id")
    Card(Modifier.fillMaxWidth().padding(vertical = 4.dp), onClick = { open = !open }) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(pv.str("name"), style = MaterialTheme.typography.bodyLarge, color = c.text)
                    Text(listOf(why, pv.str("limits")).filter { it.isNotEmpty() }.joinToString(" · "), style = MaterialTheme.typography.bodySmall, color = c.muted, maxLines = if (open) 6 else 1, overflow = TextOverflow.Ellipsis)
                }
                when {
                    pv.bool("connected") -> Badge(tr("connected"), c.ok)
                    pv.bool("keyless") -> Badge(tr("no key needed"), c.muted)
                    else -> Badge("+${pv.int("models_total").takeIf { it > 0 } ?: pv.int("models")}", c.accent)
                }
            }
            if (open) {
                if (pv.bool("paid")) Text(T("Paid provider: off in Team mode unless you turn it on."), style = MaterialTheme.typography.bodySmall, color = c.warn, modifier = Modifier.padding(top = 6.dp))
                if (pv.bool("trains")) Text(T("This provider may use prompts for training."), style = MaterialTheme.typography.bodySmall, color = c.warn)
                if (!pv.bool("keyless") && pv.str("key").isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Field(key, { key = it }, if (pv.bool("key_set")) T("Replace the key") else T("Paste the key"), password = true)
                    Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Primary(T("Save"), {
                            app.chat.scope.launch {
                                try { Api.post("/api/keys", jsonOf("name" to pv.str("key"), "value" to key.trim())); key = ""; app.ui.toast(tr("Saved. The engine restarts to load it…")); app.backend.restartEngine() }
                                catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
                            }
                        }, enabled = key.isNotBlank())
                        if (pv.str("signup").isNotEmpty()) Secondary(T("Get a key"), { app.ui.openUrl(pv.str("signup")) }, icon = AppIcons.ext)
                    }
                }
                Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    if (pv.bool("connected")) Secondary(T("Test"), {
                        tests[id] = tr("Testing…")
                        app.chat.scope.launch {
                            tests[id] = try { val j = Api.post("/api/keys/test", jsonOf("provider" to id), 60_000); if (j.bool("ok")) tr("Works (#)", j.str("model")) else j.str("error").ifEmpty { tr("Failed") } } catch (e: Exception) { e.message ?: "" }
                        }
                    })
                    if (pv.bool("key_set") && pv.str("key_source") != "env") Secondary(T("Remove key"), {
                        app.chat.scope.launch { try { Api.post("/api/keys", jsonOf("name" to pv.str("key"), "value" to "")); app.backend.restartEngine() } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") } }
                    }, danger = true)
                    tests[id]?.let { Text(it, style = MaterialTheme.typography.bodySmall, color = c.muted) }
                }
            }
        }
    }
}

@Composable
private fun ConnectionTab() {
    val b = app.backend
    val p = app.prefs
    var log by remember { mutableStateOf<String?>(null) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Ic(if (b.isPc) AppIcons.server else AppIcons.spark, null, tint = c.accent)
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(if (b.isPc) T("Connected to your computer") else T("Built-in engine on this phone"), style = MaterialTheme.typography.titleMedium, color = c.text)
                    Text(if (b.isPc) p.pcUrl else T("Engine # · # models", b.version.ifEmpty { "?" }, b.models.size), style = MaterialTheme.typography.bodySmall, color = c.muted)
                }
            }
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (b.isPc) Secondary(T("Use this phone instead"), { b.useBuiltIn() }) else Primary(T("Connect to my computer"), { app.ui.go("connect") }, icon = AppIcons.server)
                Secondary(T("Restart"), { b.restartEngine() }, icon = AppIcons.refresh)
            }
        }
    }
    Spacer(Modifier.height(10.dp))
    Text(T("Built-in: everything runs inside the app; models answer through their providers on the internet. Computer: the phone uses A.S.T.R.O AI running on your PC (same Wi-Fi or a private network), with its models, keys and projects."),
        style = MaterialTheme.typography.bodySmall, color = c.muted)
    if (!b.isPc) {
        SectionTitle(T("Engine log"))
        Secondary(if (log == null) T("Show the log") else T("Hide the log"), { log = if (log == null) app.engine.logTail(8000) else null })
        log?.let { SelectionContainer { Text(it.ifEmpty { "(empty)" }, Modifier.padding(top = 8.dp).fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.codeBg).padding(10.dp), style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.muted) } }
    }
}

@Composable
private fun DataTab() {
    val p = app.prefs
    val store = app.store
    var wiping by remember { mutableStateOf(false) }
    var syncMsg by remember { mutableStateOf("") }
    SectionTitle(T("On this phone"))
    Text(T("# chats · # notes · # tasks · # memories", store.conversations.size, store.notes.size, store.tasks.size, store.memory.size), style = MaterialTheme.typography.bodyMedium, color = c.text)
    Text(T("Saved in the app's private storage. Not in the cloud unless you turn on sync below."), style = MaterialTheme.typography.bodySmall, color = c.muted)
    Spacer(Modifier.height(8.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Secondary(T("Export all"), {
            val o = JSONObject(); listOf("chats", "notes", "tasks", "memory", "reminders").forEach { k -> o.put(k, store.snapshot(k)) }
            o.put("app", "A.S.T.R.O AI Android ${BuildConfig.VERSION_NAME}").put("exported", System.currentTimeMillis())
            app.ui.shareText("astro-ai-backup.json", o.toString(2))
        }, icon = AppIcons.export)
        Secondary(T("Delete everything"), { wiping = true }, danger = true, icon = AppIcons.trash)
    }
    if (app.backend.isPc) {
        SectionTitle(T("Sync with your computer"))
        ToggleRow(T("Keep chats, notes and tasks in sync"), T("Saves them on your computer's A.S.T.R.O AI so both stay the same"), p.syncOn, { p.syncOn = it })
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Secondary(T("Save now"), { app.chat.scope.launch { syncMsg = try { withContext(Dispatchers.IO) { store.pushBlocking(force = false) }?.let { tr(it) } ?: tr("Already up to date") } catch (e: Exception) { e.message ?: "" } } })
            Secondary(T("Load from computer"), {
                app.chat.scope.launch {
                    syncMsg = try { val r = withContext(Dispatchers.IO) { store.remoteBlocking() }; if (r == null) tr("Nothing saved on the computer yet") else { store.applyRemote(r.first, r.second); tr("Loaded") } } catch (e: Exception) { e.message ?: "" }
                }
            })
        }
        if (syncMsg.isNotEmpty()) Text(syncMsg, style = MaterialTheme.typography.bodySmall, color = c.muted, modifier = Modifier.padding(top = 6.dp))
    }
    SectionTitle(T("Reports"))
    ToggleRow(T("Send crash reports automatically"), T("Off by default. Only the error and app version, with personal paths and keys removed."), p.crashReports, { p.crashReports = it })
    Text(T("Send feedback or report a problem"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { app.ui.go("feedback") }.padding(vertical = 8.dp), color = c.accent, style = MaterialTheme.typography.labelLarge)
    if (wiping) ConfirmDialog(T("Delete everything?"), T("All chats, notes, tasks, memory and reminders on this phone are deleted. This cannot be undone."), T("Delete everything"), {
        app.chat.newChat()
        store.conversations.clear(); store.notes.clear(); store.tasks.clear(); store.memory.clear()
        store.reminders.forEach { app.reminders.cancel(it.id, it.text) }; store.reminders.clear()
        store.saveChats(); store.saveNotes(); store.saveTasks(); store.saveMemory(); store.saveReminders()
        app.ui.toast(tr("Everything was deleted"))
    }, { wiping = false })
}

@Composable
private fun AboutTab() {
    val u = app.updater
    var license by remember { mutableStateOf("") }
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
        OrbitLogo(72.dp)
        Spacer(Modifier.height(10.dp))
        Text("A.S.T.R.O AI", style = MaterialTheme.typography.headlineSmall, color = c.text)
        Text(T("All Systems Thinking, Reasoning as One"), style = MaterialTheme.typography.bodySmall, color = c.muted)
        Text(T("Android # · engine #", BuildConfig.VERSION_NAME, app.backend.version.ifEmpty { "?" }), style = MaterialTheme.typography.labelMedium, color = c.faint, modifier = Modifier.padding(top = 4.dp))
    }
    SectionTitle(T("Updates"))
    Text(T("Updates come from the official site. Android checks that each update is signed by the same key as this app."), style = MaterialTheme.typography.bodySmall, color = c.muted)
    ToggleRow(T("Check for updates automatically"), "", app.prefs.updateAuto, { app.prefs.updateAuto = it })
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Secondary(if (u.checking) T("Checking…") else T("Check now"), { app.chat.scope.launch { u.check(force = true) } }, enabled = !u.checking, icon = AppIcons.refresh)
        u.available?.let { info -> Primary(T("Install #", info.version), { app.chat.scope.launch { u.install(info) } }, enabled = u.progress < 0f) }
    }
    if (u.message.isNotEmpty()) Text(u.message, style = MaterialTheme.typography.bodySmall, color = c.muted, modifier = Modifier.padding(top = 6.dp))
    u.available?.highlights?.takeIf { it.isNotEmpty() }?.let { hl -> hl.take(6).forEach { Text("• $it", style = MaterialTheme.typography.bodySmall, color = c.text) } }
    SectionTitle(T("Support the project"))
    Text(T("A.S.T.R.O AI is free and open source. Free model tiers have limits; donations and the optional Pro plan help keep it running."), style = MaterialTheme.typography.bodySmall, color = c.muted)
    Row(Modifier.padding(top = 8.dp).horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Chip(T("Support"), icon = AppIcons.hand, onClick = { app.ui.openUrl("$SITE/support.html") })
        Chip(T("Pro"), icon = AppIcons.sparkles, onClick = { app.ui.openUrl("$SITE/pro.html") })
        Chip(T("Limits"), icon = AppIcons.graph, onClick = { app.ui.openUrl("$SITE/limits.html") })
        Chip(T("Beta"), icon = AppIcons.flask, onClick = { app.ui.openUrl("$SITE/beta.html") })
    }
    Spacer(Modifier.height(8.dp))
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f)) { Field(license, { license = it }, T("Pro license key"), password = true) }
        Spacer(Modifier.width(8.dp))
        Primary(T("Activate"), {
            app.chat.scope.launch {
                try { val j = Api.post("/api/license", jsonOf("key" to license.trim()), 30_000); license = ""; app.ui.toast(if (j.bool("active")) tr("Pro is active") else tr("License saved")) }
                catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
            }
        }, enabled = license.isNotBlank())
    }
    SectionTitle(T("Links"))
    listOf(T("Website") to SITE, T("Documentation") to "$SITE/docs.html", T("Privacy") to "$SITE/privacy.html", T("Source code") to "https://github.com/esoexe/astro-ai",
        T("Report a security issue") to "https://github.com/esoexe/astro-ai/security/advisories/new").forEach { (l, url) ->
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { app.ui.openUrl(url) }.padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(l, Modifier.weight(1f), color = c.text, style = MaterialTheme.typography.bodyLarge); Ic(AppIcons.ext, null, tint = c.faint, size = 16.dp)
        }
    }
    SectionTitle(T("Licenses"))
    Text(T("A.S.T.R.O AI: MIT OR Apache-2.0 OR GPL-3.0. Includes OpenSSL (Apache-2.0), curl (curl license), Inter, Instrument Serif and JetBrains Mono (SIL OFL 1.1), AndroidX and Jetpack Compose (Apache-2.0). Full texts are in the app's licenses folder and on the website."),
        style = MaterialTheme.typography.bodySmall, color = c.muted)
    Spacer(Modifier.height(24.dp))
}

@Suppress("unused") private fun unusedItems() = Unit
