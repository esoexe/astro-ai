package io.github.esoexe.astroai.core

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONObject

/**
 * Interface languages: English, French, Arabic (right to left). Strings are looked up by their English text in
 * assets/i18n.json, which tools/android_i18n.js builds from the web app's dictionary plus native-only strings,
 * so the phone and the computer use the same wording. "#" in a string is replaced by the arguments, in order.
 */
object I18n {
    val LANGS = linkedMapOf("en" to "English", "fr" to "Français", "ar" to "العربية")   // i18n-ignore: native language names

    var lang by mutableStateOf("en")
        private set

    private var dict: Map<String, Array<String>> = emptyMap()
    private var pats: List<Triple<Regex, String, String>> = emptyList()
    private val space = Regex("\\s+")
    private val numRe = Regex("\\d+(?:[.,]\\d+)*")

    fun load(ctx: Context) {
        try {
            val j = JSONObject(ctx.assets.open("i18n.json").bufferedReader().use { it.readText() })
            val d = j.getJSONObject("d")
            val m = HashMap<String, Array<String>>(d.length() * 2)
            for (k in d.keys()) { val a = d.getJSONArray(k); m[k] = arrayOf(a.getString(0), a.getString(1)) }
            dict = m
            val p = j.getJSONArray("p")
            pats = (0 until p.length()).mapNotNull { i ->
                val a = p.getJSONArray(i)
                runCatching { Triple(Regex(a.getString(0)), a.getString(1), a.getString(2)) }.getOrNull()
            }
        } catch (_: Exception) { /* English only */ }
    }

    fun setLanguage(l: String) { lang = if (LANGS.containsKey(l)) l else "en" }
    val rtl: Boolean get() = lang == "ar"

    private fun fill(s: String, args: Array<out Any?>): String {
        if (args.isEmpty() || !s.contains('#')) return s
        val sb = StringBuilder(); var i = 0
        for (ch in s) { if (ch == '#' && i < args.size) sb.append(args[i++].toString()) else sb.append(ch) }
        return sb.toString()
    }

    fun tr(en: String, vararg args: Any?): String {
        if (lang == "en" || en.isEmpty()) return fill(en, args)
        val idx = if (lang == "fr") 0 else 1
        val key = en.replace(space, " ").trim()
        dict[key]?.let { return fill(it[idx], args) }
        val full = fill(en, args)
        for ((re, fr, ar) in pats) if (re.containsMatchIn(full)) return runCatching { full.replace(re, if (idx == 0) fr else ar) }.getOrDefault(full)
        // "Run on 3 model(s)" matches the key "Run on # model(s)"; the numbers go back in order.
        val nums = numRe.findAll(full).map { it.value }.toList()
        if (nums.isNotEmpty()) dict[full.replace(space, " ").trim().replace(numRe, "#")]?.let { return fill(it[idx], nums.toTypedArray()) }
        return full
    }
}

/** Translated UI text; recomposes when the interface language changes. */
@Composable
fun T(en: String, vararg args: Any?): String {
    @Suppress("UNUSED_VARIABLE") val l = I18n.lang
    return I18n.tr(en, *args)
}

/** Translated text outside Compose (toasts, notifications). */
fun tr(en: String, vararg args: Any?): String = I18n.tr(en, *args)
