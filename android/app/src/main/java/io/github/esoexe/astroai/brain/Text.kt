package io.github.esoexe.astroai.brain

import org.json.JSONArray
import org.json.JSONObject

/** Text rules shared with the web app (web/app.js), so answers behave the same on the phone and the computer. */
object Text {
    // Is this message about code? Then the server adds the coding standard, and the team gets coding roles.
    private val CODE_WORDS = Regex("\\b(code|coding|function|method|class|script|program|bug|debug|error|exception|traceback|stack ?trace|compile[rsd]?|syntax|refactor|implement|algorithm|regex|regexp|api|endpoint|sql|query|database|schema|json|yaml|unit tests?|npm|pip|cargo|gradle|docker|git|python|javascript|typescript|node(?:\\.js)?|react|vue|svelte|angular|html|css|tailwind|java|kotlin|swift|rust|golang|c\\+\\+|c#|php|ruby|bash|shell|powershell|lua|dart|flutter|django|flask|fastapi|express|next\\.js|leetcode)\\b", RegexOption.IGNORE_CASE)
    private val CODE_SIGNS = Regex("```|\\b\\w+\\.(?:py|js|mjs|ts|tsx|jsx|c|cc|cpp|h|hpp|cs|java|go|rs|rb|php|sh|sql|html|css|json|yml|yaml|toml|kt|swift)\\b|=>|\\bdef \\w+\\(|\\bfunction\\s*\\w*\\(|#include|console\\.log|System\\.out|\\bimport \\w+|\\w+Error\\b")
    private val ARABIC_FRENCH_CODE = Regex("برمج|كود|شيفرة|دالة|خطأ برمجي|programm|fonction|déboguer|bogue", RegexOption.IGNORE_CASE)

    fun isCodeRequest(text: String): Boolean {
        val t = if (text.length > 6000) text.substring(0, 6000) else text
        return CODE_SIGNS.containsMatchIn(t) || CODE_WORDS.containsMatchIn(t) || ARABIC_FRENCH_CODE.containsMatchIn(t)
    }

    val URL_RE = Regex("\\bhttps?://[^\\s<>\"'`)\\]]+", RegexOption.IGNORE_CASE)

    fun links(text: String): List<String> = URL_RE.findAll(text).map { it.value.trimEnd('.', ',', ';', ':', '!', '?') }.distinct().take(3).toList()

    fun hostOf(u: String): String = try { java.net.URI(u).host?.removePrefix("www.") ?: u } catch (_: Exception) { u }

    private val CONF_RE = Regex("\\[\\[\\s*confidence\\s*:\\s*(high|medium|low)\\s*]]\\s*$", RegexOption.IGNORE_CASE)
    private val CONF_STRIP = Regex("\\n*\\[\\[\\s*(c(o(n(f(i(d(e(n(c(e(\\s*:[^\\]\\n]{0,12})?)?)?)?)?)?)?)?)?)?)?\\s*]?]?\\s*$", RegexOption.IGNORE_CASE)

    fun stripConfidence(t: String): String = t.replace(CONF_STRIP, "")
    fun confidenceOf(t: String): String = CONF_RE.find(t)?.groupValues?.get(1)?.lowercase() ?: ""

    fun stripReasoningLeak(text: String): String {
        var t = text
        if (t.contains("<|channel|>") || t.contains("<|message|>")) {
            val marker = "<|channel|>final<|message|>"
            val fin = t.lastIndexOf(marker)
            if (fin >= 0) t = t.substring(fin + marker.length)
            t = t.replace(Regex("<\\|(start|end|return|call|channel|message|constrain)\\|>[^<\\n]*?(?=<\\||\\n|$)"), "").replace(Regex("<\\|[a-z_]+\\|>"), "")
        }
        return t
    }

