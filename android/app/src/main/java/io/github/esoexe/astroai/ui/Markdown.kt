package io.github.esoexe.astroai.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.esoexe.astroai.core.T

/** A small Markdown renderer: the subset the models actually write. */
sealed interface Block {
    data class Heading(val level: Int, val text: String) : Block
    data class Para(val text: String) : Block
    data class Code(val lang: String, val code: String, val closed: Boolean) : Block
    data class Quote(val text: String) : Block
    data class Items(val ordered: Boolean, val start: Int, val items: List<Pair<Int, String>>) : Block
    data class Table(val head: List<String>, val rows: List<List<String>>) : Block
    data object Rule : Block
}

object MarkdownParser {
    private val fence = Regex("^\\s*(```+|~~~+)\\s*([\\w+#.-]*).*$")
    private val heading = Regex("^(#{1,6})\\s+(.*?)\\s*#*\\s*$")
    private val bullet = Regex("^(\\s*)([-*+]|\\d{1,3}[.)])\\s+(.*)$")
    private val rule = Regex("^\\s*([-*_])(\\s*\\1){2,}\\s*$")
    private val tableSep = Regex("^\\s*\\|?\\s*:?-{2,}:?\\s*(\\|\\s*:?-{2,}:?\\s*)*\\|?\\s*$")

    private fun cells(line: String) = line.trim().removePrefix("|").removeSuffix("|").split(Regex("(?<!\\\\)\\|")).map { it.trim().replace("\\|", "|") }

    fun parse(src: String): List<Block> {
        val out = ArrayList<Block>()
        val lines = src.replace("\r\n", "\n").split('\n')
        var i = 0
        val para = StringBuilder()
        fun flush() { if (para.isNotBlank()) out += Block.Para(para.toString().trim()); para.clear() }
        while (i < lines.size) {
            val line = lines[i]
            val f = fence.matchEntire(line)
            if (f != null) {
                flush()
                val mark = f.groupValues[1]; val lang = f.groupValues[2].lowercase()
                val code = StringBuilder(); i++
                var closed = false
                while (i < lines.size) {
                    if (lines[i].trim().startsWith(mark) && lines[i].trim().trimStart(mark[0]).isBlank()) { closed = true; i++; break }
                    code.append(lines[i]).append('\n'); i++
                }
                out += Block.Code(lang, code.toString().trimEnd('\n'), closed); continue
            }
            if (line.isBlank()) { flush(); i++; continue }
            val h = heading.matchEntire(line)
            if (h != null) { flush(); out += Block.Heading(h.groupValues[1].length, h.groupValues[2]); i++; continue }
            if (rule.matches(line)) { flush(); out += Block.Rule; i++; continue }
            if (line.trimStart().startsWith(">")) {
                flush(); val q = StringBuilder()
                while (i < lines.size && lines[i].trimStart().startsWith(">")) { q.append(lines[i].trimStart().removePrefix(">").removePrefix(" ")).append('\n'); i++ }
                out += Block.Quote(q.toString().trim()); continue
            }
            if (line.contains('|') && i + 1 < lines.size && tableSep.matches(lines[i + 1])) {
                flush(); val head = cells(line); i += 2
                val rows = ArrayList<List<String>>()
                while (i < lines.size && lines[i].contains('|') && lines[i].isNotBlank()) { rows += cells(lines[i]); i++ }
                out += Block.Table(head, rows); continue
            }
            val b = bullet.matchEntire(line)
            if (b != null) {
                flush()
                val ordered = b.groupValues[2].first().isDigit()
                val start = if (ordered) b.groupValues[2].dropLast(1).toIntOrNull() ?: 1 else 1
                val items = ArrayList<Pair<Int, String>>()
                while (i < lines.size) {
                    val m = bullet.matchEntire(lines[i])
                    if (m != null) { items += (m.groupValues[1].replace("\t", "    ").length / 2).coerceAtMost(4) to m.groupValues[3]; i++; continue }
                    if (lines[i].isNotBlank() && lines[i].startsWith("  ") && items.isNotEmpty()) { val (d, t) = items.last(); items[items.size - 1] = d to (t + " " + lines[i].trim()); i++; continue }
                    break
                }
                out += Block.Items(ordered, start, items); continue
            }
            para.append(line).append('\n'); i++
        }
        flush()
        return out
    }
}

@Immutable
class MdStyle(val text: Color, val muted: Color, val accent: Color, val codeBg: Color, val border: Color)

