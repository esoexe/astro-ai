// master-virality/speech - how long a line takes to say (numbers counted the way they are spoken), and the real speaking
// pace measured from a transcript (SRT, WebVTT, Whisper JSON, YouTube json3, or plain text).
// C++ port of master-skills/skills/virality/tools/speech.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::master_speech {
using namespace pyrt;
using namespace packtools::master;

constexpr double DEFAULT_WPM = 165.0;
// a numeric token, after surrounding punctuation is stripped
inline const std::string NUM_RE = R"(^(?P<cur>[$£€¥])?(?P<num>\d[\d,]*(?:\.\d+)?)(?P<suf>bn|[kKmMbB]|%|[xX]|st|nd|rd|th|s)?(?P<rest>.*)$)";
inline const char* COMPOUND_NUM_RE = R"(^\d+(?:[/:\-–]\d+)+$)";   // 24/7, 3:30, 5-10
inline const char* STRIP_EDGES = "\"'()[]{}<>.,!?;:*_‘’“”";
inline const char* DOMAIN_RE = R"(^[A-Za-z0-9-]+(?:\.[A-Za-z]{2,})+$)";
inline const char* TS_RE = R"((?:(\d+):)?(\d{1,2}):(\d{2})(?:[.,](\d{1,3}))?)";
inline const char* CUE_RE = R"(^\s*(\S+)\s*-->\s*(\S+))";
inline const char* VTT_TAG_RE = R"(<[^>]+>)";

// ------------------------------------------------------------------ numbers
inline long long under_1000(long long g) {
    const long long hundreds = g / 100, rest = g % 100;
    long long w = hundreds ? 2 : 0;                               // "four hundred"
    if (rest) w += (rest < 20 || rest % 10 == 0) ? 1 : 2;         // "twelve", "forty", "forty two"
    return w;
}
// words in an integer said out loud, US style; digits is the integer as a decimal string (any size)
inline long long int_words(const std::string& digits0) {
    std::string d = ascii_digits(digits0);
    std::size_t z = 0; while (z < d.size() && d[z] == '0') ++z;
    d = d.substr(z);
    if (d.empty()) return 1;
    long long words = 0, scale = 0;
    for (long end = static_cast<long>(d.size()); end > 0; end -= 3) {
        const long beg = std::max(0L, end - 3);
        const long long group = std::stoll(d.substr(static_cast<std::size_t>(beg), static_cast<std::size_t>(end - beg)));
        if (group) words += under_1000(group) + (scale ? 1 : 0);   // + thousand / million
        ++scale;
    }
    return words;
}
inline long long year_words(long long n) {   // 1999 -> nineteen ninety nine, 2005 -> two thousand five
    if (n >= 2000 && n <= 2009) return n == 2000 ? 2 : 3;
    const long long hi = n / 100, lo = n % 100;
    if (lo == 0) return under_1000(hi) + 1;                       // "nineteen hundred"
    return under_1000(hi) + (lo < 10 ? 1 : 0) + under_1000(lo);
}
inline std::optional<long long> number_token_words(const std::string& token) {
    if (re::match(COMPOUND_NUM_RE, token)) {
        long long sum = 0;
        for (const auto& p : re::split(R"([/:\-–])", token)) sum += int_words(p);
        return sum + static_cast<long long>(re::findall(R"([\-–])", token).size());   // 5-10 -> "five to ten"
    }
    const auto& rx = re::compile(NUM_RE);
    const auto m = rx.match(token);
    if (!m) return std::nullopt;
    const std::string cur = m.group(1), num = m.group(2), suf = m.group(3), rest = m.group(4);
    const std::string nc = replace_all(num, ",", "");
    const std::size_t dot = nc.find('.');
    const std::string whole = nc.substr(0, dot), frac = dot == std::string::npos ? "" : nc.substr(dot + 1);
    if (whole.empty()) return std::nullopt;
    std::string wd = ascii_digits(whole); std::size_t z = 0; while (z + 1 < wd.size() && wd[z] == '0') ++z; wd = wd.substr(z);
    const long long value = wd.size() <= 6 ? std::stoll(wd) : 1000000;   // only compared against 1900..2099
    const bool plain = cur.empty() && suf.empty() && num.find(',') == std::string::npos && frac.empty();
    long long words;
    if (plain && value >= 1900 && value <= 2099) words = year_words(value);
    else if (suf == "s" && value >= 1900 && value <= 2099) words = year_words(value);   // the 1990s
    else words = int_words(whole);
    if (!frac.empty()) words += 1 + static_cast<long long>(cplen(frac));                // "point two five"
    const std::string sl = lower(suf);
    if (sl == "k" || sl == "m" || sl == "b" || sl == "bn" || sl == "x" || sl == "%") words += 1;   // thousand / million / ex / percent
    if (!cur.empty()) words += 1;                                                       // dollars
    if (!rest.empty()) words += static_cast<long long>(re::findall("[A-Za-z]+", rest).size());   // "$20/mo" -> + "mo"
    return words;
}
// word count of text as said out loud: numbers expanded, symbols voiced, emoji silent
inline long long spoken_words(const std::string& text) {
    long long total = 0;
    for (const auto& raw : split_ws(text)) {
        const std::string token = strip_chars(raw, STRIP_EDGES);
        if (token.empty()) continue;
        if (const auto n = number_token_words(token)) { total += *n; continue; }
        if (token == "&" || token == "+" || token == "=" || token == "%" || token == "@") { total += 1; continue; }
        if (re::match(R"(^#\d+$)", token)) { total += 1 + int_words(cpslice(token, 1)); continue; }   // "#1" -> "number one"
        // "highest-paying" is two words out loud, "don't" is one, "opusjake.ai" is three.
        long long parts = 0;
        for (const auto& p : re::split(R"([-–/.]+)", token)) if (re::search("[A-Za-z0-9]", p)) ++parts;
        if (parts) {
            total += parts;
            if (re::match(DOMAIN_RE, token)) total += static_cast<long long>(std::count(token.begin(), token.end(), '.'));   // "dot"
        }
    }
    return total;
}
inline double seconds(const std::string& text, double wpm) { return static_cast<double>(spoken_words(text)) * 60.0 / wpm; }
inline std::string timecode(double sec) {
    sec = sec > 0.0 ? sec : 0.0;   // max(0.0, sec)
    double s; const double m = py_divmod(sec, 60.0, s);
    char b[64]; std::snprintf(b, sizeof b, "%lld:%04.1f", static_cast<long long>(m), s); return b;
}

