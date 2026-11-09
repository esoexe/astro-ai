package io.github.esoexe.astroai.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.CrashReports
import io.github.esoexe.astroai.core.I18n
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.engine.Backend
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Card
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.OrbitLogo
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.SectionTitle
import io.github.esoexe.astroai.ui.ThinkingDots
import io.github.esoexe.astroai.ui.ToggleRow
import io.github.esoexe.astroai.ui.TopBar
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import kotlinx.coroutines.launch

// ---------------- first start ----------------

@Composable
fun WelcomeScreen() {
    val p = app.prefs
    var step by remember { mutableStateOf(0) }
    var name by remember { mutableStateOf(p.name) }
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding().padding(24.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            repeat(3) { i -> Box(Modifier.padding(4.dp).size(if (i == step) 22.dp else 8.dp, 8.dp).clip(CircleShape).background(if (i <= step) c.accent else c.surface2)) }
        }
        AnimatedContent(step, Modifier.weight(1f), label = "welcome") { s ->
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                when (s) {
                    0 -> {
                        OrbitLogo(110.dp)
                        Spacer(Modifier.height(24.dp))
                        Text("A.S.T.R.O AI", style = MaterialTheme.typography.displaySmall, color = c.text)
                        Text(T("All Systems Thinking, Reasoning as One"), style = MaterialTheme.typography.bodyMedium, color = c.accent)
                        Spacer(Modifier.height(18.dp))
                        Text(T("Every free AI model thinks about your question together, and you get one careful answer. Chat, talk live, write code, run it, and let the agent build things."),
                            style = MaterialTheme.typography.bodyLarge, color = c.muted, textAlign = TextAlign.Center)
                        Spacer(Modifier.height(18.dp))
                        Feature(AppIcons.brain, T("One Brain"), T("All models answer, then merge and review"))
                        Feature(AppIcons.wave, T("Live voice"), T("Talk hands-free, it answers out loud"))
                        Feature(AppIcons.code, T("Workspace and agent"), T("Files, code that runs, and an agent with undo"))
                        Feature(AppIcons.note, T("Your organiser"), T("Notes, tasks, reminders and memory, on this phone"))
                        Spacer(Modifier.height(12.dp))
                        Text(T("Free model tiers have daily limits; they are not unlimited."), style = MaterialTheme.typography.bodySmall, color = c.faint, textAlign = TextAlign.Center)
                    }
                    1 -> {
                        Text(T("About you"), style = MaterialTheme.typography.headlineMedium, color = c.text)
                        Spacer(Modifier.height(16.dp))
                        Field(name, { name = it }, T("What should A.S.T.R.O AI call you? (optional)"))
                        SectionTitle(T("Interface language"))
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Chip(T("Phone setting"), selected = p.uiLang.isEmpty(), onClick = { p.uiLang = ""; app.applyUiLanguage() })
                            I18n.LANGS.forEach { (k, l) -> Chip(l, selected = p.uiLang == k, onClick = { p.uiLang = k; app.applyUiLanguage() }) }
                        }
                        SectionTitle(T("Theme"))
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf("system" to T("Phone setting"), "dark" to T("Dark"), "light" to T("Light")).forEach { (k, l) -> Chip(l, selected = p.theme == k, onClick = { p.theme = k }) }
                        }
                        SectionTitle(T("Privacy"))
                        ToggleRow(T("Send crash reports automatically"), T("Off by default. Helps fix bugs; personal paths and keys are removed."), p.crashReports, { p.crashReports = it })
                    }
                    else -> {
                        Text(T("Where should it run?"), style = MaterialTheme.typography.headlineMedium, color = c.text)
                        Spacer(Modifier.height(16.dp))
                        EngineOption(AppIcons.spark, T("On this phone"), T("Recommended. The engine runs inside the app; models answer through their providers online. Works anywhere with internet."),
                            !app.backend.isPc) { if (app.backend.isPc) app.backend.useBuiltIn() }
                        Spacer(Modifier.height(10.dp))
                        EngineOption(AppIcons.server, T("On my computer"), T("Use A.S.T.R.O AI running on your PC (same Wi-Fi or a private network): its models, keys and projects."),
                            app.backend.isPc) { app.ui.go("connect"); p.welcomeDone = true }
                        Spacer(Modifier.height(16.dp))
                        Text(when (app.backend.phase) {
                            Backend.Phase.Ready -> T("Engine ready · # models", app.backend.models.size)
                            Backend.Phase.Starting -> T("Starting the engine…")
                            else -> app.backend.problem
                        }, style = MaterialTheme.typography.bodySmall, color = c.muted)
                    }
                }
            }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            if (step > 0) Secondary(T("Back"), { step-- })
            Spacer(Modifier.weight(1f))
            Primary(if (step < 2) T("Continue") else T("Start"), {
                if (step == 1) p.name = name
                if (step < 2) step++ else { p.welcomeDone = true; app.ui.go("chat") }
            }, icon = if (step < 2) AppIcons.right else AppIcons.check)
        }
    }
}