private val inlineRe = Regex(
    "(`+)(.+?)\\1" +                                   // 1,2 code
        "|\\*\\*(.+?)\\*\\*|__(.+?)__" +              // 3,4 bold
        "|~~(.+?)~~" +                                 // 5 strike
        "|(?<![\\w*])\\*(?!\\s)(.+?)(?<!\\s)\\*(?![\\w*])|(?<![\\w_])_(?!\\s)(.+?)(?<!\\s)_(?![\\w_])" + // 6,7 italic
        "|!?\\[([^\\]]+)]\\((\\S+?)(?:\\s+\"[^\"]*\")?\\)" + // 8,9 link
        "|(https?://[^\\s<>()\\[\\]]+[^\\s<>()\\[\\].,;:!?'\"])" // 10 bare url
)

fun inline(src: String, s: MdStyle): AnnotatedString = buildAnnotatedString { appendInline(src, s) }

private fun AnnotatedString.Builder.appendInline(src: String, s: MdStyle) {
    var last = 0
    for (m in inlineRe.findAll(src)) {
        if (m.range.first < last) continue
        append(src.substring(last, m.range.first).replace("\\*", "*").replace("\\_", "_"))
        val g = m.groupValues
        when {
            g[2].isNotEmpty() -> withStyle(SpanStyle(fontFamily = Mono, fontSize = 13.5.sp, background = s.codeBg, color = s.accent)) { append(" ${g[2].trim()} ") }
            g[3].isNotEmpty() || g[4].isNotEmpty() -> withStyle(SpanStyle(fontWeight = FontWeight.SemiBold)) { appendInline(g[3].ifEmpty { g[4] }, s) }
            g[5].isNotEmpty() -> withStyle(SpanStyle(textDecoration = TextDecoration.LineThrough)) { appendInline(g[5], s) }
            g[6].isNotEmpty() || g[7].isNotEmpty() -> withStyle(SpanStyle(fontStyle = FontStyle.Italic)) { appendInline(g[6].ifEmpty { g[7] }, s) }
            g[8].isNotEmpty() -> link(g[9], s) { appendInline(g[8], s) }
            g[10].isNotEmpty() -> link(g[10], s) { append(g[10].removePrefix("https://").removePrefix("http://").let { if (it.length > 48) it.take(45) + "…" else it }) }
        }
        last = m.range.last + 1
    }
    if (last < src.length) append(src.substring(last).replace("\\*", "*").replace("\\_", "_"))
}

private inline fun AnnotatedString.Builder.link(url: String, s: MdStyle, body: AnnotatedString.Builder.() -> Unit) {
    if (!url.startsWith("http://") && !url.startsWith("https://") && !url.startsWith("mailto:")) { body(); return }
    withLink(LinkAnnotation.Url(url, TextLinkStyles(SpanStyle(color = s.accent, textDecoration = TextDecoration.Underline)))) { body() }
}

@Composable
fun Markdown(text: String, modifier: Modifier = Modifier, streaming: Boolean = false, onOpenCode: ((String, String) -> Unit)? = null) {
    val colors = c
    val s = remember(colors) { MdStyle(colors.text, colors.muted, colors.accent, colors.codeBg, colors.border) }
    val blocks = remember(text) { MarkdownParser.parse(text) }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        blocks.forEachIndexed { idx, b -> BlockView(b, s, streaming && idx == blocks.lastIndex, onOpenCode) }
    }
}

