package io.github.esoexe.astroai.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.esoexe.astroai.AstroApp
import io.github.esoexe.astroai.UiBus
import io.github.esoexe.astroai.core.T
import kotlin.math.cos
import kotlin.math.sin

val c @Composable get() = LocalAstro.current

@Composable
fun Ic(icon: ImageVector, desc: String? = null, modifier: Modifier = Modifier, tint: Color = Color.Unspecified, size: Dp = 20.dp) {
    Icon(icon, desc, modifier.size(size), tint = if (tint == Color.Unspecified) androidx.compose.material3.LocalContentColor.current else tint)
}

@Composable
fun IconBtn(icon: ImageVector, desc: String, onClick: () -> Unit, modifier: Modifier = Modifier, tint: Color = c.muted, enabled: Boolean = true, size: Dp = 20.dp) {
    IconButton(onClick = onClick, modifier = modifier, enabled = enabled) { Icon(icon, desc, Modifier.size(size), tint = if (enabled) tint else c.faint) }
}

/** Top bar used by every page: menu, serif title, optional subtitle and actions. */
@Composable
fun TopBar(title: String, sub: String = "", onMenu: (() -> Unit)? = null, onBack: (() -> Unit)? = null, actions: @Composable RowScope.() -> Unit = {}) {
    Row(Modifier.fillMaxWidth().statusBarsPadding().height(56.dp).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        when {
            onBack != null -> IconBtn(AppIcons.left, T("Back"), onBack, tint = c.text)
            onMenu != null -> IconBtn(AppIcons.menu, T("Menu"), onMenu, tint = c.text)
            else -> Spacer(Modifier.width(12.dp))
        }
        Column(Modifier.weight(1f).padding(start = 4.dp)) {
            Text(title, style = MaterialTheme.typography.headlineSmall, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (sub.isNotEmpty()) Text(sub, style = MaterialTheme.typography.bodySmall, color = c.muted, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        actions()
    }
}

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Text(text.uppercase(), modifier.padding(top = 18.dp, bottom = 6.dp), style = MaterialTheme.typography.labelSmall, color = c.faint)
}

@Composable
fun Card(modifier: Modifier = Modifier, onClick: (() -> Unit)? = null, selected: Boolean = false, content: @Composable () -> Unit) {
    Surface(
        modifier = modifier.then(if (onClick != null) Modifier.clip(RoundedCornerShape(14.dp)).clickable(onClick = onClick) else Modifier),
        shape = RoundedCornerShape(14.dp), color = if (selected) c.accentSoft else c.surface,
        border = BorderStroke(1.dp, if (selected) c.accent.copy(alpha = 0.6f) else c.border),
    ) { content() }
}

@Composable
fun Chip(text: String, selected: Boolean = false, icon: ImageVector? = null, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier.clip(RoundedCornerShape(50)).background(if (selected) c.accentSoft else c.surface2)
            .border(1.dp, if (selected) c.accent.copy(alpha = 0.5f) else Color.Transparent, RoundedCornerShape(50))
            .clickable(role = Role.Button, onClick = onClick).padding(horizontal = 12.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) { Ic(icon, null, tint = if (selected) c.accent else c.muted, size = 15.dp); Spacer(Modifier.width(6.dp)) }
        Text(text, style = MaterialTheme.typography.labelMedium, color = if (selected) c.text else c.muted, maxLines = 1)
    }
}

@Composable
fun Primary(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, icon: ImageVector? = null) {
    Button(onClick, modifier, enabled = enabled, shape = RoundedCornerShape(12.dp),
        colors = ButtonDefaults.buttonColors(containerColor = c.accent, contentColor = c.onAccent), contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp)) {
        if (icon != null) { Ic(icon, null, size = 18.dp); Spacer(Modifier.width(8.dp)) }
        Text(text, style = MaterialTheme.typography.labelLarge)
    }
}

@Composable
fun Secondary(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, icon: ImageVector? = null, danger: Boolean = false) {
    OutlinedButton(onClick, modifier, enabled = enabled, shape = RoundedCornerShape(12.dp), border = BorderStroke(1.dp, if (danger) c.danger.copy(alpha = 0.6f) else c.border),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = if (danger) c.danger else c.text), contentPadding = PaddingValues(horizontal = 16.dp, vertical = 9.dp)) {
        if (icon != null) { Ic(icon, null, size = 17.dp); Spacer(Modifier.width(8.dp)) }
        Text(text, style = MaterialTheme.typography.labelLarge)
    }
}

