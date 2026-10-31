package io.github.esoexe.astroai.voice

import android.content.Intent
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.esoexe.astroai.AstroApp
import io.github.esoexe.astroai.core.tr
import java.util.Locale

/**
 * Voice on the phone: Android's speech recognition for dictation and Live voice, and its text-to-speech voices
 * to read answers aloud. Recognition may use Google's online service depending on the phone; nothing is
 * recorded by A.S.T.R.O AI itself.
 */
class Speech(private val app: AstroApp) {
    private var tts: TextToSpeech? = null
    private var ttsReady = false
    private var pendingSpeak: String? = null

    var speaking by mutableStateOf(false)
        private set
    var speakingId by mutableStateOf<String?>(null)
        private set
    var listening by mutableStateOf(false)
        private set
    /** Microphone level 0..1 while listening (for the Live voice animation). */
    var level by mutableFloatStateOf(0f)
        private set

    private var onSpeechDone: (() -> Unit)? = null
    private var recognizer: SpeechRecognizer? = null

    private fun ensureTts(then: () -> Unit) {
        if (tts != null && ttsReady) { then(); return }
        if (tts == null) {
            tts = TextToSpeech(app) { status ->
                ttsReady = status == TextToSpeech.SUCCESS
                if (ttsReady) {
                    tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                        override fun onStart(id: String?) { app.ui.post { speaking = true } }
                        override fun onDone(id: String?) { if (id == "last") app.ui.post { speaking = false; speakingId = null; onSpeechDone?.invoke().also { onSpeechDone = null } } }
                        @Deprecated("Deprecated in Java")
                        override fun onError(id: String?) { app.ui.post { speaking = false; speakingId = null; onSpeechDone?.invoke().also { onSpeechDone = null } } }
                    })
                    pendingSpeak?.let { s -> pendingSpeak = null; app.ui.post { speak(s) } }
                } else app.ui.toast(tr("Text-to-speech is not available on this phone"), "error")
            }
        }
        if (!ttsReady) return
        then()
    }

    fun voices(): List<Voice> = try { tts?.voices?.filter { !it.isNetworkConnectionRequired || it.quality >= Voice.QUALITY_HIGH }?.sortedBy { it.locale.toLanguageTag() + it.name } ?: emptyList() } catch (_: Exception) { emptyList() }

    fun warmUp() = ensureTts { }

    /** Markdown to speakable text: no code blocks, links read by their text, no symbols. */
    fun speakable(md: String): String = md
        .replace(Regex("```[\\s\\S]*?```"), " ${tr("(code shown on screen)")} ")
        .replace(Regex("`([^`]+)`"), "$1")
        .replace(Regex("!?\\[([^\\]]*)]\\([^)]*\\)"), "$1")
        .replace(Regex("(?m)^#{1,6}\\s*"), "")
        .replace(Regex("(?m)^\\s*[-*+]\\s+"), "")
        .replace(Regex("[*_~>|]+"), "")
        .replace(Regex("https?://\\S+"), "")
        .replace(Regex("\\[(\\d+)]"), "")
        .replace(Regex("\\s+"), " ").trim()

    private fun localeFor(text: String): Locale = when {
        Regex("[\\u0600-\\u06FF]").containsMatchIn(text) -> Locale.forLanguageTag("ar")
        Regex("\\b(le|la|les|est|une|des|pour|avec|vous|nous|je)\\b", RegexOption.IGNORE_CASE).findAll(text.take(400)).count() >= 3 -> Locale.FRENCH
        else -> Locale.getDefault().takeIf { it.language == "en" } ?: Locale.US
    }

    fun speak(text: String, id: String? = null, onDone: (() -> Unit)? = null) {
        val clean = speakable(text)
        if (clean.isEmpty()) { onDone?.invoke(); return }
        if (tts == null || !ttsReady) { pendingSpeak = text; onSpeechDone = onDone; ensureTts { }; return }
        val t = tts ?: return
        stopSpeaking()
        onSpeechDone = onDone
        speakingId = id
        val chosen = app.prefs.voiceName
        val voice = if (chosen.isNotEmpty()) voices().firstOrNull { it.name == chosen } else null
        if (voice != null) t.voice = voice else t.language = localeFor(clean)
        t.setSpeechRate(app.prefs.voiceRate)
        t.setPitch(app.prefs.voicePitch)
        // Speak in sentence chunks (engines limit utterance length).
        val chunks = clean.split(Regex("(?<=[.!?؟])\\s+")).fold(mutableListOf<String>()) { acc, s -> if (acc.isNotEmpty() && acc.last().length + s.length < 380) acc[acc.size - 1] = acc.last() + " " + s else acc.add(s); acc }
        speaking = true
        chunks.forEachIndexed { i, c ->
            val params = Bundle()
            t.speak(c, if (i == 0) TextToSpeech.QUEUE_FLUSH else TextToSpeech.QUEUE_ADD, params, if (i == chunks.size - 1) "last" else "u$i")
        }
    }

    fun stopSpeaking() {
        onSpeechDone = null
        try { tts?.stop() } catch (_: Exception) { }
        speaking = false; speakingId = null
    }

    val recognitionAvailable: Boolean get() = SpeechRecognizer.isRecognitionAvailable(app)

    /**
     * Listens once. [onPartial] gets live text, [onFinal] the result (empty if nothing was heard).
     * The caller must have the microphone permission.
     */
    fun listen(language: String?, onPartial: (String) -> Unit, onFinal: (String) -> Unit, onError: (String) -> Unit) {
        stopListening()
        if (!recognitionAvailable) { onError(tr("Speech recognition is not available on this phone")); return }
        val r = SpeechRecognizer.createSpeechRecognizer(app)
        recognizer = r
        r.setRecognitionListener(object : RecognitionListener {
            override fun onReadyForSpeech(params: Bundle?) { listening = true }
            override fun onBeginningOfSpeech() {}
            override fun onRmsChanged(rmsdB: Float) { level = ((rmsdB + 2f) / 12f).coerceIn(0f, 1f) }
            override fun onBufferReceived(buffer: ByteArray?) {}
            override fun onEndOfSpeech() { level = 0f }
            override fun onError(error: Int) {
                listening = false; level = 0f
                if (error == SpeechRecognizer.ERROR_NO_MATCH || error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT) onFinal("")
                else onError(when (error) {
                    SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> tr("Voice input needs the microphone permission.")
                    SpeechRecognizer.ERROR_NETWORK, SpeechRecognizer.ERROR_NETWORK_TIMEOUT -> tr("Speech recognition needs the internet on this phone")
                    SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> tr("The microphone is busy")
                    else -> tr("Voice input stopped (#)", error)
                })
                destroy()
            }
            override fun onResults(results: Bundle?) {
                listening = false; level = 0f
                onFinal(results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull() ?: "")
                destroy()
            }
            override fun onPartialResults(partial: Bundle?) { partial?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.let(onPartial) }
            override fun onEvent(eventType: Int, params: Bundle?) {}
        })
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
            .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            .putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            .putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
        val tag = when (language) { "French" -> "fr-FR"; "Arabic" -> "ar"; "English" -> "en-US"; else -> null }
        if (tag != null) intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, tag)
        try { r.startListening(intent) } catch (e: Exception) { listening = false; onError(e.message ?: "") }
    }

    fun stopListening() {
        try { recognizer?.stopListening() } catch (_: Exception) { }
        destroy()
        listening = false; level = 0f
    }

    private fun destroy() { try { recognizer?.destroy() } catch (_: Exception) { }; recognizer = null }
}