    // ---------------- actions the assistant can take ("```actions" blocks) ----------------
    val TOOL_NAMES = listOf("create_note", "append_note", "delete_note", "create_task", "complete_task", "delete_task", "remember", "forget", "set_reminder",
        "set_theme", "set_mode", "set_language", "open", "new_chat", "export_chat", "open_url", "speak", "calculate", "search", "read_note", "list_tasks",
        "web_search", "read_url", "create_file", "open_reader", "list_notes")

    fun parseActionJson(raw: String): List<JSONObject> {
        val text = raw.trim()
        if (text.isEmpty()) return emptyList()
        val variants = listOf(text, text.replace(Regex(",\\s*([\\]}])"), "$1"), text.replace('\'', '"').replace(Regex(",\\s*([\\]}])"), "$1"))
        for (t in variants) {
            try {
                val list = mutableListOf<JSONObject>()
                when {
                    t.startsWith("[") -> { val a = JSONArray(t); for (i in 0 until a.length()) a.optJSONObject(i)?.let { list.add(it) } }
                    else -> { val o = JSONObject(t); val acts = o.optJSONArray("actions"); if (acts != null) for (i in 0 until acts.length()) acts.optJSONObject(i)?.let { list.add(it) } else list.add(o) }
                }
                return list.filter { it.opt("tool") is String }.take(8)
            } catch (_: Exception) { }
        }
        val out = mutableListOf<JSONObject>()
        for (line in text.split('\n')) try { val v = JSONObject(line.trim().removeSuffix(",")); if (v.opt("tool") is String) out.add(v) } catch (_: Exception) { }
        return out.take(8)
    }

    data class Extracted(val clean: String, val actions: List<JSONObject>)

    private val ACTIONS_FENCE = Regex("```[ \\t]*actions\\b", RegexOption.IGNORE_CASE)
    private val JSON_TOOL_FENCE = Regex("```[ \\t]*json[ \\t]*\\n\\s*\\[?\\s*\\{\\s*\"tool\"\\s*:", RegexOption.IGNORE_CASE)
    private val PARTIAL_TAIL = Regex("\\n?`{1,3}[a-z]{0,7}$", RegexOption.IGNORE_CASE)
    private val PARTIAL_ACTIONS = Regex("^\\n?`{1,3}(a(c(t(i(o(n(s)?)?)?)?)?)?)?$", RegexOption.IGNORE_CASE)

    fun extractActions(content: String, streaming: Boolean = false): Extracted {
        var idx = ACTIONS_FENCE.find(content)?.range?.first ?: -1
        var fence = Regex("```[ \\t]*actions[^\\n]*\\n?([\\s\\S]*?)(?:```|$)", RegexOption.IGNORE_CASE)
        if (idx < 0) {
            val j = JSON_TOOL_FENCE.find(content)?.range?.first ?: -1
            if (j >= 0) {
                val probe = Regex("\"tool\"\\s*:\\s*\"([a-z_]+)\"", RegexOption.IGNORE_CASE).find(content.substring(j))
                if (probe != null && probe.groupValues[1].lowercase() in TOOL_NAMES) { idx = j; fence = Regex("```[ \\t]*json[^\\n]*\\n?([\\s\\S]*?)(?:```|$)", RegexOption.IGNORE_CASE) }
            }
        }
        if (idx < 0) {
            if (streaming) {
                val tail = PARTIAL_TAIL.find(content)
                if (tail != null && PARTIAL_ACTIONS.matches(tail.value)) return Extracted(content.substring(0, tail.range.first).trimEnd(), emptyList())
            }
            return Extracted(content, emptyList())
        }
        val block = content.substring(idx)
        val m = fence.find(block)
        val actions = parseActionJson(m?.groupValues?.get(1) ?: "")
        val after = if (m != null) block.substring(m.range.last + 1) else ""
        return Extracted((content.substring(0, idx) + after).trim(), actions)
    }

    /** Code blocks in an answer: (language tag, code). */
    fun codeBlocks(text: String): List<Pair<String, String>> =
        Regex("```([\\w+#.-]*)[^\\n]*\\n([\\s\\S]*?)```").findAll(text).map { it.groupValues[1] to it.groupValues[2] }.toList()
}