@Composable
fun Field(value: String, onChange: (String) -> Unit, label: String, modifier: Modifier = Modifier, singleLine: Boolean = true, password: Boolean = false,
          minLines: Int = 1, keyboard: KeyboardType = KeyboardType.Text, mono: Boolean = false, onDone: (() -> Unit)? = null, placeholder: String = "") {
    OutlinedTextField(
        value, onChange, modifier.fillMaxWidth(), label = if (label.isNotEmpty()) ({ Text(label) }) else null,
        placeholder = if (placeholder.isNotEmpty()) ({ Text(placeholder, color = c.faint) }) else null,
        singleLine = singleLine, minLines = minLines, shape = RoundedCornerShape(12.dp),
        visualTransformation = if (password) PasswordVisualTransformation() else androidx.compose.ui.text.input.VisualTransformation.None,
        keyboardOptions = KeyboardOptions(keyboardType = if (password) KeyboardType.Password else keyboard, imeAction = if (onDone != null) ImeAction.Done else ImeAction.Default),
        keyboardActions = KeyboardActions(onDone = { onDone?.invoke() }),
        textStyle = if (mono) MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono) else MaterialTheme.typography.bodyLarge,
        colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = c.accent, unfocusedBorderColor = c.border, cursorColor = c.accent, focusedLabelColor = c.accent),
    )
}

@Composable
fun ToggleRow(title: String, sub: String = "", checked: Boolean, onChange: (Boolean) -> Unit, enabled: Boolean = true) {
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).clickable(enabled = enabled) { onChange(!checked) }.padding(vertical = 10.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).padding(end = 12.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, color = if (enabled) c.text else c.faint)
            if (sub.isNotEmpty()) Text(sub, style = MaterialTheme.typography.bodySmall, color = c.muted)
        }
        Switch(checked, onChange, enabled = enabled, colors = SwitchDefaults.colors(checkedTrackColor = c.accent, checkedThumbColor = c.onAccent, uncheckedBorderColor = c.border))
    }
}

@Composable
fun EmptyState(icon: ImageVector, title: String, sub: String = "", modifier: Modifier = Modifier, action: (@Composable () -> Unit)? = null) {
    Column(modifier.fillMaxWidth().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(56.dp).clip(CircleShape).background(c.surface2), contentAlignment = Alignment.Center) { Ic(icon, null, tint = c.accent, size = 26.dp) }
        Spacer(Modifier.height(14.dp))
        Text(title, style = MaterialTheme.typography.titleMedium, color = c.text)
        if (sub.isNotEmpty()) { Spacer(Modifier.height(4.dp)); Text(sub, style = MaterialTheme.typography.bodyMedium, color = c.muted, textAlign = androidx.compose.ui.text.style.TextAlign.Center) }
        if (action != null) { Spacer(Modifier.height(16.dp)); action() }
    }
}

@Composable
fun Badge(text: String, color: Color = c.accent) {
    Text(text, Modifier.clip(RoundedCornerShape(6.dp)).background(color.copy(alpha = 0.15f)).padding(horizontal = 6.dp, vertical = 2.dp),
        style = MaterialTheme.typography.labelSmall, color = color, maxLines = 1)
}

/** The "Refined orbit" mark, live: a ring, an orbiting moon and the spark at the centre. */
@Composable
fun OrbitLogo(size: Dp = 64.dp, animated: Boolean = true, modifier: Modifier = Modifier) {
    val t = rememberInfiniteTransition(label = "orbit")
    val angle by t.animateFloat(0f, 360f, infiniteRepeatable(tween(6000, easing = LinearEasing)), label = "a")
    val pulse by t.animateFloat(0.85f, 1f, infiniteRepeatable(tween(1600), RepeatMode.Reverse), label = "p")
    val accent = c.accent
    val ring = c.text.copy(alpha = 0.22f)
    Canvas(modifier.size(size)) {
        val r = this.size.minDimension / 2f
        val center = Offset(r, r)
        rotate(-24f, center) {
            drawOval(ring, topLeft = Offset(r * 0.08f, r * 0.52f), size = androidx.compose.ui.geometry.Size(r * 1.84f, r * 0.96f), style = Stroke(r * 0.06f))
        }
        val a = Math.toRadians((if (animated) angle else 40f).toDouble())
        val ex = r * 0.92f; val ey = r * 0.48f
        val px = (ex * cos(a)).toFloat(); val py = (ey * sin(a)).toFloat()
        val rot = Math.toRadians(-24.0)
        val mx = center.x + (px * cos(rot) - py * sin(rot)).toFloat()
        val my = center.y + (px * sin(rot) + py * cos(rot)).toFloat()
        val s = r * 0.62f * (if (animated) pulse else 1f)
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(center.x, center.y - s)
            quadraticTo(center.x + s * 0.12f, center.y - s * 0.12f, center.x + s, center.y)
            quadraticTo(center.x + s * 0.12f, center.y + s * 0.12f, center.x, center.y + s)
            quadraticTo(center.x - s * 0.12f, center.y + s * 0.12f, center.x - s, center.y)
            quadraticTo(center.x - s * 0.12f, center.y - s * 0.12f, center.x, center.y - s)
            close()
        }
        drawPath(path, Brush.linearGradient(listOf(Color(0xFFF7C59F), accent, Color(0xFFE07A4F)), Offset(center.x - s, center.y - s), Offset(center.x + s, center.y + s)))
        drawCircle(accent, radius = r * 0.11f, center = Offset(mx, my))
    }
}