@Composable
private fun BlockView(b: Block, s: MdStyle, live: Boolean, onOpenCode: ((String, String) -> Unit)?) {
    val body = MaterialTheme.typography.bodyLarge.copy(color = s.text, lineHeight = 24.sp)
    when (b) {
        is Block.Heading -> Text(inline(b.text, s), Modifier.padding(top = if (b.level <= 2) 6.dp else 2.dp),
            style = when (b.level) { 1 -> MaterialTheme.typography.headlineSmall; 2 -> MaterialTheme.typography.titleLarge; else -> MaterialTheme.typography.titleMedium }.copy(color = s.text))
        is Block.Para -> Text(inline(b.text, s), style = body)
        is Block.Quote -> Row(Modifier.fillMaxWidth().height(IntrinsicSize.Min)) {
            Box(Modifier.width(3.dp).fillMaxHeight().clip(RoundedCornerShape(2.dp)).background(s.accent.copy(alpha = 0.5f)))
            Markdown(b.text, Modifier.padding(start = 12.dp))
        }
        is Block.Items -> Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            var n = b.start
            for ((depth, t) in b.items) {
                val task = Regex("^\\[([ xX])]\\s+(.*)").matchEntire(t)
                Row(Modifier.padding(start = (depth * 16).dp)) {
                    val mark = when {
                        task != null -> if (task.groupValues[1].isBlank()) "☐" else "☑"
                        b.ordered && depth == 0 -> "${n++}."
                        else -> if (depth == 0) "•" else "◦"
                    }
                    Text(mark, Modifier.widthIn(min = 22.dp), style = body.copy(color = if (b.ordered) s.muted else s.accent))
                    Text(inline(task?.groupValues?.get(2) ?: t, s), style = body)
                }
            }
        }
        is Block.Code -> CodeBlock(b.lang, b.code, live && !b.closed, onOpenCode)
        is Block.Table -> Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).border(1.dp, s.border, RoundedCornerShape(10.dp)).horizontalScroll(rememberScrollState())) {
            Column {
                val cols = maxOf(b.head.size, b.rows.maxOfOrNull { it.size } ?: 0)
                val all = listOf(b.head) + b.rows
                all.forEachIndexed { r, row ->
                    Row(Modifier.background(if (r == 0) s.codeBg else Color.Transparent)) {
                        for (ci in 0 until cols) {
                            Text(inline(row.getOrElse(ci) { "" }, s), Modifier.widthIn(min = 90.dp, max = 260.dp).padding(horizontal = 10.dp, vertical = 7.dp),
                                style = MaterialTheme.typography.bodyMedium.copy(color = s.text, fontWeight = if (r == 0) FontWeight.SemiBold else FontWeight.Normal))
                        }
                    }
                    if (r < all.lastIndex) HorizontalDivider(color = s.border)
                }
            }
        }
        Block.Rule -> HorizontalDivider(Modifier.padding(vertical = 6.dp), color = s.border)
    }
}

@Composable
fun CodeBlock(lang: String, code: String, live: Boolean = false, onOpenCode: ((String, String) -> Unit)? = null) {
    val colors = c
    val hl = remember(code, lang, colors.dark) { Highlighter.highlight(code, lang, colors.dark) }
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(colors.codeBg).border(1.dp, colors.border, RoundedCornerShape(12.dp))) {
        Row(Modifier.fillMaxWidth().padding(start = 12.dp, end = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(lang.ifEmpty { "text" }, style = MaterialTheme.typography.labelSmall, color = colors.faint, modifier = Modifier.weight(1f))
            if (live) { ThinkingDots(colors.faint); Spacer(Modifier.width(8.dp)) }
            if (onOpenCode != null && !live) IconBtn(AppIcons.code, T("Open in Workspace"), { onOpenCode(lang, code) }, size = 16.dp)
            IconBtn(AppIcons.copy, T("Copy code"), { app.ui.copy(code); app.ui.toast(T_("Copied")) }, size = 16.dp)
        }
        Box(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(start = 12.dp, end = 12.dp, bottom = 12.dp)) {
            Text(hl, style = MaterialTheme.typography.bodyMedium.copy(fontFamily = Mono, fontSize = 13.sp, lineHeight = 19.sp, color = colors.text), softWrap = false)
        }
    }
}

private fun T_(s: String) = io.github.esoexe.astroai.core.tr(s)

