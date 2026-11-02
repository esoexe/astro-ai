// master_captions.inc - master-skills edit/tools/captions.py: word-timed captions, 1-3 word phrases as SRT, styled ASS
// (bold, active word highlighted, inside the 9:16 safe zone) and a JSON cue list for HyperFrames/Remotion. Burning in needs
// ffmpeg's ass or subtitles filter (libass). Transcription uses whisper.cpp (C++; the original used the faster-whisper
// Python package): any other transcriber's word timings work through --words.
namespace packtools::master_captions {
using namespace pyrt;
using namespace packtools::master_mk;

inline const std::set<std::string> FILLERS{"um", "umm", "ummm", "uh", "uhh", "uhm", "er", "erm", "ah", "ahh", "hmm", "hm", "mm", "mmm"};
// words that open a phrase: a chunk should not end on them when the sentence continues
inline const std::set<std::string> WEAK_ENDINGS{"a", "an", "the", "to", "of", "and", "or", "but", "for", "in", "on", "at", "with", "from", "by", "into", "my", "your",
                                                "our", "their", "his", "her", "its", "if", "as", "is", "are", "was", "were", "will", "can"};
inline const char* VERBATIM_PROMPT = "Um, so, uh, I mean, like, you know, uh, okay. Hmm, um, let me, uh, think.";
inline const char* INSTALL_HINT =
    "whisper.cpp is not installed, so I cannot transcribe. Either install it (macOS: brew install whisper-cpp; Windows and Linux: "
    "the whisper-cli build from github.com/ggml-org/whisper.cpp, on PATH or named by EDIT_WHISPER) and download a model "
    "(ggml-small.bin from huggingface.co/ggerganov/whisper.cpp into ~/.cache/whisper.cpp/), or pass --words with word timings "
    "from any transcriber: ElevenLabs Scribe, Whisper/faster-whisper JSON with word timestamps, whisper.cpp JSON, Deepgram, "
    "AssemblyAI, HyperFrames transcript.json, or an SRT/VTT (word times approximated inside each cue).";

struct CWord { Word w; std::string display; };
inline std::string norm(const std::string& text) { return master::strip_chars(re::sub(R"([^\w']+)", "", lower(text)), "'"); }
inline std::string last_cp(const std::string& s) { return cpslice(s, -1); }
inline bool in_set(const std::string& c, const char* chars) { return c.empty() || std::string(chars).find(c) != std::string::npos; }   // `c in "..."`
inline std::string display_text(std::string text, const std::string& kase, bool keep_punct) {
    if (!keep_punct) {
        text = re::sub(R"([,;:]+$)", "", text);
        if (ends(text, ".") && std::count(text.begin(), text.end(), '.') == 1) text.pop_back();
    }
    return kase == "upper" ? upper(text) : kase == "lower" ? lower(text) : text;
}
struct Cue { double start, end; std::vector<CWord> words; };
// group words into caption phrases: break after . ! ? (and after commas once a phrase has 2 words), on pauses, at
// max_words / max_chars; never end a full phrase on a weak opener, and grow a phrase rather than flash it < min_dur
inline std::vector<Cue> chunk_words(const std::vector<CWord>& words, std::size_t max_words = 3, long long max_chars = 18, double pause = 0.3, double min_dur = 0.3) {
    std::vector<std::vector<CWord>> chunks; std::vector<CWord> cur;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const CWord& word = words[i];
        const CWord* nxt = i + 1 < words.size() ? &words[i + 1] : nullptr;
        cur.push_back(word);
        std::vector<std::string> t; for (const auto& c : cur) t.push_back(c.w.text);
        const long long text_len = static_cast<long long>(cplen(join(t, " ")));
        const bool hard = in_set(last_cp(word.w.text), ".!?") || !nxt || nxt->w.start - word.w.end >= pause;
        const bool soft = in_set(last_cp(word.w.text), ",;:") && cur.size() >= 2;
        const long long nxt_len = nxt ? text_len + 1 + static_cast<long long>(cplen(nxt->w.text)) : 0;
        const bool full = cur.size() >= max_words || (nxt && nxt_len > max_chars);
        if (!(hard || soft || full)) continue;
        if (!hard && cur.size() < max_words && nxt && nxt_len <= max_chars + 4 && nxt->w.end - cur[0].w.start < 1.2 && word.w.end - cur[0].w.start < min_dur)
            continue;   // too quick to read: let it grow by one word
        if (full && !hard && cur.size() >= 2 && WEAK_ENDINGS.count(norm(word.w.text))) {
            CWord carry = cur.back(); cur.pop_back();
            chunks.push_back(cur);
            cur = {carry};
            continue;
        }
        chunks.push_back(cur);
        cur.clear();
    }
    if (!cur.empty()) chunks.push_back(cur);
    std::vector<Cue> cues;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto& g = chunks[i];
        const double start = g.front().w.start;
        double end = g.back().w.end;
        if (i + 1 < chunks.size()) {
            const double next_start = chunks[i + 1].front().w.start;
            end = next_start - end <= 0.5 ? next_start : end + 0.25;
            end = std::min(std::max(end, start + min_dur), next_start);
        } else end = std::max(end + 0.25, start + min_dur);
        cues.push_back({round_n(start, 3), round_n(end, 3), g});
    }
    return cues;
}
inline std::string pad2(long long v) { return v < 10 ? "0" + std::to_string(v) : std::to_string(v); }
inline std::string srt_time(double t) {
    const long long ms = round_int(std::max(0.0, t) * 1000);
    std::string f = std::to_string(ms % 1000); f = std::string(3 - f.size(), '0') + f;
    return pad2(ms / 3600000) + ":" + pad2(ms / 60000 % 60) + ":" + pad2(ms / 1000 % 60) + "," + f;
}
inline std::string ass_time(double t) {
    const long long cs = round_int(std::max(0.0, t) * 100);
    return std::to_string(cs / 360000) + ":" + pad2(cs / 6000 % 60) + ":" + pad2(cs / 100 % 60) + "." + pad2(cs % 100);
}
inline std::string ass_color(const std::string& hex, int alpha = 0) {
    std::size_t k = 0; while (k < hex.size() && hex[k] == '#') k++;
    const std::string h = hex.substr(k);
    if (!re::fullmatch("[0-9a-fA-F]{6}", h).ok) throw EditError("bad colour " + str_repr(hex) + ", use #RRGGBB", EXIT_USAGE);
    char a[3]; std::snprintf(a, sizeof a, "%02X", alpha);
    return upper(std::string("&H") + a + h.substr(4, 2) + h.substr(2, 2) + h.substr(0, 2));
}
struct Layout { int align; long long margin_v, anchor_y, margin_x; bool pos = false; };
// 9:16 lower = text bottom at 75% height (y 1440 of 1920, under the IG caption block line of 1470); seam = centred on a
// split-screen seam; top = just under the 240 px header. Side margins keep text clear of the right-hand button rail.
inline Layout layout(long long width, long long height, const std::string& position, std::optional<long long> y) {
    const bool vertical = height > width;
    const long long margin_x = round_int(static_cast<double>(width) * (vertical ? 0.13 : 0.08));
    if (position == "lower") {
        const long long bottom = y ? *y : round_int(static_cast<double>(height) * (vertical ? 0.75 : 0.92));
        return {2, height - bottom, bottom, margin_x};
    }
    if (position == "top") {
        const long long top = y ? *y : round_int(static_cast<double>(height) * (vertical ? 0.135 : 0.06));
        return {8, top, top, margin_x};
    }
    const long long centre = y ? *y : height / 2;
    return {5, 0, centre, margin_x, true};
}
struct Style { std::string position, font, color, accent, kase; std::optional<long long> y; long long font_size = 0; bool no_highlight = false; };
inline std::string build_ass(const std::vector<Cue>& cues, long long width, long long height, const Style& a) {
    const Layout lay = layout(width, height, a.position, a.y);
    const long long size = a.font_size ? a.font_size : round_int(static_cast<double>(height) * (height > width ? 0.044 : 0.06));
    const long long outline = std::max<long long>(2, round_int(static_cast<double>(size) * 0.075));
    const auto S = [](long long v) { return std::to_string(v); };
    std::vector<std::string> head{
        "[Script Info]", "; made by master-skills edit/captions", "ScriptType: v4.00+", "PlayResX: " + S(width), "PlayResY: " + S(height), "WrapStyle: 0",
        "ScaledBorderAndShadow: yes", "YCbCr Matrix: TV.709", "", "[V4+ Styles]",
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
        "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
        "Style: Caption," + a.font + "," + S(size) + "," + ass_color(a.color) + "," + ass_color(a.color) + ",&H00000000,&H64000000,-1,0,0,0,100,100,0,0,1," + S(outline) +
            ",2," + S(lay.align) + "," + S(lay.margin_x) + "," + S(lay.margin_x) + "," + S(lay.margin_v) + ",1",
        "", "[Events]", "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text"};
    const std::string pos = lay.pos ? "{\\an5\\pos(" + S(width / 2) + "," + S(lay.anchor_y) + ")}" : "";
    const std::string accent = ass_color(a.accent);
    struct Ev { double s, e; std::string text; };
    std::vector<Ev> events;
    for (const auto& cue : cues) {
        std::vector<std::string> texts; for (const auto& w : cue.words) texts.push_back(w.display);
        if (a.no_highlight) { events.push_back({cue.start, cue.end, pos + join(texts, " ")}); continue; }
        for (std::size_t k = 0; k < cue.words.size(); ++k) {
            const double start = k == 0 ? cue.start : cue.words[k].w.start;
            const double end = k + 1 == cue.words.size() ? cue.end : cue.words[k + 1].w.start;
            if (end <= start) continue;
            std::vector<std::string> parts;
            for (std::size_t j = 0; j < texts.size(); ++j) parts.push_back(j == k ? "{\\c" + accent + "&}" + texts[j] + "{\\r}" : texts[j]);
            events.push_back({start, end, pos + join(parts, " ")});
        }
    }
    for (const auto& e : events) head.push_back("Dialogue: 0," + ass_time(e.s) + "," + ass_time(e.e) + ",Caption,,0,0,0,," + e.text);
    return join(head, "\n") + "\n";
}