/** Three dots that pulse while something is thinking. */
@Composable
fun ThinkingDots(color: Color = c.accent) {
    val t = rememberInfiniteTransition(label = "dots")
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
        repeat(3) { i ->
            val a by t.animateFloat(0.25f, 1f, infiniteRepeatable(tween(600, delayMillis = i * 160), RepeatMode.Reverse), label = "d$i")
            Box(Modifier.size(6.dp).clip(CircleShape).background(color.copy(alpha = a)))
        }
    }
}

@Composable
fun PulseDot(color: Color, size: Dp = 8.dp) {
    val t = rememberInfiniteTransition(label = "pulse")
    val a by t.animateFloat(0.35f, 1f, infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "a")
    Box(Modifier.size(size).clip(CircleShape).background(color.copy(alpha = a)))
}

@Composable
fun Toasts(ui: UiBus, modifier: Modifier = Modifier) {
    Column(modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        for (t in ui.toasts) {
            key(t.id) {
                var shown by remember { mutableStateOf(false) }
                LaunchedEffect(Unit) { shown = true }
                AnimatedVisibility(shown, enter = slideInVertically { it } + fadeIn(), exit = slideOutVertically { it } + fadeOut()) {
                    Surface(shape = RoundedCornerShape(14.dp), color = if (c.dark) Color(0xFF26262B) else Color(0xFF1F1F23), shadowElevation = 6.dp) {
                        Row(Modifier.padding(start = 16.dp, end = 6.dp, top = 6.dp, bottom = 6.dp).fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                            if (t.kind == "error") { Ic(AppIcons.alert, null, tint = Color(0xFFFF9C9C), size = 18.dp); Spacer(Modifier.width(10.dp)) }
                            Text(t.text, Modifier.weight(1f).padding(vertical = 8.dp), color = Color(0xFFF3F4F3), style = MaterialTheme.typography.bodyMedium)
                            if (t.action != null) TextButton({ t.run?.invoke(); ui.dismiss(t) }) { Text(t.action, color = Peach, fontWeight = FontWeight.SemiBold) }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun key(k: Any, content: @Composable () -> Unit) = androidx.compose.runtime.key(k) { content() }

@Composable
fun ConfirmDialog(title: String, text: String, confirm: String, onConfirm: () -> Unit, onDismiss: () -> Unit, danger: Boolean = true) {
    AlertDialog(onDismissRequest = onDismiss, containerColor = c.surface,
        title = { Text(title, style = MaterialTheme.typography.titleLarge) }, text = { Text(text, color = c.muted) },
        confirmButton = { TextButton({ onConfirm(); onDismiss() }) { Text(confirm, color = if (danger) c.danger else c.accent, fontWeight = FontWeight.SemiBold) } },
        dismissButton = { TextButton(onDismiss) { Text(T("Cancel"), color = c.muted) } })
}

@Composable
fun PromptDialog(title: String, initial: String = "", label: String = "", confirm: String = "Save", password: Boolean = false, onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    var v by remember { mutableStateOf(initial) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { try { focus.requestFocus() } catch (_: Exception) { } }
    AlertDialog(onDismissRequest = onDismiss, containerColor = c.surface,
        title = { Text(title, style = MaterialTheme.typography.titleLarge) },
        text = { Field(v, { v = it }, label, Modifier.focusRequester(focus), password = password, onDone = { if (v.isNotBlank()) { onConfirm(v); onDismiss() } }) },
        confirmButton = { TextButton({ if (v.isNotBlank()) { onConfirm(v); onDismiss() } }) { Text(T(confirm), color = c.accent, fontWeight = FontWeight.SemiBold) } },
        dismissButton = { TextButton(onDismiss) { Text(T("Cancel"), color = c.muted) } })
}

val app: AstroApp get() = AstroApp.instance

fun ago(ts: Long): String {
    val d = (System.currentTimeMillis() - ts) / 1000
    return when {
        d < 60 -> io.github.esoexe.astroai.core.tr("just now")
        d < 3600 -> io.github.esoexe.astroai.core.tr("# min ago", d / 60)
        d < 86400 -> io.github.esoexe.astroai.core.tr("# h ago", d / 3600)
        else -> io.github.esoexe.astroai.core.tr("# d ago", d / 86400)
    }
}

fun secs(ms: Long): String = if (ms < 10_000) "%.1fs".format(ms / 1000.0) else if (ms < 120_000) "${ms / 1000}s" else "${ms / 60_000}m ${(ms / 1000) % 60}s"