/** Regex highlighter: keywords, strings, comments, numbers, for the common languages. */
object Highlighter {
    private val common = setOf("if", "else", "for", "while", "return", "break", "continue", "switch", "case", "default", "try", "catch", "finally", "throw", "new", "class", "import", "from", "as", "in", "do")
    private val kw = mapOf(
        "py" to setOf("def", "lambda", "elif", "pass", "with", "yield", "async", "await", "not", "and", "or", "is", "None", "True", "False", "self", "global", "nonlocal", "raise", "except", "del", "assert"),
        "js" to setOf("function", "const", "let", "var", "async", "await", "yield", "export", "typeof", "instanceof", "null", "undefined", "true", "false", "this", "of", "extends", "super", "static", "interface", "type", "enum", "implements", "public", "private", "readonly"),
        "c" to setOf("int", "char", "float", "double", "void", "long", "short", "unsigned", "signed", "const", "static", "struct", "union", "enum", "typedef", "sizeof", "auto", "bool", "true", "false", "nullptr", "template", "typename", "namespace", "using", "public", "private", "protected", "virtual", "override", "std", "include", "define", "constexpr", "inline", "extern"),
        "java" to setOf("public", "private", "protected", "static", "final", "void", "int", "long", "boolean", "double", "float", "char", "byte", "extends", "implements", "interface", "package", "null", "true", "false", "this", "super", "fun", "val", "var", "object", "when", "is", "data", "sealed", "override", "suspend", "companion", "lateinit"),
        "go" to setOf("func", "package", "var", "const", "type", "struct", "interface", "map", "chan", "go", "defer", "select", "range", "nil", "true", "false", "fallthrough", "goto"),
        "rs" to setOf("fn", "let", "mut", "pub", "struct", "enum", "impl", "trait", "use", "mod", "match", "loop", "where", "self", "Self", "crate", "super", "ref", "move", "dyn", "true", "false", "Some", "None", "Ok", "Err", "async", "await", "unsafe"),
        "sh" to setOf("then", "fi", "elif", "esac", "done", "echo", "export", "local", "function", "set", "unset", "source", "cd", "exit"),
        "sql" to setOf("select", "insert", "update", "delete", "where", "join", "left", "right", "inner", "outer", "on", "group", "by", "order", "having", "limit", "create", "table", "values", "into", "and", "or", "not", "null", "primary", "key", "drop", "alter", "index", "distinct", "count", "set"),
    )
    private fun family(lang: String) = when (lang) {
        "py", "python", "python3" -> "py"
        "js", "javascript", "ts", "typescript", "jsx", "tsx", "json", "mjs" -> "js"
        "c", "cpp", "c++", "h", "hpp", "cc", "cs", "csharp", "objc" -> "c"
        "java", "kotlin", "kt", "scala", "swift", "dart" -> "java"
        "go", "golang" -> "go"
        "rust", "rs" -> "rs"
        "sh", "bash", "shell", "zsh", "powershell", "ps1", "console" -> "sh"
        "sql" -> "sql"
        else -> ""
    }
    private val token = Regex("(#[^\\n]*|//[^\\n]*|/\\*[\\s\\S]*?\\*/|--[^\\n]*)|(\"(?:\\\\.|[^\"\\\\\\n])*\"|'(?:\\\\.|[^'\\\\\\n])*'|`(?:\\\\.|[^`\\\\])*`)|\\b(\\d[\\d_]*(?:\\.\\d+)?(?:e[+-]?\\d+)?|0x[0-9a-fA-F]+)\\b|\\b([A-Za-z_]\\w*)\\b(\\s*\\()?")

    fun highlight(code: String, lang: String, dark: Boolean): AnnotatedString {
        val fam = family(lang)
        if (fam.isEmpty() && lang !in setOf("", "html", "xml", "css", "yaml", "yml", "toml", "ini")) return AnnotatedString(code)
        val words = (kw[fam] ?: emptySet()) + common
        val comment = SpanStyle(color = if (dark) Color(0xFF7D8590) else Color(0xFF8A8F98), fontStyle = FontStyle.Italic)
        val str = SpanStyle(color = if (dark) Color(0xFFA5D6A7) else Color(0xFF2E7D32))
        val num = SpanStyle(color = if (dark) Color(0xFFF7C59F) else Color(0xFFB0532A))
        val key = SpanStyle(color = if (dark) Color(0xFFF3A679) else Color(0xFFC2410C), fontWeight = FontWeight.SemiBold)
        val call = SpanStyle(color = if (dark) Color(0xFF9CC9FF) else Color(0xFF1D4ED8))
        val hashComments = fam in setOf("py", "sh", "") || lang in setOf("yaml", "yml", "toml", "ini")
        val dashComments = fam == "sql"
        return buildAnnotatedString {
            var last = 0
            for (m in token.findAll(code)) {
                append(code.substring(last, m.range.first))
                val g = m.groupValues
                when {
                    g[1].isNotEmpty() -> {
                        val ok = (g[1].startsWith("#") && hashComments) || (g[1].startsWith("--") && dashComments) || g[1].startsWith("//") || g[1].startsWith("/*")
                        if (ok && !(g[1].startsWith("//") && fam == "py")) withStyle(comment) { append(g[1]) } else append(g[1])
                    }
                    g[2].isNotEmpty() -> withStyle(str) { append(g[2]) }
                    g[3].isNotEmpty() -> withStyle(num) { append(g[3]) }
                    g[4].isNotEmpty() -> {
                        val w = g[4]
                        when {
                            w in words || (fam == "sql" && w.lowercase() in words) -> withStyle(key) { append(w) }
                            g[5].isNotEmpty() -> withStyle(call) { append(w) }
                            else -> append(w)
                        }
                        append(g[5])
                    }
                    else -> append(m.value)
                }
                last = m.range.last + 1
            }
            if (last < code.length) append(code.substring(last))
        }
    }
}
