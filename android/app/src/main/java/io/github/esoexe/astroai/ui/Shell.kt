package io.github.esoexe.astroai.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.long
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.engine.Backend
import io.github.esoexe.astroai.ui.screens.AgentScreen
import io.github.esoexe.astroai.ui.screens.CalculatorScreen
import io.github.esoexe.astroai.ui.screens.ChatScreen
import io.github.esoexe.astroai.ui.screens.ConnectScreen
import io.github.esoexe.astroai.ui.screens.ConversationsScreen
import io.github.esoexe.astroai.ui.screens.FeedbackScreen
import io.github.esoexe.astroai.ui.screens.KnowledgeScreen
import io.github.esoexe.astroai.ui.screens.LabScreen
import io.github.esoexe.astroai.ui.screens.LiveVoice
import io.github.esoexe.astroai.ui.screens.NotesScreen
import io.github.esoexe.astroai.ui.screens.ReaderScreen
import io.github.esoexe.astroai.ui.screens.SettingsScreen
import io.github.esoexe.astroai.ui.screens.TasksScreen
import io.github.esoexe.astroai.ui.screens.WelcomeScreen
import io.github.esoexe.astroai.ui.screens.WorkspaceScreen
import kotlinx.coroutines.launch

class NavItem(val route: String, val label: String, val icon: ImageVector)

val NAV = listOf(
    NavItem("chat", "Chat", AppIcons.chat),
    NavItem("workspace", "Workspace", AppIcons.code),
    NavItem("agent", "Agent", AppIcons.agent),
    NavItem("notes", "Notes", AppIcons.note),
    NavItem("tasks", "Tasks", AppIcons.task),
    NavItem("reader", "Reader", AppIcons.globe),
    NavItem("knowledge", "Knowledge", AppIcons.book),
    NavItem("calculator", "Calculator", AppIcons.calc),
    NavItem("lab", "Lab", AppIcons.flask),
)

@Composable
fun AppRoot() {
    val ui = app.ui
    val backend = app.backend
    Box(Modifier.fillMaxSize().background(c.bg)) {
        when {
            // A pairing link (QR code) is confirmed first, even on first run or while the engine starts.
            ui.pendingPair != null && backend.phase != Backend.Phase.Ready -> ConnectScreen(fromError = false, onBack = { ui.pendingPair = null })
            !app.prefs.welcomeDone -> WelcomeScreen()
            backend.phase == Backend.Phase.Ready -> MainShell()
            backend.phase == Backend.Phase.Starting -> Starting()
            backend.phase == Backend.Phase.NeedsLogin || backend.phase == Backend.Phase.NeedsSetup || backend.phase == Backend.Phase.Unreachable -> ConnectScreen(fromError = true)
            else -> EngineFailed()
        }
        Toasts(ui, Modifier.align(Alignment.BottomCenter).navigationBarsPadding().imePadding().padding(bottom = 72.dp))
    }
}

@Composable
private fun Starting() {
    Column(Modifier.fillMaxSize().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        OrbitLogo(96.dp)
        Spacer(Modifier.height(24.dp))
        Text("A.S.T.R.O AI", style = MaterialTheme.typography.headlineMedium, color = c.text)
        Spacer(Modifier.height(6.dp))
        Text(if (app.backend.isPc) T("Connecting to your computer…") else T("Starting the engine on this phone…"), style = MaterialTheme.typography.bodyMedium, color = c.muted)
        Spacer(Modifier.height(20.dp))
        LinearProgressIndicator(Modifier.width(160.dp).clip(RoundedCornerShape(4.dp)), color = c.accent, trackColor = c.surface2)
    }
}

@Composable
private fun EngineFailed() {
    val b = app.backend
    var showLog by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().verticalScroll(rememberScrollState()).padding(24.dp)) {
        Spacer(Modifier.height(40.dp))
        OrbitLogo(64.dp, animated = false)
        Spacer(Modifier.height(16.dp))
        Text(T("The engine could not start"), style = MaterialTheme.typography.headlineSmall, color = c.text)
        Spacer(Modifier.height(8.dp))
        Text(b.problem.ifEmpty { T("Something went wrong while starting.") }, color = c.muted)
        Spacer(Modifier.height(20.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Primary(T("Try again"), { b.restartEngine() }, icon = AppIcons.refresh)
            Secondary(T("Connect to my computer"), { app.ui.go("connect"); app.prefs.connMode = "pc"; b.start() }, icon = AppIcons.server)
        }
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Secondary(if (showLog) T("Hide details") else T("Show details"), { showLog = !showLog })
            Secondary(T("Send report"), {
                app.chat.scope.launch {
                    try {
                        io.github.esoexe.astroai.CrashReports.sendNow(app, "engine", b.problem, b.log)
                        app.ui.toast(io.github.esoexe.astroai.core.tr("Report sent. Thank you."))
                    } catch (e: Exception) { app.ui.toast(e.message ?: "", "error") }
                }
            })
        }
        if (showLog) {
            Spacer(Modifier.height(12.dp))
            SelectionContainer {
                Text(b.log.ifEmpty { "(no log)" }, Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(c.codeBg).padding(12.dp),
                    style = MaterialTheme.typography.bodySmall.copy(fontFamily = Mono), color = c.muted)
            }
        }
    }
}