@Composable
private fun Feature(icon: ImageVector, title: String, sub: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(40.dp).clip(RoundedCornerShape(12.dp)).background(c.accentSoft), contentAlignment = Alignment.Center) { Ic(icon, null, tint = c.accent) }
        Spacer(Modifier.width(14.dp))
        Column { Text(title, style = MaterialTheme.typography.titleSmall, color = c.text); Text(sub, style = MaterialTheme.typography.bodySmall, color = c.muted) }
    }
}

@Composable
private fun EngineOption(icon: ImageVector, title: String, sub: String, selected: Boolean, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(if (selected) c.accentSoft else c.surface)
        .border(1.dp, if (selected) c.accent else c.border, RoundedCornerShape(16.dp)).clickable(onClick = onClick).padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
        Ic(icon, null, tint = c.accent, size = 26.dp)
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) { Text(title, style = MaterialTheme.typography.titleMedium, color = c.text); Text(sub, style = MaterialTheme.typography.bodySmall, color = c.muted) }
        if (selected) Ic(AppIcons.check, null, tint = c.accent)
    }
}

// ---------------- connect to a computer ----------------

@Composable
fun ConnectScreen(fromError: Boolean, onBack: (() -> Unit)? = null) {
    val b = app.backend
    val p = app.prefs
    val ui = app.ui
    val scope = rememberCoroutineScope()
    var url by remember { mutableStateOf(p.pcUrl) }
    var password by remember { mutableStateOf("") }
    var code by remember { mutableStateOf("") }
    var state by remember { mutableStateOf<org.json.JSONObject?>(null) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf(if (fromError) tr(b.problem) else "") }
    // Coming back from an error with a known address: ask for what the computer needs.
    if (fromError && state == null && b.phase == Backend.Phase.NeedsLogin) state = org.json.JSONObject().put("enabled", true).put("setup", false).put("pairing", b.pairing).put("_url", p.pcUrl)
    if (fromError && state == null && b.phase == Backend.Phase.NeedsSetup) state = org.json.JSONObject().put("enabled", true).put("setup", true).put("_url", p.pcUrl)

    fun run(block: suspend () -> Unit) {
        busy = true; error = ""
        scope.launch {
            try { block(); if (!fromError) ui.go("chat") } catch (e: Exception) { error = tr(e.message ?: "Could not connect") }
            busy = false
        }
    }
    fun check() {
        busy = true; error = ""
        scope.launch {
            try { state = b.probe(url).also { url = it.str("_url") } } catch (e: Exception) { error = tr(e.message ?: "Could not reach it"); state = null }
            busy = false
        }
    }
    val link = ui.pendingPair
    Column(Modifier.fillMaxSize().navigationBarsPadding().imePadding()) {
        TopBar(T("Connect to your computer"), onBack = onBack ?: if (fromError) ({ b.useBuiltIn() }) else null)
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(20.dp)) {
            if (link != null) {
                // From a QR code / link: never connect without asking (any app or page can open such a link).
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Text(T("Connect to #?", link.name.ifEmpty { tr("your computer") }), style = MaterialTheme.typography.titleMedium, color = c.text)
                        Spacer(Modifier.height(4.dp))
                        Text(link.urls.first(), style = MaterialTheme.typography.bodySmall, color = c.muted)
                        Spacer(Modifier.height(8.dp))
                        Text(T("Only continue if you just made this code on your own computer. This phone will then use that computer's models, keys, chats and projects."), style = MaterialTheme.typography.bodyMedium, color = c.text)
                        Spacer(Modifier.height(12.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            Primary(if (busy) T("Connecting…") else T("Connect"), { run { b.pair(link.urls, link.code); p.welcomeDone = true; ui.pendingPair = null } }, enabled = !busy, icon = AppIcons.server)
                            Secondary(T("Cancel"), { ui.pendingPair = null; error = "" }, enabled = !busy)
                        }
                    }
                }
            } else {
                if (p.pcPaired && p.pcToken.isNotEmpty() && !b.isPc) {
                    Card(Modifier.fillMaxWidth()) {
                        Column(Modifier.padding(16.dp)) {
                            Text(T("Paired with #", p.pcName.ifEmpty { p.pcUrl }), style = MaterialTheme.typography.titleMedium, color = c.text)
                            Text(p.pcUrl, style = MaterialTheme.typography.bodySmall, color = c.muted)
                            Spacer(Modifier.height(10.dp))
                            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                Primary(T("Reconnect"), { b.reconnectPc(); if (!fromError) ui.go("chat") }, icon = AppIcons.server)
                                Secondary(T("Forget this computer"), { b.forgetPc() }, danger = true)
                            }
                        }
                    }
                    Spacer(Modifier.height(18.dp))
                }
                Text(T("1. On your computer, open A.S.T.R.O AI (version 1.8.1 or later) and go to Settings > Phone."), style = MaterialTheme.typography.bodyMedium, color = c.text)
                Spacer(Modifier.height(6.dp))
                Text(T("2. Turn on Let my phone connect and click Pair a phone."), style = MaterialTheme.typography.bodyMedium, color = c.text)
                Spacer(Modifier.height(6.dp))
                Text(T("3. Scan the QR code with this phone's camera, or type the address and the 8-digit code below. Both devices must be on the same Wi-Fi, or on a private network like Tailscale."), style = MaterialTheme.typography.bodyMedium, color = c.text)
                Spacer(Modifier.height(16.dp))
                Field(url, { url = it; state = null }, T("Computer address"), keyboard = KeyboardType.Uri, onDone = { check() }, placeholder = "192.168.1.20:4174")
                Spacer(Modifier.height(10.dp))
                val st = state
                if (st == null) Primary(if (busy) T("Checking…") else T("Check"), { check() }, enabled = url.isNotBlank() && !busy, icon = AppIcons.search)
                else {
                    Row(verticalAlignment = Alignment.CenterVertically) { Ic(AppIcons.check, null, tint = c.ok, size = 18.dp); Spacer(Modifier.width(8.dp)); Text(T("Found A.S.T.R.O AI at #", st.str("_url").ifEmpty { b.normalizeUrl(url) }), color = c.text) }
                    Spacer(Modifier.height(10.dp))
                    when {
                        st.bool("pairing") -> Field(code, { v -> code = v.filter { it.isDigit() }.take(8) }, T("Pairing code (8 digits)"), keyboard = KeyboardType.NumberPassword, onDone = { if (code.length == 8) run { b.pair(listOf(st.str("_url")), code); code = "" } })
                        !st.bool("enabled") -> Text(T("This computer has no password. Only connect on a network you trust."), style = MaterialTheme.typography.bodySmall, color = c.warn)
                        st.bool("setup") -> Field(password, { password = it }, T("Create the computer's password (8+ characters)"), password = true, onDone = { })
                        else -> Field(password, { password = it }, T("The computer's password"), password = true, onDone = { })
                    }
                    Spacer(Modifier.height(10.dp))
                    val ready = when {
                        st.bool("pairing") -> code.length == 8
                        !st.bool("enabled") -> true
                        else -> password.length >= (if (st.bool("setup")) 8 else 1)
                    }
                    Primary(if (busy) T("Connecting…") else T("Connect"), {
                        run {
                            val u = st.str("_url").ifEmpty { url }
                            when {
                                st.bool("pairing") -> { b.pair(listOf(u), code); code = "" }
                                !st.bool("enabled") -> b.useOpenPc(u)
                                else -> { b.signIn(u, password, create = st.bool("setup")); password = "" }
                            }
                        }
                    }, enabled = !busy && ready, icon = AppIcons.server)
                }
            }
            if (busy) Row(Modifier.padding(top = 12.dp)) { ThinkingDots() }
            if (error.isNotEmpty()) Text(error, Modifier.padding(top = 12.dp), color = c.danger, style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(20.dp))
            Text(T("Plain http is only allowed to this phone and private network addresses (192.168.x, 10.x, 172.16–31.x, 100.64–127.x). Pairing gives this phone its own key, which you can remove on the computer at any time. With the self-hosted server (./workspace_server), sign in with its password instead."),
                style = MaterialTheme.typography.bodySmall, color = c.faint)
            if (fromError) { Spacer(Modifier.height(16.dp)); Secondary(T("Use this phone instead"), { b.useBuiltIn() }, icon = AppIcons.spark) }
        }
    }
}

