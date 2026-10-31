package io.github.esoexe.astroai

import io.github.esoexe.astroai.core.bool
import io.github.esoexe.astroai.core.clip
import io.github.esoexe.astroai.core.int
import io.github.esoexe.astroai.core.jsonOf
import io.github.esoexe.astroai.core.str
import io.github.esoexe.astroai.core.strings
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class JsonTest {
    @Test fun lenientReads() {
        val j = JSONObject("""{"a":"x","n":"12","b":1,"z":null,"f":"true","nan":"abc"}""")
        assertEquals("x", j.str("a"))
        assertEquals("", j.str("z"))
        assertEquals("", j.str("missing"))
        assertEquals(12, j.int("n"))
        assertEquals(7, j.int("nan", 7))
        assertTrue(j.bool("b"))
        assertTrue(j.bool("f"))
        assertFalse(j.bool("missing"))
        assertEquals("ab", JSONObject("""{"s":"abcdef"}""").str("s", 2))
    }

    @Test fun buildAndStrings() {
        val j = jsonOf("name" to "A", "list" to listOf("x", "y"), "nested" to mapOf("k" to 1), "none" to null)
        assertEquals("A", j.getString("name"))
        assertEquals(2, j.getJSONArray("list").length())
        assertEquals(1, j.getJSONObject("nested").getInt("k"))
        assertEquals(listOf("a", "1"), JSONArray("""["a",1,null]""").strings())
        assertEquals("abc", clip("abcdef", 3))
    }
}