@Composable
private fun MainShell() {
    val ui = app.ui
    val drawer = rememberDrawerState(if (ui.drawerOpen) DrawerValue.Open else DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) { snapshotFlow { ui.drawerOpen }.collect { if (it) drawer.open() else drawer.close() } }
    LaunchedEffect(drawer) { snapshotFlow { drawer.currentValue }.collect { ui.drawerOpen = it == DrawerValue.Open } }
    LaunchedEffect(Unit) { app.updater.check() }
    BackHandler(enabled = ui.drawerOpen) { ui.drawerOpen = false }
    BackHandler(enabled = !ui.drawerOpen && ui.route != "chat" && !ui.liveOpen) { ui.go("chat") }

    ModalNavigationDrawer(drawerState = drawer, gesturesEnabled = ui.route != "workspace" || ui.drawerOpen, drawerContent = {
        ModalDrawerSheet(drawerContainerColor = c.surface, drawerShape = RoundedCornerShape(topEnd = 20.dp, bottomEnd = 20.dp), modifier = Modifier.width(310.dp)) {
            DrawerContent(onClose = { scope.launch { drawer.close() } })
        }
    }) {
        Column(Modifier.fillMaxSize().background(c.bg)) {
            UpdateBanner()
            Box(Modifier.weight(1f)) {
                AnimatedContent(ui.route, transitionSpec = { fadeIn() togetherWith fadeOut() }, label = "route") { r ->
                    val menu = { ui.drawerOpen = true }
                    when (r) {
                        "chats" -> ConversationsScreen(menu)
                        "notes" -> NotesScreen(menu)
                        "tasks" -> TasksScreen(menu)
                        "reader" -> ReaderScreen(menu)
                        "knowledge" -> KnowledgeScreen(menu)
                        "calculator" -> CalculatorScreen(menu)
                        "lab" -> LabScreen(menu)
                        "agent" -> AgentScreen(menu)
                        "workspace" -> WorkspaceScreen(menu)
                        "settings" -> SettingsScreen(menu)
                        "feedback" -> FeedbackScreen(menu)
                        "connect" -> ConnectScreen(fromError = false, onBack = { ui.go("settings:connection") })
                        else -> ChatScreen(menu)
                    }
                }
            }
        }
    }
    AnimatedVisibility(ui.liveOpen, enter = slideInVertically { it } + fadeIn(), exit = slideOutVertically { it } + fadeOut()) { LiveVoice() }
}

@Composable
private fun UpdateBanner() {
    val u = app.updater
    val info = u.available ?: return
    if (u.skipped) return
    Surface(color = c.accentSoft, modifier = Modifier.fillMaxWidth().statusBarsPadding()) {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Ic(AppIcons.download, null, tint = c.accent, size = 18.dp)
                Spacer(Modifier.width(10.dp))
                Text(T("Version # is available", info.version), Modifier.weight(1f), style = MaterialTheme.typography.labelLarge, color = c.text)
                if (u.progress < 0f) {
                    Text(T("Later"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { u.skip(info) }.padding(8.dp), color = c.muted, style = MaterialTheme.typography.labelLarge)
                    Text(T("Update"), Modifier.clip(RoundedCornerShape(8.dp)).clickable { app.chat.scope.launch { u.install(info) } }.padding(8.dp), color = c.accent, style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold))
                }
            }
            if (u.progress >= 0f) LinearProgressIndicator({ u.progress }, Modifier.fillMaxWidth().padding(top = 6.dp), color = c.accent, trackColor = c.surface2)
        }
    }
}

