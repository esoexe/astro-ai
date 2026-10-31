package io.github.esoexe.astroai.ui.screens

import io.github.esoexe.astroai.MicPermission
import androidx.activity.compose.BackHandler
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.core.T
import io.github.esoexe.astroai.core.tr
import io.github.esoexe.astroai.brain.OneBrain
import io.github.esoexe.astroai.ui.AppIcons
import io.github.esoexe.astroai.ui.Ic
import io.github.esoexe.astroai.ui.IconBtn
import io.github.esoexe.astroai.ui.Peach
import io.github.esoexe.astroai.ui.app
import io.github.esoexe.astroai.ui.c
import kotlin.math.sin

private fun recognitionLanguage(): String? = when (app.prefs.language) {
    "English" -> "en-US"; "French" -> "fr-FR"; "Arabic" -> "ar"
    else -> when (io.github.esoexe.astroai.core.I18n.lang) { "fr" -> "fr-FR"; "ar" -> "ar"; else -> null }
}

/** Realtime voice: speak, the brain answers out loud, then it listens again. Hands-free until you close it. */
@Composable
fun LiveVoice() {
    val chat = app.chat
    val speech = app.speech
    val ctx = LocalContext.current
    var phase by remember { mutableStateOf("idle") }   // idle, listening, thinking, speaking, paused
    var heard by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    var open by remember { mutableStateOf(true) }

    fun listen() {
        if (!open) return
        if (!MicPermission.ensure(ctx)) { phase = "paused"; error = tr("Voice input needs the microphone permission."); return }
        error = ""; heard = ""; phase = "listening"
        speech.listen(recognitionLanguage(), onPartial = { heard = it }, onFinal = { said ->
            if (!open || phase == "paused") return@listen
            if (said.isBlank()) { phase = "paused"; return@listen }
            heard = said; phase = "thinking"
            chat.send(said, voice = true, onDone = { d ->
                if (!open) return@send
                if (d.content.isBlank() || d.status == "error") { error = d.error.ifEmpty { tr("No answer this time") }; phase = "paused"; return@send }
                phase = "speaking"
                speech.speak(d.content, d.id) { app.ui.post { if (open && phase == "speaking") listen() } }
            })
        }, onError = { e -> error = e; phase = "paused" })
    }

    fun close() {
        open = false
        speech.stopListening(); speech.stopSpeaking()
        if (chat.loading) chat.stop()
        app.ui.closeLive()
    }

    LaunchedEffect(Unit) { speech.warmUp(); listen() }
    DisposableEffect(Unit) { onDispose { open = false; speech.stopListening(); speech.stopSpeaking() } }
    BackHandler { close() }
    LaunchedEffect(chat.loading, speech.speaking) {
        if (phase == "thinking" && !chat.loading && !speech.speaking) {
            // The answer finished without speaking (stopped or empty).
            kotlinx.coroutines.delay(800)
            if (phase == "thinking" && !chat.loading) phase = "paused"
        }
    }

    val last = chat.messages.lastOrNull()?.takeIf { it.role == "assistant" }
    val pending = last?.current?.actions?.filter { it.status == "pending" }.orEmpty()

    Column(
        Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color(0xFF0A0A0C), Color(0xFF1A1110)))).statusBarsPadding().navigationBarsPadding()
            .clickable(remember { MutableInteractionSource() }, null) {},
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Row(Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Text(T("Live voice"), style = MaterialTheme.typography.titleLarge, color = Color(0xFFF3F4F3))
                Text(if (app.prefs.model == "brain") OneBrain.label(app, voice = true) else modelLabel(app.prefs.model), style = MaterialTheme.typography.labelSmall, color = Color(0xFFA1A1AA))
            }
            IconBtn(AppIcons.x, T("Close"), { close() }, tint = Color(0xFFF3F4F3))
        }
        Spacer(Modifier.weight(0.6f))
        Orb(phase, speech.level) {
            when (phase) {
                "speaking" -> { speech.stopSpeaking(); listen() }
                "listening" -> { speech.stopListening(); phase = "paused" }
                "thinking" -> { chat.stop(); phase = "paused" }
                else -> listen()
            }
        }
        Spacer(Modifier.height(28.dp))
        Text(
            when (phase) {
                "listening" -> T("Listening…")
                "thinking" -> T("Thinking together…")
                "speaking" -> T("Speaking · tap to interrupt")
                "paused" -> T("Paused · tap to talk")
                else -> T("Starting…")
            },
            style = MaterialTheme.typography.titleMedium, color = Peach,
        )
        Spacer(Modifier.height(12.dp))
        Column(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 28.dp).verticalScroll(rememberScrollState()), horizontalAlignment = Alignment.CenterHorizontally) {
            if (heard.isNotEmpty()) Text("“$heard”", style = MaterialTheme.typography.bodyLarge, color = Color(0xFFF3F4F3), textAlign = TextAlign.Center)
            val answer = last?.current?.content.orEmpty()
            if (answer.isNotEmpty() && phase != "listening") {
                Spacer(Modifier.height(14.dp))
                Text(app.speech.speakable(answer), style = MaterialTheme.typography.bodyMedium, color = Color(0xFFA1A1AA), textAlign = TextAlign.Center, maxLines = 8, overflow = TextOverflow.Ellipsis)
            }
            if (error.isNotEmpty()) { Spacer(Modifier.height(12.dp)); Text(error, style = MaterialTheme.typography.bodySmall, color = Color(0xFFFF9C9C), textAlign = TextAlign.Center) }
            if (pending.isNotEmpty()) {
                Spacer(Modifier.height(16.dp))
                androidx.compose.runtime.CompositionLocalProvider(io.github.esoexe.astroai.ui.LocalAstro provides io.github.esoexe.astroai.ui.DarkAstro) { pending.forEach { ActionCardView(it) } }
            }
        }
        Row(Modifier.padding(20.dp), horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
            RoundBtn(if (phase == "listening") AppIcons.micOff else AppIcons.mic, if (phase == "listening") T("Pause") else T("Talk")) {
                if (phase == "listening") { speech.stopListening(); phase = "paused" } else { speech.stopSpeaking(); listen() }
            }
            RoundBtn(AppIcons.chat, T("Show the chat")) { close() }
        }
    }
}

