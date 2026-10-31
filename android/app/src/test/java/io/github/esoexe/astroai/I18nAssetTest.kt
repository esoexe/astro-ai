package io.github.esoexe.astroai

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** assets/i18n.json is generated from the web app's dictionary (JS regexes): every pattern must also work in Java. */
class I18nAssetTest {
    private val asset: JSONObject by lazy {
        val f = listOf("src/main/assets/i18n.json", "app/src/main/assets/i18n.json").map(::File).first { it.exists() }
        JSONObject(f.readText())
    }

    @Test fun everyPatternCompilesInJava() {
        val p = asset.getJSONArray("p")
        assertTrue(p.length() > 0)
        for (i in 0 until p.length()) {
            val a = p.getJSONArray(i)
            val re = try { Regex(a.getString(0)) } catch (e: Exception) { throw AssertionError("pattern $i does not compile in Java: ${a.getString(0)}", e) }
            assertTrue(re.pattern.isNotEmpty())
            assertTrue("pattern $i needs two translations", a.getString(1).isNotEmpty() && a.getString(2).isNotEmpty())
        }
    }

    @Test fun placeholdersSurviveTranslation() {
        val d = asset.getJSONObject("d")
        val bad = mutableListOf<String>()
        for (k in d.keys()) {
            val v = d.getJSONArray(k)
            val n = k.count { it == '#' }
            if (v.getString(0).isEmpty() || v.getString(1).isEmpty()) bad += "empty: $k"
            // only native strings use # as a placeholder; check those where the English has one
            if (n > 0 && (v.getString(0).count { it == '#' } != n || v.getString(1).count { it == '#' } != n)) bad += "placeholders: $k"
        }
        // "C#" and "#include" are literal text, not placeholders
        assertEquals(emptyList<String>(), bad.filterNot { it.contains("C#") || it.contains("#include") })
    }
}