@Composable
private fun DrawerContent(onClose: () -> Unit) {
    val ui = app.ui
    val chat = app.chat
    val convs = app.store.conversations.sortedWith(compareByDescending<org.json.JSONObject> { it.bool("pinned") }.thenByDescending { it.long("updatedAt") }).take(30)
    Column(Modifier.fillMaxHeight().statusBarsPadding().navigationBarsPadding()) {
        Row(Modifier.padding(start = 18.dp, end = 8.dp, top = 14.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            OrbitLogo(30.dp)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text("A.S.T.R.O AI", style = MaterialTheme.typography.titleMedium, color = c.text)
                Text(if (app.backend.isPc) T("Connected to your computer") else T("Running on this phone"), style = MaterialTheme.typography.labelSmall, color = c.muted)
            }
            IconBtn(AppIcons.mic, T("Live voice"), { ui.go("live") }, tint = c.accent)
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp).clip(RoundedCornerShape(12.dp)).background(c.accent).clickable { chat.newChat(); ui.go("chat") }.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Ic(AppIcons.plus, null, tint = c.onAccent, size = 18.dp)
            Spacer(Modifier.width(10.dp))
            Text(T("New chat"), color = c.onAccent, style = MaterialTheme.typography.labelLarge)
        }
        LazyColumn(Modifier.weight(1f).padding(horizontal = 8.dp)) {
            items(NAV, key = { it.route }) { n -> DrawerRow(n.icon, T(n.label), ui.route == n.route) { ui.go(n.route) } }
            item { HorizontalDivider(Modifier.padding(vertical = 8.dp, horizontal = 10.dp), color = c.border) }
            item {
                Row(Modifier.padding(horizontal = 12.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(T("Recent chats").uppercase(), Modifier.weight(1f), style = MaterialTheme.typography.labelSmall, color = c.faint)
                    Text(T("All"), Modifier.clip(RoundedCornerShape(6.dp)).clickable { ui.go("chats") }.padding(6.dp), style = MaterialTheme.typography.labelMedium, color = c.accent)
                }
            }
            if (convs.isEmpty()) item { Text(T("No chats yet"), Modifier.padding(12.dp), color = c.faint, style = MaterialTheme.typography.bodySmall) }
            items(convs, key = { "c" + it.str("id") }) { cv ->
                val id = cv.str("id")
                Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(if (chat.activeId == id && ui.route == "chat") c.surface2 else c.surface)
                    .clickable { chat.open(id); ui.go("chat") }.padding(horizontal = 12.dp, vertical = 9.dp), verticalAlignment = Alignment.CenterVertically) {
                    if (cv.bool("pinned")) { Ic(AppIcons.pin, null, tint = c.accent, size = 13.dp); Spacer(Modifier.width(6.dp)) }
                    Text(cv.str("title").ifEmpty { T("New chat") }, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.bodyMedium, color = c.text)
                }
            }
        }
        HorizontalDivider(color = c.border)
        Row(Modifier.padding(8.dp).horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            DrawerRow(AppIcons.gear, T("Settings"), ui.route == "settings", compact = true) { ui.go("settings") }
            DrawerRow(AppIcons.hand, T("Feedback"), ui.route == "feedback", compact = true) { ui.go("feedback") }
            val dark = c.dark
            DrawerRow(if (dark) AppIcons.sun else AppIcons.moon, if (dark) T("Light") else T("Dark"), false, compact = true) { app.prefs.theme = if (dark) "light" else "dark" }
        }
    }
}

@Composable
private fun DrawerRow(icon: ImageVector, label: String, selected: Boolean, compact: Boolean = false, onClick: () -> Unit) {
    Row(Modifier.then(if (compact) Modifier else Modifier.fillMaxWidth()).clip(RoundedCornerShape(10.dp)).background(if (selected) c.accentSoft else c.surface)
        .clickable(onClick = onClick).padding(horizontal = 12.dp, vertical = if (compact) 8.dp else 11.dp), verticalAlignment = Alignment.CenterVertically) {
        Ic(icon, null, tint = if (selected) c.accent else c.muted, size = 19.dp)
        Spacer(Modifier.width(if (compact) 6.dp else 14.dp))
        Text(label, style = MaterialTheme.typography.bodyMedium.copy(fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal), color = c.text)
    }
}

@Composable
fun Screen(title: String, onMenu: () -> Unit, sub: String = "", actions: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit = {}, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        TopBar(title, sub, onMenu = onMenu, actions = actions)
        Box(Modifier.weight(1f).fillMaxWidth()) { content() }
    }
}

@Suppress("unused") private val dummySize = Modifier.size(0.dp)
