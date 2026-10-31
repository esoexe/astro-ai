package io.github.esoexe.astroai.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.esoexe.astroai.R

val Serif = FontFamily(
    Font(R.font.instrument_serif, FontWeight.Normal),
    Font(R.font.instrument_serif_italic, FontWeight.Normal, FontStyle.Italic),
)
val Sans = FontFamily(
    Font(R.font.inter_regular, FontWeight.Normal),
    Font(R.font.inter_semibold, FontWeight.SemiBold),
    Font(R.font.inter_semibold, FontWeight.Bold),
)
val Mono = FontFamily(Font(R.font.jetbrains_mono, FontWeight.Normal))

/** Colours the Material scheme has no slot for. */
@Immutable
data class AstroColors(
    val dark: Boolean,
    val bg: Color,
    val surface: Color,
    val surface2: Color,
    val border: Color,
    val text: Color,
    val muted: Color,
    val faint: Color,
    val accent: Color,
    val accentSoft: Color,
    val onAccent: Color,
    val ok: Color,
    val warn: Color,
    val danger: Color,
    val codeBg: Color,
    val userBubble: Color,
)

val Peach = Color(0xFFF3A679)

internal val DarkAstro = AstroColors(
    dark = true, bg = Color(0xFF0A0A0C), surface = Color(0xFF131316), surface2 = Color(0xFF1B1B1F),
    border = Color(0xFF2A2A30), text = Color(0xFFF3F4F3), muted = Color(0xFFA3A3AA), faint = Color(0xFF6B6B73),
    accent = Peach, accentSoft = Color(0x33F3A679), onAccent = Color(0xFF1A0F08), ok = Color(0xFF6FCF97),
    warn = Color(0xFFF2C94C), danger = Color(0xFFEB6F6F), codeBg = Color(0xFF101013), userBubble = Color(0xFF1E1C1B),
)
private val LightAstro = AstroColors(
    dark = false, bg = Color(0xFFFBFAF8), surface = Color(0xFFFFFFFF), surface2 = Color(0xFFF3F1EE),
    border = Color(0xFFE4E0DB), text = Color(0xFF16181A), muted = Color(0xFF5F6368), faint = Color(0xFF9AA0A6),
    accent = Color(0xFFD9814F), accentSoft = Color(0x26F3A679), onAccent = Color(0xFFFFFFFF), ok = Color(0xFF1E8E5A),
    warn = Color(0xFFB7791F), danger = Color(0xFFC94A4A), codeBg = Color(0xFFF6F4F1), userBubble = Color(0xFFF3ECE6),
)

val LocalAstro = staticCompositionLocalOf { DarkAstro }

private fun scheme(c: AstroColors): ColorScheme = if (c.dark) darkColorScheme(
    primary = c.accent, onPrimary = c.onAccent, primaryContainer = c.accentSoft, onPrimaryContainer = c.text,
    secondary = c.accent, onSecondary = c.onAccent, secondaryContainer = c.surface2, onSecondaryContainer = c.text,
    tertiary = c.accent, onTertiary = c.onAccent, tertiaryContainer = c.accentSoft, onTertiaryContainer = c.text,
    background = c.bg, onBackground = c.text,
    surface = c.surface, onSurface = c.text, surfaceVariant = c.surface2, onSurfaceVariant = c.muted,
    surfaceContainer = c.surface, surfaceContainerHigh = c.surface2, surfaceContainerHighest = c.surface2,
    surfaceContainerLow = c.surface, surfaceContainerLowest = c.bg,
    outline = c.border, outlineVariant = c.border, error = c.danger, onError = Color.White,
) else lightColorScheme(
    primary = c.accent, onPrimary = c.onAccent, primaryContainer = c.accentSoft, onPrimaryContainer = c.text,
    secondary = c.accent, onSecondary = c.onAccent, secondaryContainer = c.surface2, onSecondaryContainer = c.text,
    tertiary = c.accent, onTertiary = c.onAccent, tertiaryContainer = c.accentSoft, onTertiaryContainer = c.text,
    background = c.bg, onBackground = c.text,
    surface = c.surface, onSurface = c.text, surfaceVariant = c.surface2, onSurfaceVariant = c.muted,
    surfaceContainer = c.surface, surfaceContainerHigh = c.surface2, surfaceContainerHighest = c.surface2,
    surfaceContainerLow = c.surface, surfaceContainerLowest = c.bg,
    outline = c.border, outlineVariant = c.border, error = c.danger, onError = Color.White,
)

private fun typography(scale: Float): Typography {
    fun s(size: Int, line: Int, family: FontFamily = Sans, weight: FontWeight = FontWeight.Normal) =
        TextStyle(fontFamily = family, fontWeight = weight, fontSize = (size * scale).sp, lineHeight = (line * scale).sp)
    return Typography(
        displayLarge = s(44, 50, Serif), displayMedium = s(38, 44, Serif), displaySmall = s(32, 38, Serif),
        headlineLarge = s(30, 36, Serif), headlineMedium = s(26, 32, Serif), headlineSmall = s(22, 28, Serif),
        titleLarge = s(19, 26, Sans, FontWeight.SemiBold), titleMedium = s(16, 22, Sans, FontWeight.SemiBold),
        titleSmall = s(14, 20, Sans, FontWeight.SemiBold),
        bodyLarge = s(16, 24), bodyMedium = s(14, 21), bodySmall = s(12, 17),
        labelLarge = s(14, 20, Sans, FontWeight.SemiBold), labelMedium = s(12, 16, Sans, FontWeight.SemiBold),
        labelSmall = s(11, 14, Sans, FontWeight.SemiBold),
    )
}

@Composable
fun AstroTheme(theme: String, density: String, content: @Composable () -> Unit) {
    val dark = when (theme) { "light" -> false; "dark" -> true; else -> isSystemInDarkTheme() }
    val c = if (dark) DarkAstro else LightAstro
    val scale = when (density) { "compact" -> 0.93f; "large" -> 1.1f; else -> 1f }
    CompositionLocalProvider(LocalAstro provides c) {
        MaterialTheme(
            colorScheme = scheme(c),
            typography = typography(scale),
            shapes = Shapes(
                extraSmall = RoundedCornerShape(6.dp), small = RoundedCornerShape(10.dp), medium = RoundedCornerShape(14.dp),
                large = RoundedCornerShape(20.dp), extraLarge = RoundedCornerShape(28.dp),
            ),
            content = content,
        )
    }
}