@Composable
private fun RoundBtn(icon: androidx.compose.ui.graphics.vector.ImageVector, desc: String, onClick: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(56.dp).clip(CircleShape).background(Color(0x22FFFFFF)).clickable(onClick = onClick), contentAlignment = Alignment.Center) { Ic(icon, desc, tint = Color(0xFFF3F4F3), size = 24.dp) }
        Spacer(Modifier.height(6.dp))
        Text(desc, style = MaterialTheme.typography.labelSmall, color = Color(0xFFA1A1AA))
    }
}

@Composable
private fun Orb(phase: String, level: Float, onTap: () -> Unit) {
    val t = rememberInfiniteTransition(label = "orb")
    val spin by t.animateFloat(0f, 6.2832f, infiniteRepeatable(tween(if (phase == "thinking") 1400 else 5000, easing = LinearEasing)), label = "spin")
    val breathe by t.animateFloat(0.94f, 1.04f, infiniteRepeatable(tween(1800), RepeatMode.Reverse), label = "b")
    val lvl by animateFloatAsState(if (phase == "listening") level else if (phase == "speaking") 0.45f else 0f, tween(120), label = "lvl")
    Box(Modifier.size(240.dp).clip(CircleShape).clickable(onClick = onTap), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val r = size.minDimension / 2f
            val center = Offset(r, r)
            val base = r * 0.52f * breathe * (1f + lvl * 0.35f)
            for (i in 0 until 3) {
                val rr = base * (1.25f + i * 0.22f) + sin(spin * (i + 1)) * r * 0.02f * (1 + lvl * 4)
                drawCircle(Peach.copy(alpha = 0.10f - i * 0.025f + lvl * 0.06f), rr, center, style = Stroke(r * 0.012f))
            }
            drawCircle(Brush.radialGradient(listOf(Color(0xFFFFD9BD), Peach, Color(0xFFB0532A)), center, base), base, center)
            if (phase == "thinking") {
                for (k in 0 until 6) {
                    val a = spin + k * 1.047f
                    drawCircle(Color(0xCCFFFFFF), r * 0.025f, Offset(center.x + kotlin.math.cos(a) * base * 1.35f, center.y + sin(a) * base * 1.35f))
                }
            }
        }
        Ic(when (phase) { "listening" -> AppIcons.mic; "speaking" -> AppIcons.speaker; "thinking" -> AppIcons.brain; else -> AppIcons.play }, null, tint = Color(0xFF1A1110), size = 40.dp)
    }
}

@Suppress("unused") private val keepHeight = Modifier.heightIn(min = 0.dp)