// ------------------------------------------------------------------ transcripts
struct Cue { bool timed = false; double start = 0, end = 0; std::string text; };

inline double parse_ts(const std::string& s) {
    const auto m = re::search(TS_RE, s);
    if (!m) throw PyErr("ValueError", "bad timestamp: " + str_repr(s));
    auto num = [](const std::string& d) { return std::strtod(ascii_digits(d).c_str(), nullptr); };
    std::string ms = m.has(4) ? m.group(4) : "0"; while (cplen(ms) < 3) ms += "0";
    const double whole = (m.has(1) ? num(m.group(1)) : 0.0) * 3600 + num(m.group(2)) * 60 + num(m.group(3));
    return whole + num(ms) / 1000.0;
}
inline std::vector<Cue> load_subtitles(const std::string& raw) {
    struct C { double s, e; std::vector<std::string> lines; };
    std::vector<C> cues; long cur = -1;
    for (const auto& line : splitlines(raw)) {
        const auto m = re::match(CUE_RE, line);
        if (m) {
            try { const double a = parse_ts(m.group(1)), b = parse_ts(m.group(2)); cues.push_back({a, b, {}}); cur = static_cast<long>(cues.size()) - 1; }
            catch (const PyErr& e) { if (e.type != "ValueError") throw; cur = -1; }
            continue;
        }
        if (strip_chars(line, "\r").empty()) { cur = -1; continue; }   // an empty line ends a cue (SRT and WebVTT agree on this)
        if (cur < 0) continue;                                         // sequence numbers, the WEBVTT header, NOTE and STYLE blocks
        const std::string text = strip(re::sub(VTT_TAG_RE, "", line));
        if (!text.empty()) cues[static_cast<std::size_t>(cur)].lines.push_back(text);   // YouTube puts whitespace-only lines inside its cues
    }
    std::vector<Cue> out; std::vector<std::string> prev;
    for (const auto& c : cues) {
        // YouTube's rolling auto-captions repeat the previous cue's lines at the top of the next one.
        std::vector<std::string> fresh;
        for (const auto& l : c.lines) if (std::find(prev.begin(), prev.end(), l) == prev.end()) fresh.push_back(l);
        if (!fresh.empty()) out.push_back({true, c.s, c.e, join(fresh, " ")});
        prev = c.lines;
    }
    return out;
}
inline double div1000(const Val& v) {   // x / 1000.0
    if (!v.is_real()) throw PyErr("TypeError", std::string("unsupported operand type(s) for /: '") + type_name(v) + "' and 'float'");
    return v.num() / 1000.0;
}
inline std::vector<Cue> load_json(const Val& data) {
    std::vector<Cue> out;
    if (data.t == Val::DICT && data.d->has("events")) {   // YouTube json3
        List events; for (const auto& e : iterate(data.d->at("events"))) if (truthy(as_dict(e).at("segs"))) events.push_back(e);
        for (std::size_t i = 0; i < events.size(); ++i) {
            const Val& e = events[i]; std::string text;
            for (const auto& s : iterate(e.d->at("segs"))) {
                const Val u = as_dict(s).at("utf8", Val::str(""));
                if (u.t != Val::STR) throw PyErr("TypeError", std::string("sequence item: expected str instance, ") + type_name(u) + " found");
                text += u.s;
            }
            text = strip(replace_all(text, "\n", " "));
            if (text.empty()) continue;
            const double start = div1000(e.d->at("tStartMs", Val::integer(0)));
            const Val dur = e.d->at("dDurationMs"); double end;
            if (dur.is_none()) {
                const Val nxt = i + 1 < events.size() ? events[i + 1].d->at("tStartMs") : Val();
                end = !nxt.is_none() ? div1000(nxt) : start;
            } else end = start + div1000(dur);
            out.push_back({true, start, end, text});
        }
        return out;
    }
    const Val segs = data.t == Val::DICT ? data.d->at("segments", Val::list()) : data;
    if (truthy(segs)) for (const auto& s : iterate(segs)) {
        if (s.t != Val::DICT) continue;
        const Val tv = orv(s.d->at("text"), Val::str(""));
        if (tv.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(tv) + "' object has no attribute 'strip'");
        const std::string text = strip(tv.s);
        if (!text.empty()) {
            const double a = to_float(s.d->at("start", Val::integer(0)));
            const double b = to_float(s.d->has("end") ? s.d->at("end") : s.d->at("start", Val::integer(0)));
            out.push_back({true, a, b, text});
        }
    }
    if (out.empty() && data.t == Val::DICT && data.d->at("text").t == Val::STR) out.push_back({false, 0, 0, strip(data.d->at("text").s)});
    return out;
}
inline std::vector<Cue> load_transcript(const std::string& path) {   // plain text comes back as one cue with no timings
    const std::string raw = path == "-" ? read_stdin_text() : decode_utf8_replace(read_text(path, true));
    const std::string ext = lower(splitext(path).second);
    const std::string first = cpslice(lstrip_ws(raw), 0, 1);
    if (ext == ".json" || ext == ".json3" || (ext != ".srt" && ext != ".vtt" && ext != ".txt" && ext != ".md" && (first.empty() || first == "{" || first == "["))) {
        Val data; bool ok = true;
        try { data = loads(raw); } catch (const PyErr& e) { if (e.type.find("JSONDecodeError") == std::string::npos) throw; ok = false; }
        if (ok && !data.is_none()) return load_json(data);   // valid JSON with no transcript in it is empty, not prose
    }
    if (raw.find("-->") != std::string::npos) return load_subtitles(raw);
    const std::string text = join(split_ws(raw), " ");
    if (text.empty()) return {};
    return {Cue{false, 0, 0, text}};
}

