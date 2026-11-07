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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.net.Api
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Card
import io.github.esoexe.astroai.ui.Chip
import io.github.esoexe.astroai.ui.Field
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.Primary
import io.github.esoexe.astroai.ui.Screen
import io.github.esoexe.astroai.ui.Secondary
import io.github.esoexe.astroai.ui.ToggleRow
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import kotlinx.coroutines.launch
import org.json.JSONObject

@Composable
fun ProScreen(onMenu: () -> Unit) {
    val b = app.backend
    val p = app.prefs
    val ui = app.ui
    var phoneInput by remember { mutableStateOf(p.contactPhone) }
    var testCalling by remember { mutableStateOf(false) }

    Screen(T("A.S.T.R.O Pro"), onMenu, sub = T("Dual Presence, Sentinel & Phone Calling")) {
        LazyColumn(
            Modifier.fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // 1. Executive Status Header Card
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(
                                Modifier.size(36.dp).clip(RoundedCornerShape(8.dp)).background(c.accentSoft),
                                contentAlignment = Alignment.Center
                            ) {
                                Ic(AppIcons.pro, null, tint = c.accent, size = 20.dp)
                            }
                            Spacer(Modifier.width(12.dp))
                            Column(Modifier.weight(1f)) {
                                Text(
                                    "A.S.T.R.O AI PRO · VERSION 2.0.0-PRO",
                                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold, letterSpacing = 1.sp),
                                    color = c.accent
                                )
                                Text(
                                    "Mission Control & Phone Bridge",
                                    style = MaterialTheme.typography.titleMedium,
                                    color = c.text
                                )
                            }
                            Surface(
                                color = if (p.proMode) c.ok.copy(alpha = 0.15f) else c.surface2,
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Row(Modifier.padding(horizontal = 8.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                                    Box(Modifier.size(6.dp).clip(CircleShape).background(if (p.proMode) c.ok else c.muted))
                                    Spacer(Modifier.width(6.dp))
                                    Text(if (p.proMode) "PRO ACTIVE" else "STANDBY", style = MaterialTheme.typography.labelSmall.copy(fontSize = 10.sp, fontWeight = FontWeight.Bold), color = if (p.proMode) c.ok else c.muted)
                                }
                            }
                        }
                        Spacer(Modifier.height(10.dp))
                        Text(
                            T("Dual-presence engine synchronization, autonomous project sentinel, and physical phone call execution."),
                            style = MaterialTheme.typography.bodySmall,
                            color = c.muted
                        )
                    }
                }
            }

            // 2. Physical Agent Call Execution (Core Feature)
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Ic(AppIcons.phone, null, tint = c.accent, size = 20.dp)
                            Spacer(Modifier.width(10.dp))
                            Text(T("Physical Agent Call Gate"), style = MaterialTheme.typography.titleMedium, color = c.text)
                        }
                        Spacer(Modifier.height(6.dp))
                        Text(
                            T("Physical agent calls execute ONLY on this Android device. When the agent finishes work (on PC or phone) or needs authorization, your phone rings with ringtone, vibrates with full alert banner, and dials."),
                            style = MaterialTheme.typography.bodySmall,
                            color = c.muted
                        )
                        Spacer(Modifier.height(14.dp))

                        Field(
                            phoneInput,
                            { phoneInput = it; p.contactPhone = it },
                            T("Emergency Contact / User Phone Number"),
                            placeholder = "+1 202 555 0199"
                        )
                        Spacer(Modifier.height(10.dp))

                        Text(T("Preferred Contact Channel:"), style = MaterialTheme.typography.labelMedium, color = c.text)
                        Spacer(Modifier.height(6.dp))
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf("call" to "Voice Call (Physical)", "alert" to "High-Priority Alert", "auto" to "Auto (Urgent = Call)").forEach { (k, l) ->
                                Chip(l, selected = p.contactChannel == k, onClick = { p.contactChannel = k })
                            }
                        }

                        Spacer(Modifier.height(14.dp))
                        ToggleRow(
                            T("Call when agent finishes work"),
                            T("Automatically triggers phone call and heads-up alert whenever an agent run completes on PC or phone"),
                            p.contactOnFinish,
                            { p.contactOnFinish = it }
                        )

                        Spacer(Modifier.height(14.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            Primary(
                                if (testCalling) T("Ringing & Dialing…") else T("Trigger Test Agent Call"),
                                {
                                    testCalling = true
                                    b.executeAgentCallOnAndroid("Verification test: agent task completed", p.contactChannel, "urgent")
                                    app.chat.scope.launch {
                                        kotlinx.coroutines.delay(3000)
                                        testCalling = false
                                    }
                                },
                                enabled = !testCalling,
                                icon = AppIcons.phone
                            )
                        }
                    }
                }
            }

            // 3. Dual-Presence Bridge & Failover
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Ic(AppIcons.server, null, tint = c.accent, size = 20.dp)
                            Spacer(Modifier.width(10.dp))
                            Text(T("Dual-Presence Bridge"), style = MaterialTheme.typography.titleMedium, color = c.text)
                        }
                        Spacer(Modifier.height(8.dp))
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Column {
                                Text(T("Current Hub"), style = MaterialTheme.typography.labelSmall, color = c.muted)
                                Text(if (b.isPc) (p.pcName.ifEmpty { "PC Studio" }) else "Phone Built-in Engine", style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold), color = c.text)
                            }
                            Column(horizontalAlignment = Alignment.End) {
                                Text(T("Status"), style = MaterialTheme.typography.labelSmall, color = c.muted)
                                Text(if (b.isPc && b.pcOnline) "Synced (${b.pcLatencyMs} ms)" else if (b.isFailoverActive) "Failover Active" else "Standalone", style = MaterialTheme.typography.bodyMedium, color = if (b.pcOnline) c.ok else c.warn)
                            }
                        }

                        Spacer(Modifier.height(12.dp))
                        ToggleRow(
                            T("Instant Zero-Drop Failover"),
                            T("If connection to your computer is lost, tasks and chats continue instantly on this phone's built-in engine"),
                            p.autoFailover,
                            { p.autoFailover = it }
                        )

                        Spacer(Modifier.height(10.dp))
                        Secondary(
                            if (b.isPc) T("Reconnect to PC") else T("Manage Connection"),
                            { ui.go("settings:connection") },
                            icon = AppIcons.redo
                        )
                    }
                }
            }

            // 4. Autonomous Sentinel Watcher
            item {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Ic(AppIcons.agent, null, tint = c.accent, size = 20.dp)
                            Spacer(Modifier.width(10.dp))
                            Text(T("Autonomous Sentinel Watcher"), style = MaterialTheme.typography.titleMedium, color = c.text)
                        }
                        Spacer(Modifier.height(8.dp))
                        Text(
                            T("Continuous background sentinel guarding build integrity, test failures, and autonomous agent progress."),
                            style = MaterialTheme.typography.bodySmall,
                            color = c.muted
                        )
                        Spacer(Modifier.height(12.dp))

                        ToggleRow(
                            T("Sentinel Active"),
                            T("Watch projects for stalls and test failures; dispatches emergency call if intervention is required"),
                            p.watcherEnabled,
                            { p.watcherEnabled = it }
                        )

                        Spacer(Modifier.height(8.dp))
                        Text(T("Monitored Sentinel Targets:"), style = MaterialTheme.typography.labelSmall, color = c.faint)
                        Spacer(Modifier.height(4.dp))
                        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            listOf(
                                "Build Integrity & Compilation Checks",
                                "Continuous Test Runner & PoC Verification",
                                "Agent Milestone Deadlock & Stall Detector",
                                "Permission & Approval Gatekeeper"
                            ).forEach { item ->
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Box(Modifier.size(4.dp).clip(CircleShape).background(c.ok))
                                    Spacer(Modifier.width(8.dp))
                                    Text(item, style = MaterialTheme.typography.bodySmall, color = c.text)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
