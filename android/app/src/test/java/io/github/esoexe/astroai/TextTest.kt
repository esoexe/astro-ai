package io.github.esoexe.astroai

import io.github.esoexe.astroai.brain.Text
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The text rules must behave like the web app's (web/app.js), or answers would differ between phone and computer. */
class TextTest {
    @Test fun codeRequests() {
        assertTrue(Text.isCodeRequest("Fix this Python function please"))
        assertTrue(Text.isCodeRequest("why does main.rs not compile"))
        assertTrue(Text.isCodeRequest("TypeError: x is undefined"))
        assertTrue(Text.isCodeRequest("اكتب دالة تحسب المتوسط"))
        assertTrue(Text.isCodeRequest("écris une fonction de tri"))
        assertFalse(Text.isCodeRequest("What is the capital of Algeria?"))
        assertFalse(Text.isCodeRequest("Plan my week with three workouts"))
    }

    @Test fun linksAreTrimmedAndLimited() {
        val l = Text.links("See https://a.example/x, and https://b.example/y. Also https://a.example/x then https://c.example https://d.example")
        assertEquals(listOf("https://a.example/x", "https://b.example/y", "https://c.example"), l)
        assertEquals("example.com", Text.hostOf("https://www.example.com/page"))
    }

    @Test fun actionsBlockIsRemovedAndParsed() {
        val answer = "Done, I added it.\n\n```actions\n[{\"tool\":\"create_task\",\"title\":\"Buy milk\"},{\"tool\":\"remember\",\"text\":\"likes tea\"},]\n```\nAnything else?"
        val x = Text.extractActions(answer)
        assertEquals("Done, I added it.\n\n\nAnything else?", x.clean)   // byte-for-byte what web/app.js returns; Markdown collapses the blank lines
        assertEquals(listOf("create_task", "remember"), x.actions.map { it.getString("tool") })
    }

    @Test fun jsonFenceOnlyCountsWithKnownTools() {
        val known = Text.extractActions("Ok\n```json\n{\"tool\":\"set_theme\",\"theme\":\"dark\"}\n```")
        assertEquals(1, known.actions.size)
        val code = "Example:\n```json\n{\"tool\":\"hammer\",\"size\":3}\n```"
        assertEquals(code, Text.extractActions(code).clean)
        assertTrue(Text.extractActions(code).actions.isEmpty())
    }

    @Test fun partialFenceIsHiddenWhileStreaming() {
        assertEquals("Sure", Text.extractActions("Sure\n```act", streaming = true).clean)
        assertEquals("Sure\n```act", Text.extractActions("Sure\n```act", streaming = false).clean)
        assertEquals("Look:\n```py", Text.extractActions("Look:\n```py", streaming = true).clean)   // a code fence is not an actions fence
    }

    @Test fun confidenceTag() {
        assertEquals("high", Text.confidenceOf("Answer.\n[[confidence: HIGH]]"))
        assertEquals("Answer.", Text.stripConfidence("Answer.\n[[confidence: high]]"))
        assertEquals("Answer.", Text.stripConfidence("Answer.\n[[confid"))   // half-streamed tag never flashes
        assertEquals("", Text.confidenceOf("No tag here"))
    }

    @Test fun reasoningLeakIsRemoved() {
        val leaked = "<|channel|>analysis<|message|>secret thoughts<|end|><|start|>assistant<|channel|>final<|message|>The answer is 4."
        assertEquals("The answer is 4.", Text.stripReasoningLeak(leaked))
        assertEquals("plain", Text.stripReasoningLeak("plain"))
    }

    @Test fun codeBlocks() {
        val b = Text.codeBlocks("a\n```python\nprint(1)\n```\nb\n```\nx\n```")
        assertEquals(listOf("python" to "print(1)\n", "" to "x\n"), b)
    }
}
