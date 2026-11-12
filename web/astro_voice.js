// =============================================================================================
// A.S.T.R.O AI Neural Voice & Telemetry Desk (Edge Neural TTS, Sentence Streaming & Speech-to-Text)
// Integrated from proven E.D.I.T.H speech architecture.
// =============================================================================================
(function () {
  'use strict';

  // Cleans AI response text so speech engines sound crisp and natural.
  function cleanForSpeech(t) {
    return String(t || '')
      .replace(/```[\s\S]*?```/g, ' code block omitted. ')
      .replace(/`([^`]*)`/g, '$1')
      .replace(/https?:\/\/\S+/g, 'link')
      .replace(/\[(.*?)\]\(.*?\)/g, '$1')
      .replace(/<\/?[a-z][^>]*>/gi, ' ')
      .replace(/^\s*[-*•]\s+/gm, '')
      .replace(/[*_#>|~]+/g, ' ')
      .replace(/[\u{1F300}-\u{1FAFF}\u{2600}-\u{27BF}]/gu, '')
      .replace(/\s&\s/g, ' and ')
      .replace(/\s+/g, ' ')
      .trim();
  }

  // Splits streaming tokens into speakable chunks as they arrive.
  class SentenceSplitter {
    constructor(onSentence, { minChars = 20 } = {}) {
      this.buf = '';
      this.on = onSentence;
      this.min = minChars;
      this.first = true;
    }
    push(delta) {
      this.buf += delta;
      for (;;) {
        const rx = this.first ? /[.!?;:,]\s+|\n\n/g : /[.!?]\s+|\n\n/g;
        let m, cut = -1;
        while ((m = rx.exec(this.buf))) {
          const end = m.index + m[0].length;
          const s = this.buf.slice(0, end).trim();
          const hard = /[.!?]$/.test(s);
          if (hard && /(\b(Mr|Mrs|Ms|Dr|St|vs|No|etc|approx|e\.g|i\.e)|\b[ap]\.m|\b[A-Z])\.$/i.test(s)) continue;
          if (s.length >= this.min || (hard && s.length >= 6)) { cut = end; break; }
        }
        if (cut < 0) break;
        const s = this.buf.slice(0, cut).trim();
        this.buf = this.buf.slice(cut);
        this.first = false;
        if (s) this.on(s);
      }
      if (this.buf.length > 300) {
        const i = this.buf.lastIndexOf(' ', 280);
        const s = this.buf.slice(0, i > 80 ? i : 280).trim();
        this.buf = this.buf.slice(s.length);
        this.first = false;
        if (s) this.on(s);
      }
    }
    flush() {
      const s = this.buf.trim();
      this.buf = '';
      if (s) this.on(s);
    }
  }

  const EDGE_VOICES = [
    { id: 'en-GB-SoniaNeural', name: 'Sonia (British Natural)', lang: 'en-GB', gender: 'Female' },
    { id: 'en-GB-RyanNeural', name: 'Ryan (British Natural)', lang: 'en-GB', gender: 'Male' },
    { id: 'en-US-AriaNeural', name: 'Aria (US Expressive)', lang: 'en-US', gender: 'Female' },
    { id: 'en-US-GuyNeural', name: 'Guy (US Expressive)', lang: 'en-US', gender: 'Male' },
    { id: 'fr-FR-DeniseNeural', name: 'Denise (French Natural)', lang: 'fr-FR', gender: 'Female' },
    { id: 'fr-FR-HenriNeural', name: 'Henri (French Natural)', lang: 'fr-FR', gender: 'Male' },
    { id: 'ar-SA-ZariyahNeural', name: 'Zariyah (Arabic Expressive)', lang: 'ar-SA', gender: 'Female' },
    { id: 'ar-SA-HamedNeural', name: 'Hamed (Arabic Natural)', lang: 'ar-SA', gender: 'Male' },
    { id: 'es-ES-ElviraNeural', name: 'Elvira (Spanish Natural)', lang: 'es-ES', gender: 'Female' },
    { id: 'de-DE-KatjaNeural', name: 'Katja (German Natural)', lang: 'de-DE', gender: 'Female' }
  ];

  class AstroVoiceDesk {
    constructor() {
      this.queue = [];
      this.playing = false;
      this.currentAudio = null;
      this.currentUtterance = null;
      this.audioCtx = null;
      this.analyser = null;
      this.waveCanvas = null;
      this.animId = 0;
      this.isListening = false;
      this.recognition = null;
      this.voiceName = localStorage.getItem('astro_voice_name') || 'en-GB-SoniaNeural';
      this.muted = localStorage.getItem('astro_voice_mute') === '1';
      this.engine = localStorage.getItem('astro_voice_engine') || 'edge'; // 'edge' or 'system'
      this.rate = parseFloat(localStorage.getItem('astro_voice_rate') || '1.0');
      this.pitch = parseFloat(localStorage.getItem('astro_voice_pitch') || '1.0');
      this.autoSpeak = localStorage.getItem('astro_voice_autospeak') === '1';

      this.initRecognition();
    }

    setMuted(m) {
      this.muted = !!m;
      localStorage.setItem('astro_voice_mute', this.muted ? '1' : '0');
      if (this.muted) this.stop();
      this.updateUi();
    }

    toggleMute() {
      this.setMuted(!this.muted);
      return this.muted;
    }

    setEngine(eng) {
      this.engine = eng === 'system' ? 'system' : 'edge';
      localStorage.setItem('astro_voice_engine', this.engine);
    }

    setVoice(v) {
      this.voiceName = v;
      localStorage.setItem('astro_voice_name', v);
    }

    setRate(r) {
      this.rate = Math.max(0.5, Math.min(2.0, r));
      localStorage.setItem('astro_voice_rate', this.rate.toString());
    }

    setAutoSpeak(on) {
      this.autoSpeak = !!on;
      localStorage.setItem('astro_voice_autospeak', this.autoSpeak ? '1' : '0');
    }

    // Synthesize text using Microsoft Edge Neural TTS over client WebSocket
    async synthEdge(text, voice) {
      return new Promise((resolve, reject) => {
        const wsUrl = 'wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4';
        let ws;
        try {
          ws = new WebSocket(wsUrl);
        } catch (e) {
          return reject(e);
        }
        ws.binaryType = 'arraybuffer';
        const chunks = [];
        const timer = setTimeout(() => {
          try { ws.close(); } catch (_) {}
          reject(new Error('Edge TTS timeout'));
        }, 15000);

        ws.onopen = () => {
          const date = new Date().toString();
          const config = 'X-Timestamp:' + date + '\r\nContent-Type:application/json; charset=utf-8\r\nPath:speech.config\r\n\r\n{"context":{"synthesis":{"audio":{"metadataoptions":{"sentenceBoundaryEnabled":"false","wordBoundaryEnabled":"false"},"outputFormat":"audio-24khz-48kbitrate-mono-mp3"}}}}';
          ws.send(config);

          const rPct = (this.rate >= 1.0 ? '+' : '') + Math.round((this.rate - 1.0) * 100) + '%';
          const pHz = '+0Hz';
          const reqId = Math.random().toString(36).slice(2, 12);
          const safeText = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
          const ssml = `<speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis" xml:lang="en-US"><voice name="${voice}"><prosody rate="${rPct}" pitch="${pHz}">${safeText}</prosody></voice></speak>`;
          const msg = `X-RequestId:${reqId}\r\nContent-Type:application/ssml+xml\r\nPath:ssml\r\n\r\n${ssml}`;
          ws.send(msg);
        };

        ws.onmessage = (event) => {
          if (typeof event.data === 'string') {
            if (event.data.includes('Path:turn.end')) {
              clearTimeout(timer);
              ws.close();
              if (chunks.length > 0) {
                resolve(new Blob(chunks, { type: 'audio/mp3' }));
              } else {
                reject(new Error('No audio returned'));
              }
            }
          } else if (event.data instanceof ArrayBuffer) {
            const view = new DataView(event.data);
            if (event.data.byteLength > 2) {
              const headerLen = view.getUint16(0);
              if (event.data.byteLength > headerLen + 2) {
                chunks.push(event.data.slice(headerLen + 2));
              }
            }
          }
        };

        ws.onerror = (e) => {
          clearTimeout(timer);
          reject(e);
        };
      });
    }

    // Play a chunk with waveform visualizer
    speakChunk(text, opts = {}) {
      if (this.muted) return;
      const clean = cleanForSpeech(text);
      if (!clean) return;

      this.queue.push({ text: clean, opts });
      if (!this.playing) this.next();
    }

    async next() {
      if (!this.queue.length || this.muted) {
        this.playing = false;
        this.stopWave();
        return;
      }
      this.playing = true;
      const item = this.queue.shift();
      const v = item.opts.voice || this.voiceName;

      this.startWave();

      if (this.engine === 'edge') {
        try {
          const blob = await this.synthEdge(item.text, v);
          const url = URL.createObjectURL(blob);
          const a = new Audio(url);
          this.currentAudio = a;
          a.playbackRate = this.rate;

          a.onended = () => {
            URL.revokeObjectURL(url);
            this.currentAudio = null;
            this.next();
          };
          a.onerror = () => {
            URL.revokeObjectURL(url);
            this.currentAudio = null;
            this.speakSystem(item.text);
          };
          await a.play();
          return;
        } catch (e) {
          // Fall back to system speech synthesis on error
          this.speakSystem(item.text);
          return;
        }
      }

      this.speakSystem(item.text);
    }

    speakSystem(text) {
      if (!('speechSynthesis' in window)) {
        this.next();
        return;
      }
      const u = new SpeechSynthesisUtterance(text);
      u.rate = this.rate;
      u.pitch = this.pitch;
      const voices = window.speechSynthesis.getVoices();
      if (voices.length > 0) {
        const langCode = this.voiceName.slice(0, 2);
        const match = voices.find(x => x.lang && x.lang.startsWith(langCode));
        if (match) u.voice = match;
      }
      u.onend = () => {
        this.currentUtterance = null;
        this.next();
      };
      u.onerror = () => {
        this.currentUtterance = null;
        this.next();
      };
      this.currentUtterance = u;
      window.speechSynthesis.speak(u);
    }

    stop() {
      this.queue = [];
      this.playing = false;
      if (this.currentAudio) {
        try { this.currentAudio.pause(); } catch (_) {}
        this.currentAudio = null;
      }
      if ('speechSynthesis' in window) {
        try { window.speechSynthesis.cancel(); } catch (_) {}
      }
      this.stopWave();
    }

    startWave() {
      const el = document.getElementById('astro-voice-wave');
      if (el) el.classList.add('active');
    }

    stopWave() {
      const el = document.getElementById('astro-voice-wave');
      if (el) el.classList.remove('active');
    }

    updateUi() {
      const btn = document.getElementById('tb-voice-mute');
      if (btn) {
        btn.classList.toggle('muted', this.muted);
        btn.title = this.muted ? 'Unmute voice' : 'Mute voice';
      }
    }

    // Speech to text listener
    initRecognition() {
      const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
      if (!SpeechRecognition) return;

      this.recognition = new SpeechRecognition();
      this.recognition.continuous = false;
      this.recognition.interimResults = true;

      this.recognition.onstart = () => {
        this.isListening = true;
        const mic = document.getElementById('voice-input-btn');
        if (mic) mic.classList.add('listening');
      };

      this.recognition.onresult = (event) => {
        let transcript = '';
        for (let i = event.resultIndex; i < event.results.length; ++i) {
          transcript += event.results[i][0].transcript;
        }
        const box = document.getElementById('chat-input') || document.getElementById('composer-text');
        if (box && transcript) {
          box.value = transcript;
          box.dispatchEvent(new Event('input', { bubbles: true }));
        }
      };

      this.recognition.onend = () => {
        this.isListening = false;
        const mic = document.getElementById('voice-input-btn');
        if (mic) mic.classList.remove('listening');
      };

      this.recognition.onerror = () => {
        this.isListening = false;
        const mic = document.getElementById('voice-input-btn');
        if (mic) mic.classList.remove('listening');
      };
    }

    toggleListen() {
      if (!this.recognition) {
        alert('Speech recognition is not supported in this browser.');
        return;
      }
      if (this.isListening) {
        try { this.recognition.stop(); } catch (_) {}
      } else {
        const lang = document.documentElement.lang || 'en-US';
        this.recognition.lang = lang;
        try { this.recognition.start(); } catch (_) {}
      }
    }
  }

  window.AstroVoiceDesk = AstroVoiceDesk;
  window.AstroVoice = new AstroVoiceDesk();
  window.SentenceSplitter = SentenceSplitter;
  window.cleanForSpeech = cleanForSpeech;
  window.EDGE_VOICES = EDGE_VOICES;
})();