// ------------------------------------------------------------------ transcription (whisper.cpp)
inline std::string model_path(const std::string& model, const std::string& bin) {
    if (is_file(model)) return model;
    const std::string file = ends(model, ".bin") ? model : "ggml-" + model + ".bin";
    std::vector<std::string> dirs;
    if (!env("EDIT_WHISPER_MODELS").empty()) dirs.push_back(env("EDIT_WHISPER_MODELS"));
    std::string home = env("HOME");
    if (home.empty()) home = env("USERPROFILE");
    if (!home.empty()) { dirs.push_back(path_join(home, ".cache/whisper.cpp")); dirs.push_back(path_join(home, ".cache/whisper")); }
    const std::string bdir = dirname(abspath(bin));
    dirs.push_back(path_join(bdir, "models")); dirs.push_back(path_join(dirname(bdir), "models")); dirs.push_back("models");
    for (const auto& d : dirs) { const std::string p = path_join(d, file); if (is_file(p)) return p; }
    throw EditError("whisper.cpp model " + str_repr(model) + " not found. Download https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + file +
                    " into ~/.cache/whisper.cpp/ (or set EDIT_WHISPER_MODELS, or pass --model /path/to/ggml-model.bin)", EXIT_MISSING);
}
struct TxOpts { std::string model, device, language, prompt; bool verbatim = false; };
inline std::pair<std::vector<Word>, std::string> transcribe(const std::string& media, const TxOpts& o) {
    const std::string bin = whisper_bin();
    if (bin.empty()) throw EditError(INSTALL_HINT, EXIT_MISSING);
    const std::string model = model_path(o.model, bin);
    std::vector<std::string> pp; if (o.verbatim) pp.push_back(VERBATIM_PROMPT); if (!o.prompt.empty()) pp.push_back(o.prompt);
    const std::string prompt = join(pp, " ");
    eprint("transcribing with whisper.cpp " + o.model + "...");
    TempDir tmp("captions-");
    const std::string wav = path_join(tmp.path, "audio.wav"), stem = path_join(tmp.path, "words");
    run({ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", media, "-map", "0:a:0", "-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", wav});
    // one word per segment (-ml 1 -sow) gives word timings in the JSON's transcription[].offsets (milliseconds)
    std::vector<std::string> cmd{bin, "-m", model, "-f", wav, "-oj", "-of", stem, "-ml", "1", "-sow", "-bs", "5", "-np", "-l", o.language.empty() ? "auto" : o.language};
    if (!prompt.empty()) { cmd.push_back("--prompt"); cmd.push_back(prompt); }
    if (o.device == "cpu") cmd.push_back("-ng");
    run(cmd);
    if (!is_file(stem + ".json")) throw EditError("whisper.cpp wrote no transcript (" + stem + ".json)", EXIT_FAIL);
    const Val data = loads(read_text(stem + ".json"));
    std::vector<RawWord> words;
    for (const auto& seg : or_empty(get(data, "transcription"))) {
        const std::string text = strip(pystr(get(seg, "text")));
        if (text.empty() || (starts(text, "[") && ends(text, "]"))) continue;   // [BLANK_AUDIO], [Music]: not words
        const Val off = get(seg, "offsets");
        const auto s = num(get(off, "from")), e = num(get(off, "to"));
        if (!s) continue;
        words.push_back({Val::str(text), *s / 1000.0, (e ? *e : *s) / 1000.0, std::nullopt});
    }
    const Val lang = get(get(data, "result"), "language");
    return {clean_words(words), truthy(lang) ? pystr(lang) : !o.language.empty() ? o.language : "en"};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("captions", "Word-timed captions: 1-3 word phrases as SRT, styled ASS and a JSON cue list.");
    p.epilog = EXIT_CODES_HELP; p.raw_description = true;
    p.pos("input").help("video/audio file (or a words JSON/SRT/VTT when there is no media)");
    p.opt({"--words"}).help("word timings (JSON/SRT/VTT); skips transcription");
    p.opt({"-o", "--outdir"}).help("output folder (default: next to the input)");
    p.opt({"--name"}).help("output file stem (default: input stem)");
    p.opt({"--words-only"}).store_true().help("just transcribe: write words.json + phrases.txt");
    p.opt({"--model"}).def("small").help("whisper.cpp model: small; medium/large-v3 for finals, or a ggml .bin path");
    p.opt({"--language"}).help("language code, e.g. en (default: auto)");
    p.opt({"--prompt"}).help("vocabulary hint: names, brands, jargon");
    p.opt({"--verbatim"}).store_true().help("nudge Whisper to keep ums/uhs (for deadspace)");
    p.opt({"--device"}).def("auto").help("auto (GPU when whisper.cpp has one) or cpu");
    p.opt({"--retranscribe"}).store_true().help("ignore a cached words.json");
    p.opt({"--max-words"}).integer().def(3).help("words per caption (1-3 recommended)");
    p.opt({"--max-chars"}).integer().def(18).help("characters per caption line");
    p.opt({"--pause"}).number().def_float(0.3).help("a pause this long starts a new caption (s)");
    p.opt({"--case"}).choices({"upper", "lower", "keep"}).def("upper");
    p.opt({"--keep-fillers"}).store_true().help("show um/uh in captions");
    p.opt({"--keep-punctuation"}).store_true().help("keep trailing commas and periods");
    p.opt({"--size"}).help("canvas WxH (default: the video's display size, else 1080x1920)");
    p.opt({"--position"}).choices({"lower", "seam", "top"}).def("lower");
    p.opt({"--y"}).integer().help("anchor y in px (lower: text bottom, seam: centre, top: text top)");
    p.opt({"--font"}).def("Arial").help("font family (must be installed for libass)");
    p.opt({"--font-size"}).integer().help("px (default 4.4%% of height, 84 on 1920)");
    p.opt({"--color"}).def("#FFFFFF").help("text colour");
    p.opt({"--accent"}).def("#FFD400").help("active-word colour");
    p.opt({"--no-highlight"}).store_true().help("no active-word highlight");
    p.opt({"--burn"}).metavar("OUT").help("burn the ASS into a copy of the video (needs libass)");
    p.opt({"--json"}).store_true().help("print a JSON summary");
    const Val args = parse_args(p, argv);
    const long long max_words = get(args, "max_words").i;
    if (!(1 <= max_words && max_words <= 8)) ap::fail(ap::norm(p, p.prog), "--max-words must be 1-8");
    auto opt_s = [&](const char* k) { const Val v = get(args, k); return v.t == Val::STR ? v.s : std::string(); };
    const std::string input = S(args, "input");

    return run_main([&]() -> int {
        const std::string ext = lower(splitext(input).second);
        const bool is_text_input = ext == ".json" || ext == ".srt" || ext == ".vtt";
        const Val info = is_text_input ? Val() : probe(input);
        const std::string outdir = !opt_s("outdir").empty() ? opt_s("outdir") : dirname(abspath(input));
        makedirs(outdir);
        const std::string stem = !opt_s("name").empty() ? opt_s("name") : stem_of(input);
        const std::string base = path_join(outdir, stem);
        Val summary = Val::dict(); Val files = Val::dict(); summary.d->set("files", files);
        std::vector<Word> words; bool have_words = false;
        if (!opt_s("words").empty() || is_text_input) {
            const Val d = truthy(info) ? get(info, "duration") : Val();
            words = load_words(!opt_s("words").empty() ? opt_s("words") : input, d.is_none() ? std::nullopt : std::optional<double>(d.num()));
            have_words = true;
        } else {
            if (!truthy(get(info, "audio"))) throw EditError("no audio stream to transcribe; pass --words", EXIT_USAGE);
            const std::string cache = base + ".words.json";
            std::string language;
            if (is_file(cache) && !B(args, "retranscribe")) {
                const Val doc = loads(read_text(cache));
                const Val src = truthy(get(doc, "source")) ? get(doc, "source") : Val::dict();
                const Val sz = get(src, "size"), mt = get(src, "mtime");
                const bool same_size = sz.is_real() && sz.num() == static_cast<double>(size_of(input));
                if (same_size && std::fabs((truthy(mt) ? mt.num() : 0.0) - mtime_of(input)) < 1) {
                    words = load_words(cache); have_words = true;
                    const Val lc = get(doc, "language_code"); language = truthy(lc) ? pystr(lc) : "";
                    eprint("using cached transcript " + cache);
                }
            }
            if (!have_words) {
                TxOpts o{S(args, "model"), S(args, "device"), opt_s("language"), opt_s("prompt"), B(args, "verbatim")};
                auto r = transcribe(input, o);
                words = std::move(r.first); language = r.second; have_words = true;
                save_words(cache, words, input, "whisper.cpp " + o.model, language);
            }
            files.d->set("words", Val::str(cache));
            std::string ph; for (const auto& l : phrase_lines(words)) ph += l + "\n";
            if (ph.empty()) ph = "\n";
            write_text(base + ".phrases.txt", ph);
            files.d->set("phrases", Val::str(base + ".phrases.txt"));
        }
        if (words.empty()) throw EditError("no words to caption", EXIT_USAGE);
        summary.d->set("words", Val::integer(static_cast<long long>(words.size())));
        if (B(args, "words_only")) {
            if (B(args, "json")) print(json_print(summary));
            else print("transcript: " + std::to_string(words.size()) + " words -> " + pystr(get(files, "words")));
            return EXIT_OK;
        }
        Style st{S(args, "position"), S(args, "font"), S(args, "color"), S(args, "accent"), S(args, "case"), std::nullopt, 0, B(args, "no_highlight")};
        if (!get(args, "y").is_none()) st.y = get(args, "y").i;
        if (!get(args, "font_size").is_none()) st.font_size = get(args, "font_size").i;
        std::vector<CWord> shown;
        for (const auto& w : words) {
            if (!B(args, "keep_fillers") && FILLERS.count(norm(w.text))) continue;
            CWord c{w, display_text(w.text, st.kase, B(args, "keep_punctuation"))};
            if (!c.display.empty()) shown.push_back(c);
        }
        const auto cues = chunk_words(shown, static_cast<std::size_t>(max_words), get(args, "max_chars").i, get(args, "pause").num());
        long long width = 1080, height = 1920;
        if (!opt_s("size").empty()) std::tie(width, height) = parse_size(opt_s("size"));
        else if (truthy(info) && truthy(get(info, "video"))) { width = get(get(info, "video"), "display_width").i; height = get(get(info, "video"), "display_height").i; }

        std::string srt;
        for (std::size_t i = 0; i < cues.size(); ++i) {
            std::vector<std::string> t; for (const auto& w : cues[i].words) t.push_back(w.display);
            srt += std::to_string(i + 1) + "\n" + srt_time(cues[i].start) + " --> " + srt_time(cues[i].end) + "\n" + join(t, " ") + "\n\n";
        }
        write_text(base + ".srt", srt);
        write_text(base + ".ass", build_ass(cues, width, height, st));
        const Layout lay = layout(width, height, st.position, st.y);
        Val doc = Val::dict();
        doc.d->set("version", Val::integer(1)); doc.d->set("kind", Val::str("caption-cues")); doc.d->set("source", truthy(info) ? get(info, "path") : Val());
        doc.d->set("width", Val::integer(width)); doc.d->set("height", Val::integer(height));
        if (width == 1080 && height == 1920) {
            Val sa = Val::dict(); sa.d->set("x0", Val::integer(90)); sa.d->set("y0", Val::integer(240)); sa.d->set("x1", Val::integer(950)); sa.d->set("y1", Val::integer(1470));
            doc.d->set("safe_area", sa);
        } else doc.d->set("safe_area", Val());
        Val style = Val::dict();
        style.d->set("case", Val::str(st.kase)); style.d->set("font", Val::str(st.font)); style.d->set("color", Val::str(st.color)); style.d->set("accent", Val::str(st.accent));
        style.d->set("position", Val::str(st.position)); style.d->set("anchor_y", Val::integer(lay.anchor_y));
        style.d->set("align", Val::str(lay.align == 2 ? "bottom" : lay.align == 8 ? "top" : "middle"));
        doc.d->set("style", style);
        Val cl = Val::list();
        for (std::size_t i = 0; i < cues.size(); ++i) {
            std::vector<std::string> t; Val wl = Val::list();
            for (const auto& w : cues[i].words) {
                t.push_back(w.display);
                Val wd = Val::dict(); wd.d->set("text", Val::str(w.display)); wd.d->set("start", Val::flt(w.w.start)); wd.d->set("end", Val::flt(w.w.end)); wl.l->push_back(wd);
            }
            Val c = Val::dict(); c.d->set("i", Val::integer(static_cast<long long>(i))); c.d->set("start", Val::flt(cues[i].start)); c.d->set("end", Val::flt(cues[i].end));
            c.d->set("text", Val::str(join(t, " "))); c.d->set("words", wl); cl.l->push_back(c);
        }
        doc.d->set("cues", cl);
        write_json(base + ".cues.json", doc);
        files.d->set("srt", Val::str(base + ".srt")); files.d->set("ass", Val::str(base + ".ass")); files.d->set("cues", Val::str(base + ".cues.json"));
        summary.d->set("cues", Val::integer(static_cast<long long>(cues.size())));

        int code = EXIT_OK;
        if (!opt_s("burn").empty()) {
            if (!truthy(info) || !truthy(get(info, "video"))) throw EditError("--burn needs a video input", EXIT_USAGE);
            const std::string flt = has_filter("ass") ? "ass" : has_filter("subtitles") ? "subtitles" : "";
            if (flt.empty()) {
                eprint("cannot burn in: this ffmpeg has no libass (no 'ass' or 'subtitles' filter). The SRT, ASS and cues files are written. Route: render the "
                       "captions with HyperFrames (/embedded-captions, reusing the words.json as transcript.json), or install an ffmpeg with libass (macOS: brew "
                       "uninstall ffmpeg, then brew trust homebrew-ffmpeg/ffmpeg, brew tap homebrew-ffmpeg/ffmpeg, brew install homebrew-ffmpeg/ffmpeg/ffmpeg; most "
                       "Linux distro ffmpeg builds include libass).");
                summary.d->set("burned", Val::boolean(false));
                code = EXIT_MISSING;
            } else {
                TempDir tmp("captions-");   // a relative name avoids filter path escaping
                std::error_code ec;
                std::filesystem::copy_file(fspath(base + ".ass"), fspath(path_join(tmp.path, "captions.ass")), std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) throw OSError(ec.value(), base + ".ass");
                const auto vargs = video_codec_args(get(info, "video"), 17).first;
                std::vector<std::string> cmd{ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", abspath(input), "-vf", flt + "=captions.ass", "-map", "0:v:0", "-map", "0:a?"};
                cmd.insert(cmd.end(), vargs.begin(), vargs.end());
                for (const auto& x : std::vector<std::string>{"-c:a", "copy", "-movflags", "+faststart", abspath(opt_s("burn"))}) cmd.push_back(x);
                run(cmd, tmp.path);
                summary.d->set("burned", Val::boolean(true));
                files.d->set("burned", Val::str(abspath(opt_s("burn"))));
            }
        }
        if (B(args, "json")) print(json_print(summary));
        else {
            print("captions: " + std::to_string(cues.size()) + " phrases from " + std::to_string(words.size()) + " words");
            for (const auto& kv : files.d->items) print("  " + kv.first + ": " + pystr(kv.second));
        }
        return code;
    });
}
PACKTOOL("master-edit/captions", main);
}  // namespace packtools::master_captions