// ------------------------------------------------------------------ cli
inline int main(const std::vector<std::string>& argv) {
    Parser p("speech", "Time lines out loud, or measure the speaking pace of a transcript.");
    p.epilog = "Numbers are expanded the way they are said, so '$4,200' counts as five words.";
    p.pos("input").nargs("?").help("transcript (.srt .vtt .json .json3 .txt) or - for stdin");
    p.opt({"--line"}).append().help("a line to time (repeatable)");
    p.opt({"--wpm"}).number().def_float(DEFAULT_WPM).help("speaking rate for timing lines (default %(default)s)");
    p.opt({"--json"}).store_true().help("machine-readable output");
    const Val a = parse_args(p, argv);
    const double wpm = get(a, "wpm").num();
    if (wpm <= 0) ap::fail(ap::norm(p, p.prog), "--wpm must be positive");
    std::vector<std::string> lines; for (const auto& l : iter(get(a, "line"))) lines.push_back(l.s);
    const Val inp = get(a, "input"); const std::string input = inp.t == Val::STR ? inp.s : "";
    if (lines.empty() && input.empty()) { print_help_stderr(p); return 2; }
    const bool as_json = B(a, "json");
    DumpOpts o1; o1.indent = 1;

    if (!lines.empty()) {
        struct R { std::string line; long long words; double start, secs; };
        std::vector<R> rows; double clock = 0.0;
        for (const auto& line : lines) {
            const double dur = seconds(line, wpm);
            rows.push_back({line, spoken_words(line), round_n(clock, 2), round_n(dur, 2)});
            clock += dur;
        }
        if (as_json) {
            Val ls = Val::list();
            for (const auto& r : rows) {
                Val d = Val::dict(); d.d->set("line", Val::str(r.line)); d.d->set("spoken_words", Val::integer(r.words));
                d.d->set("start", Val::flt(r.start)); d.d->set("seconds", Val::flt(r.secs)); ls.l->push_back(d);
            }
            Val res = Val::dict(); res.d->set("wpm", Val::flt(wpm)); res.d->set("lines", ls); res.d->set("total", Val::flt(round_n(clock, 2)));
            print(dumps(res, o1)); return 0;
        }
        print();
        for (const auto& r : rows) {
            char b[64]; std::snprintf(b, sizeof b, "  %4.1fs  %2lld words  ", r.secs, r.words);
            print("  " + timecode(r.start) + b + r.line);
        }
        print("\n  total " + fmt_f(clock, 1) + "s at " + fmt_g(wpm) + " wpm\n");
        return 0;
    }

    if (input != "-" && !exists(input)) { eprint("no such file: " + input); return 2; }
    const auto cues = load_transcript(input);
    if (cues.empty()) { eprint("no speech found in " + input); return 1; }
    // measure: words, span and pace (pace is None when there are no timings)
    std::vector<std::string> texts; for (const auto& c : cues) texts.push_back(c.text);
    const long long words = spoken_words(join(texts, " "));
    bool any = false; double start = 0, end = 0;
    for (const auto& c : cues) if (c.timed) {
        if (!any) { start = c.start; end = c.end; any = true; }
        else { if (c.start < start) start = c.start; if (c.end > end) end = c.end; }
    }
    Val span, pace, vs, ve;
    if (any) {
        const double sp = (end - start) > 0.0 ? end - start : 0.0;
        span = Val::flt(round_n(sp, 2)); vs = Val::flt(start); ve = Val::flt(end);
        if (sp != 0.0) pace = Val::integer(round_int(static_cast<double>(words) / (sp / 60.0)));
    }
    if (as_json) {
        Val res = Val::dict(); res.d->set("cues", Val::integer(static_cast<long long>(cues.size()))); res.d->set("words", Val::integer(words));
        res.d->set("start", vs); res.d->set("end", ve); res.d->set("span", span); res.d->set("wpm", pace);
        print(dumps(res, o1)); return 0;
    }
    print();
    if (truthy(pace)) {
        print("  " + input + "   " + std::to_string(cues.size()) + " cues   " + std::to_string(words) + " spoken words   " + timecode(start) + " -> " + timecode(end) +
              " (" + fmt_f(span.f, 1) + "s)   pace " + std::to_string(pace.i) + " wpm");
        print("  Pass --wpm " + std::to_string(pace.i) + " when you time scripts for this voice.\n");
    } else {
        print("  " + input + "   " + std::to_string(words) + " spoken words, no timings in this file.");
        print("  At " + fmt_g(wpm) + " wpm that is about " + fmt_f(static_cast<double>(words) * 60.0 / wpm, 1) + "s. Use a .srt/.vtt/.json3 to measure real pace.\n");
    }
    return 0;
}
PACKTOOL("master-virality/speech", main);
}  // namespace packtools::master_speech