// ---------------- feedback ----------------

@Composable
fun FeedbackScreen(onMenu: () -> Unit) {
    var kind by remember { mutableStateOf("idea") }
    var text by remember { mutableStateOf("") }
    var contact by remember { mutableStateOf("") }
    var sending by remember { mutableStateOf(false) }
    var sent by remember { mutableStateOf(false) }
    Screen(T("Feedback"), onMenu, T("Tell the team what to fix or build")) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).imePadding().padding(16.dp)) {
            if (sent) {
                Text(T("Thank you! Your message was sent."), style = MaterialTheme.typography.titleMedium, color = c.text)
                Spacer(Modifier.height(10.dp))
                Secondary(T("Send another"), { sent = false; text = "" })
                return@Column
            }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf("idea" to "Idea", "bug" to "Bug", "answer" to "Bad answers", "other" to "Other").forEach { (k, l) -> Chip(T(l), selected = kind == k, onClick = { kind = k }) }
            }
            Spacer(Modifier.height(10.dp))
            Field(text, { text = it }, T("Your message"), singleLine = false, minLines = 5)
            Spacer(Modifier.height(8.dp))
            Field(contact, { contact = it }, T("Email for a reply (optional)"), keyboard = KeyboardType.Email)
            Text(T("Sent with the app version and Android version only. No chats are included."), style = MaterialTheme.typography.bodySmall, color = c.faint, modifier = Modifier.padding(top = 6.dp))
            Spacer(Modifier.height(12.dp))
            Primary(if (sending) T("Sending…") else T("Send"), {
                sending = true
                app.chat.scope.launch {
                    try { CrashReports.sendNow(app, kind, text.trim().take(4000), "", kind = "feedback", contact = contact.trim()); sent = true }
                    catch (e: Exception) { app.ui.toast(e.message ?: tr("Could not send"), "error") }
                    sending = false
                }
            }, enabled = text.trim().length >= 3 && !sending, icon = AppIcons.send)
            SectionTitle(T("Elsewhere"))
            Text(T("Public discussion and bug reports: GitHub issues."), Modifier.clip(RoundedCornerShape(8.dp)).clickable { app.ui.openUrl("https://github.com/esoexe/astro-ai/issues") }.padding(vertical = 8.dp), color = c.accent)
            Text(T("Join the beta to try features early."), Modifier.clip(RoundedCornerShape(8.dp)).clickable { app.ui.openUrl("$SITE/beta.html") }.padding(vertical = 8.dp), color = c.accent)
        }
    }
}
