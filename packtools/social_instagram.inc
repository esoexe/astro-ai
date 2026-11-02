// The instagram pack (Jakeschincariol/instagram-agent-skill, MIT): ig-caption/caption, ig-reel/beats, ig-reel/hookscore
// and ig-viral/swipe, ported from the Python helpers with the same output. ig-human/detect and ig-human/humanize live in
// social_youtube.inc (they share the master-virality ports). Group 2, so the calibrated hook panel of
// master_hookscore.inc (copied there unchanged from ig-reel/hookscore.py) is reused rather than duplicated.
namespace packtools::ig {
using namespace pyrt;

inline const char* WORD_RE = R"([A-Za-z0-9$%'’-]+)";
inline const char* CONCRETE_RE = R"(\$\s?\d|\b\d[\d,.]*\b|(?<!^)\b[A-Z][a-z]{2,}\b)";   // re.MULTILINE

inline std::vector<std::string> words(const std::string& text) {   // "$18,000" is one word when it is spoken
    return re::findall(WORD_RE, re::sub(R"((?<=\d),(?=\d))", "", text));
}
inline std::string pad0(const std::string& s, std::size_t w) {   // '0' flag: zeros after the sign
    if (s.size() >= w) return s;
    const std::size_t at = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    return s.substr(0, at) + std::string(w - s.size(), '0') + s.substr(at);
}
inline long long py_trunc(double x) {   // int(float)
    if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    if (std::fabs(x) >= 9.2e18) throw PyErr("OverflowError", "int too large for this build");
    return static_cast<long long>(x);
}
inline std::pair<double, double> py_divmod(double vx, double wx) {   // float.__divmod__ (Objects/floatobject.c)
    double mod = std::fmod(vx, wx), div = (vx - mod) / wx;
    if (mod != 0.0 || std::isnan(mod)) {
        if ((wx < 0) != (mod < 0)) { mod += wx; div -= 1.0; }
    } else mod = std::copysign(0.0, wx);
    double fd;
    if (div != 0.0 || std::isnan(div)) { fd = std::floor(div); if (div - fd > 0.5) fd += 1.0; }
    else fd = std::copysign(0.0, vx / wx);
    return {fd, mod};
}
inline std::string expanduser(const std::string& p) {   // os.path.expanduser for "~" and "~/..."
    if (p.empty() || p[0] != '~' || (p.size() > 1 && p[1] != '/' && p[1] != '\\')) return p;
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home) return p;
    std::string h = home;
    if (p.size() > 1 && !h.empty() && (h.back() == '/' || h.back() == '\\')) h.pop_back();
    return h + p.substr(1);
}
inline std::string read_input(const std::string& path) { return path == "-" ? packtools::master::read_stdin_text() : read_text_utf8(path); }
// Python == for the scalars a JSON file can hold (1 == 1.0 == True)
inline bool py_eq(const Val& a, const Val& b) {
    const bool an = a.t == Val::INT || a.t == Val::FLOAT || a.t == Val::BOOL, bn = b.t == Val::INT || b.t == Val::FLOAT || b.t == Val::BOOL;
    if (an && bn) return a.num() == b.num();
    if (a.t != b.t) return false;
    if (a.t == Val::STR) return a.s == b.s;
    if (a.t == Val::NONE) return true;
    return dumps(a) == dumps(b);
}
inline std::string type_of(const Val& v) {
    switch (v.t) { case Val::NONE: return "NoneType"; case Val::BOOL: return "bool"; case Val::INT: return "int"; case Val::FLOAT: return "float";
        case Val::STR: return "str"; case Val::LIST: return "list"; default: return "dict"; }
}
inline const Val& getitem(const Val& d, const std::string& k) {   // d[k] with Python's errors
    if (d.t == Val::DICT) { const Val* v = d.d->get(k); if (!v) throw PyErr("KeyError", str_repr(k)); return *v; }
    if (d.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
    if (d.t == Val::STR) throw PyErr("TypeError", "string indices must be integers, not 'str'");
    throw PyErr("TypeError", "'" + type_of(d) + "' object is not subscriptable");
}
inline List iterate_py(const Val& v) {   // for x in v
    if (v.t == Val::LIST) return *v.l;
    if (v.t == Val::DICT) { List o; for (const auto& kv : v.d->items) o.push_back(Val::str(kv.first)); return o; }
    if (v.t == Val::STR) { List o; for (const auto& c : cps(v.s)) o.push_back(Val::str(c)); return o; }
    throw PyErr("TypeError", "'" + type_of(v) + "' object is not iterable");
}
inline void hashable(const Val& v) { if (v.t == Val::LIST || v.t == Val::DICT) throw PyErr("TypeError", "unhashable type: '" + type_of(v) + "'"); }
}  // namespace packtools::ig

// ---------------------------------------------------------------------------------------------- ig-caption/caption
namespace packtools::ig_caption {
using namespace pyrt;
using namespace packtools::ig;

inline const char* DOC = R"DOC(caption - lint an Instagram caption and show exactly what the feed shows before the "... more".

    astro-server tool ig-caption/caption caption.txt
    astro-server tool ig-caption/caption caption.txt --keywords "proposal software,client contract"
    astro-server tool ig-caption/caption - < caption.txt
    astro-server tool ig-caption/caption caption.txt --json

Instagram gives a caption about 125 characters in the feed and hides the rest behind a tap. This prints the
visible window as a box, then checks the eight things that are worth checking. The 125-character cut is an
approximation; change it with --truncate.
)DOC";

constexpr long long LIMIT = 2200;          // Instagram's hard caption limit
constexpr long long TRUNCATE = 125;        // roughly where the feed cuts to "... more"
constexpr long long HASHTAG_LIMIT = 5;     // Instagram's cap per post or reel since 18 Dec 2025

inline const char* HASHTAG_RE = R"((?:^|\s)(#[A-Za-z0-9_]+))";
inline const char* MENTION_RE = R"((?:^|\s)(@[A-Za-z0-9_.]+))";
inline const char* LINK_RE = R"(https?://\S+|\bwww\.\S+|\b[a-z0-9-]+\.(?:com|co|io|net|org|ai|app)/\S*)";   // re.IGNORECASE
/* [\U0001F300-\U0001FAFF U+2600-U+27BF U+2190-U+21FF U+FE0F] */
inline const std::string EMOJI_RE = std::string("[\\U0001F300-\\U0001FAFF") + "\xE2\x98\x80" "-" "\xE2\x9E\xBF" "\xE2\x86\x90" "-" "\xE2\x87\xBF" "\xEF\xB8\x8F" "]";

inline const std::vector<std::pair<std::string, std::string>>& asks() {
    static const std::vector<std::pair<std::string, std::string>> A = {
        {R"((?i)\bcomment (?:the word |\")?[A-Z0-9]{2,}\b)", "comment a keyword"},
        {R"((?i)\b(?:dm|message) me\b)", "DM me"},
        {R"((?i)\bsave (?:this|it)\b)", "save this"},
        {R"((?i)\bshare (?:this|it)\b)", "share this"},
        {R"((?i)\bfollow (?:me|for)\b)", "follow"},
        {R"((?i)\blink in (?:my )?bio\b)", "link in bio"},
        {R"((?i)\b(?:swipe|tap) (?:through|left|right|for|to)\b)", "swipe or tap"},
        {R"((?i)\btell me\b|\bwhat would you\b|\bwhich one\b)", "answer a question"},
    };
    return A;
}
inline const std::set<std::string> FILLER_TAGS = {"#viral", "#fyp", "#explore", "#explorepage", "#foryou", "#foryoupage",
    "#trending", "#instagood", "#love", "#follow", "#like4like", "#reels", "#reelsinstagram", "#viralreels", "#instadaily"};

inline long cut_l(long long cut) { return static_cast<long>(std::max(-(1LL << 30), std::min(cut, 1LL << 30))); }
inline std::string visible_window(const std::string& text, long long cut) {   // Instagram cuts mid-word, so this does too
    const std::string flat = strip(text);
    return static_cast<long long>(cplen(flat)) <= cut ? flat : cpslice(flat, 0, cut_l(cut));
}
// textwrap's whitespace munging (expand_tabs, replace_whitespace) before pyrt's wrap
inline std::string munge(const std::string& s) {
    std::string o; std::size_t col = 0;
    for (const auto& c : cps(s)) {
        if (c == "\t") { const std::size_t n = 8 - col % 8; o += std::string(n, ' '); col += n; }
        else if (c == "\n" || c == "\r") { o += ' '; col = 0; }
        else if (c == "\x0b" || c == "\x0c") { o += ' '; col++; }
        else { o += c; col++; }
    }
    return o;
}
inline Val strs(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(Val::str(s)); return l; }

inline Val analyse(const std::string& text0, long long cut, const std::vector<std::string>& kw_raw) {
    const std::string stripped = strip(rstrip(text0));
    const long long chars = static_cast<long long>(cplen(stripped));
    const std::size_t nl = stripped.find('\n');
    const std::string first_line = strip(nl == std::string::npos ? stripped : stripped.substr(0, nl));
    const long long flen = static_cast<long long>(cplen(first_line));
    const auto tags = re::findall(HASHTAG_RE, stripped);
    const auto mentions = re::findall(MENTION_RE, stripped);
    const auto links = re::compile(LINK_RE, re::I).findall(stripped);
    const auto emoji = re::findall(EMOJI_RE, stripped);
    const std::string window = visible_window(stripped, cut);
    const bool truncated = chars > cut;
    std::vector<std::string> ask; for (const auto& [pat, name] : asks()) if (re::search(pat, stripped)) ask.push_back(name);
    std::vector<std::string> filler; for (const auto& t : tags) if (FILLER_TAGS.count(lower(t))) filler.push_back(t);
    std::vector<std::string> keywords; for (const auto& k : kw_raw) if (!strip(k).empty()) keywords.push_back(strip(k));

    Val checks = Val::list();
    auto add = [&](const std::string& name, const std::string& status, const std::string& detail) {
        Val c = Val::dict(); c.d->set("check", Val::str(name)); c.d->set("status", Val::str(status)); c.d->set("detail", Val::str(detail)); checks.l->push_back(c);
    };
    const std::string L = std::to_string(LIMIT);
    add("LENGTH", chars > LIMIT ? "FAIL" : "PASS", std::to_string(chars) + " / " + L + " characters" + (chars > LIMIT ? ", " + std::to_string(chars - LIMIT) + " over the limit" : ""));

    if (first_line.empty()) add("FIRST LINE", "FAIL", "the caption opens on a blank line");
    else if (starts(first_line, "#") || starts(first_line, "@")) add("FIRST LINE", "FAIL", "opens on a hashtag or a mention, which is the one position worth a sentence");
    else if (flen > cut)
        add("FIRST LINE", "WARN", std::to_string(flen) + " characters, so it gets cut at " + std::to_string(cut) + " mid-thought. Fine if the cut is a cliffhanger, bad if it is a subordinate clause");
    else add("FIRST LINE", "PASS", std::to_string(flen) + " characters, lands whole");

    const auto conc = re::compile(CONCRETE_RE, re::M).findall(window);
    add("HOOK IS CONCRETE", conc.empty() ? "WARN" : "PASS",
        std::to_string(conc.size()) + " number(s) or name(s) in the visible window" + (conc.empty() ? " - nothing checkable before the tap" : ""));

    const long long nt = static_cast<long long>(tags.size()), nf = static_cast<long long>(filler.size());
    if (nt > HASHTAG_LIMIT) add("HASHTAGS", "FAIL", std::to_string(nt) + " tags, over Instagram's cap of " + std::to_string(HASHTAG_LIMIT) + ". Tags past the fifth do not count and the block reads as old");
    else if (nt == HASHTAG_LIMIT && nf) add("HASHTAGS", "WARN", std::to_string(nt) + " tags, at the cap, and " + std::to_string(nf) + " of them generic. Spend the five on topics");
    else if (nf) {
        std::vector<std::string> f3(filler.begin(), filler.begin() + static_cast<long>(std::min<std::size_t>(3, filler.size())));
        add("HASHTAGS", "WARN", std::to_string(nt) + " tags, " + std::to_string(nf) + " of them generic (" + join(f3, ", ") + "). Those describe nothing");
    } else add("HASHTAGS", "PASS", std::to_string(nt) + " tag(s)" + (nt ? ": " + join(tags, " ") : ""));

    if (tags.empty()) add("TAG PLACEMENT", "PASS", "no tags to place");
    else {
        bool in = false;
        for (const auto& t : tags) { const std::string pat = R"((?:^|\s))" + re::escape(t) + R"(\b)"; if (re::search(pat, window)) { in = true; break; } }
        if (in) add("TAG PLACEMENT", "WARN", "a hashtag is inside the visible window, spending feed space on a label");
        else add("TAG PLACEMENT", "PASS", "tags are below the fold");
    }

    add("LINKS", links.empty() ? "PASS" : "WARN",
        links.empty() ? std::string("no dead links in the body") : std::to_string(links.size()) + " link(s) in the caption, and captions are not clickable. Move it to the bio or the DM");

    if (ask.size() == 1) add("ONE ASK", "PASS", "one call to action: " + ask[0]);
    else if (ask.empty()) add("ONE ASK", "WARN", "no call to action. Decide what this post is for");
    else add("ONE ASK", "WARN", std::to_string(ask.size()) + " asks (" + join(ask, ", ") + "). Two asks is the same as none");

    const double density = static_cast<double>(emoji.size() * 100) / static_cast<double>(std::max<long long>(chars, 1));
    add("EMOJI", density > 4 ? "WARN" : "PASS", std::to_string(emoji.size()) + " emoji, " + fmt_f(density, 1) + " per 100 characters" + (density > 4 ? " - reads as decoration" : ""));

    if (!keywords.empty()) {
        const std::string low = lower(stripped), wlow = lower(window);
        std::vector<std::string> found, missing, in_window;
        for (const auto& k : keywords) (low.find(lower(k)) != std::string::npos ? found : missing).push_back(k);
        for (const auto& k : found) if (wlow.find(lower(k)) != std::string::npos) in_window.push_back(k);
        const std::string status = missing.empty() ? "PASS" : (found.empty() ? "FAIL" : "WARN");
        add("SEARCH TERMS", status, std::to_string(found.size()) + "/" + std::to_string(keywords.size()) + " present" +
            (found.empty() ? "" : ", " + std::to_string(in_window.size()) + " in the visible window") + (missing.empty() ? "" : ". Missing: " + join(missing, ", ")));
    }

    long long fails = 0, warns = 0;
    for (const auto& c : *checks.l) { const std::string s = get(c, "status").s; if (s == "FAIL") fails++; else if (s == "WARN") warns++; }
    const std::string verdict = fails ? "FIX" : (warns ? "REVIEW" : "READY");

    Val a = Val::dict();
    a.d->set("characters", Val::integer(chars)); a.d->set("limit", Val::integer(LIMIT)); a.d->set("truncate_at", Val::integer(cut));
    a.d->set("visible", Val::str(window)); a.d->set("truncated", Val::boolean(truncated)); a.d->set("first_line_chars", Val::integer(flen));
    a.d->set("hashtags", strs(tags)); a.d->set("mentions", strs(mentions)); a.d->set("links", strs(links));
    a.d->set("emoji", Val::integer(static_cast<long long>(emoji.size()))); a.d->set("asks", strs(ask));
    a.d->set("checks", checks); a.d->set("verdict", Val::str(verdict));
    return a;
}

inline void render_box(const std::string& window, bool truncated, std::size_t width = 52) {
    print("\n  WHAT THE FEED SHOWS");
    print("  +" + std::string(width + 2, '-') + "+");
    std::vector<std::string> lines;
    std::size_t k = 0;
    for (;;) {   // window.split("\n")
        const std::size_t e = window.find('\n', k);
        const std::string raw = window.substr(k, e == std::string::npos ? std::string::npos : e - k);
        auto w = ap::tw::wrap(munge(raw), static_cast<int>(width));
        if (w.empty()) lines.push_back(""); else lines.insert(lines.end(), w.begin(), w.end());
        if (e == std::string::npos) break;
        k = e + 1;
    }
    for (std::size_t i = 0; i < lines.size() && i < 8; ++i) print("  | " + ljust(lines[i], width) + " |");
    const std::string tail = truncated ? "... more" : "(whole caption fits)";
    print("  +" + std::string(width + 2 - tail.size() - 2, '-') + " " + tail + " " + "+");
}
inline void render(const Val& a) {
    const std::string dot = " \xC2\xB7 ";
    const std::string head = "CAPTION LINT " + dot + " " + std::to_string(get(a, "characters").i) + " / " + std::to_string(get(a, "limit").i) + " chars " + dot + " " +
                             std::to_string(get(a, "hashtags").size()) + " hashtags " + dot + " " + std::to_string(get(a, "asks").size()) + " ask(s)";
    print("\n" + head);
    print(std::string(std::max<std::size_t>(cplen(head), 62), '='));
    render_box(get(a, "visible").s, truthy(get(a, "truncated")));
    print("");
    for (const auto& c : *get(a, "checks").l) print("  " + ljust(get(c, "status").s, 5) + " " + ljust(get(c, "check").s, 17) + " " + get(c, "detail").s);
    print(std::string(std::max<std::size_t>(cplen(head), 62), '-'));
    print("  VERDICT  " + get(a, "verdict").s + "\n");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("caption", "Lint an Instagram caption.");
    p.pos("input").nargs("?").def("-").help("caption file, or - for stdin");
    p.opt({"--truncate"}).integer().def(TRUNCATE).help("characters shown before '... more' (default 125)");
    p.opt({"--keywords"}).def("").help("comma-separated terms you want to be found for");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    const Val tv = get(a, "truncate");
    if (tv.big()) throw PyErr("OverflowError", "int too large for this build");
    const std::string raw = read_input(S(a, "input"));
    const Val r = analyse(raw, tv.i, split(S(a, "keywords"), ","));
    if (B(a, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(r, o)); }
    else render(r);
    return get(r, "verdict").s == "READY" ? 0 : 1;
}
PACKTOOL("ig-caption/caption", main);
}  // namespace packtools::ig_caption

// ---------------------------------------------------------------------------------------------- ig-reel/beats
namespace packtools::ig_beats {
using namespace pyrt;
using namespace packtools::ig;

inline const char* SENT_RE = R"([^.!?]+[.!?]*)";
inline const std::set<std::string> STOPWORDS = {
    "the", "a", "an", "and", "or", "but", "if", "of", "to", "in", "on", "for",
    "with", "that", "this", "it", "is", "are", "was", "were", "be", "been",
    "you", "your", "i", "my", "me", "we", "our", "they", "them", "he", "she",
    "so", "just", "not", "no", "do", "did", "does", "have", "has", "had",
    "will", "can", "at", "as", "by", "from", "out", "up", "off", "one", "all",
};
constexpr double HOOK_WINDOW = 3.0;   // seconds; past this, the thumb has already decided
constexpr double MAX_BEAT = 4.0;      // seconds on one idea with no change on screen
constexpr int ABSTRACT_RUN = 3;       // beats in a row with nothing checkable in them

inline std::string pretty(const std::string& token) { return re::sub(R"((\d)(?=(\d{3})+$))", R"(\1,)", token); }
inline std::string tc(double seconds) {
    const auto [m, s] = py_divmod(seconds, 60.0);
    const long long mi = py_trunc(m);
    return std::to_string(mi) + ":" + pad0(fmt_f(s, 1), 4);
}
inline double per(double n, double wps) { if (wps == 0.0) throw PyErr("ZeroDivisionError", "float division by zero"); return n / wps; }

inline std::vector<std::string> split_beats(const std::string& raw, double wps) {   // one line is one beat, unless a line is too long to be one
    std::vector<std::string> beats;
    for (const auto& l : splitlines(raw)) {
        const std::string line = strip(l);
        if (line.empty()) continue;
        if (per(static_cast<double>(words(line).size()), wps) <= MAX_BEAT * 1.5) { beats.push_back(line); continue; }
        std::vector<std::string> parts;   // a long paragraph: break it at sentence ends
        for (const auto& pp : re::findall(SENT_RE, line)) if (!strip(pp).empty()) parts.push_back(strip(pp));
        std::string buf;
        for (const auto& part : parts) {
            const std::string candidate = strip(buf + " " + part);
            if (!buf.empty() && per(static_cast<double>(words(candidate).size()), wps) > MAX_BEAT) { beats.push_back(buf); buf = part; }
            else buf = candidate;
        }
        if (!buf.empty()) beats.push_back(buf);
    }
    return beats;
}

struct Row { long long n = 0, words = 0, concrete = 0; double start = 0, dur = 0; std::string text, label; std::vector<std::string> flags; };

inline Val analyse(const std::string& raw, const Val& wpm_v, const Val& target_v) {
    const double wps = to_float(wpm_v) / 60.0;
    const auto beats = split_beats(raw, wps);
    if (beats.empty()) return Val();
    std::vector<Row> rows; double clock = 0.0;
    for (std::size_t i = 0; i < beats.size(); ++i) {
        Row r; r.n = static_cast<long long>(i) + 1; r.words = static_cast<long long>(words(beats[i]).size());
        const double dur = per(static_cast<double>(r.words), wps);
        r.start = round_n(clock, 2); r.dur = round_n(dur, 2); r.text = beats[i];
        r.concrete = static_cast<long long>(re::compile(CONCRETE_RE, re::M).findall(beats[i]).size());
        rows.push_back(r); clock += dur;
    }
    const double total = clock;
    for (auto& r : rows) if (r.n == 1 || r.start + r.dur <= HOOK_WINDOW) r.label = "HOOK";
    rows.back().label = rows.back().label != "HOOK" ? "CTA" : "HOOK/CTA";
    const double half = total / 2;
    for (auto& r : rows) if (r.label.empty() && r.start <= half && half < r.start + r.dur) r.label = "MID";

    std::vector<std::string> notes;
    if (rows[0].dur > HOOK_WINDOW) {
        rows[0].flags.push_back("hook runs " + fmt_f(rows[0].dur, 1) + "s, past the 3s mark");
        notes.push_back("Beat 1 takes " + fmt_f(rows[0].dur, 1) + "s to say. Cut it to " + std::to_string(py_trunc(HOOK_WINDOW * wps)) +
                        " words or fewer, or the hook lands after the decision has been made.");
    }
    if (rows[0].concrete == 0) notes.push_back("Beat 1 has no number and no name in it. Hooks without something checkable are the ones that get scrolled.");
    for (auto& r : rows) if (r.dur > MAX_BEAT) r.flags.push_back(fmt_f(r.dur, 1) + "s on one beat");
    std::vector<std::string> long_beats; for (const auto& r : rows) if (r.dur > MAX_BEAT) long_beats.push_back(std::to_string(r.n));
    if (!long_beats.empty())
        notes.push_back("Beat(s) " + join(long_beats, ", ") + " run past 4s. Either split the line or change what is on screen inside it. A static frame is where people leave.");
    int run = 0; long long start = -1;
    for (const auto& r : rows) {
        if (r.concrete == 0) {
            run++;
            if (start < 0) start = r.n;
            if (run == ABSTRACT_RUN) notes.push_back("Beats " + std::to_string(start) + "-" + std::to_string(r.n) + " have nothing concrete in them. Put a number, a name or a price in one of them.");
        } else { run = 0; start = -1; }
    }
    // does the last line hand you back to the first one?
    auto content = [](const std::string& t) { std::set<std::string> s; for (const auto& w : words(t)) { const std::string l = lower(w); if (!STOPWORDS.count(l)) s.insert(l); } return s; };
    const auto first = content(rows[0].text), last = content(rows.back().text);
    std::vector<std::string> loop; for (const auto& w : first) if (last.count(w)) loop.push_back(pretty(w));
    std::sort(loop.begin(), loop.end());
    if (!loop.empty()) {
        std::vector<std::string> l3(loop.begin(), loop.begin() + static_cast<long>(std::min<std::size_t>(3, loop.size())));
        notes.push_back("Loops: the last beat repeats \"" + join(l3, ", ") + "\" from the hook. Second watches are free reach.");
    } else notes.push_back("No loop. The last beat shares no word with the hook, so the video ends flat. Echoing one word from beat 1 is the cheapest replay you get.");

    if (truthy(target_v)) {
        const double target = to_float(target_v), delta = total - target;
        if (std::fabs(delta) <= target * 0.1) notes.push_back("Length is on target (" + fmt_f(total, 1) + "s against " + pystr(target_v) + "s).");
        else if (delta > 0) notes.push_back(fmt_f(delta, 1) + "s over target. Cut about " + std::to_string(py_trunc(delta * wps)) + " words.");
        else notes.push_back(fmt_f(-delta, 1) + "s under target. Either add " + std::to_string(py_trunc(-delta * wps)) + " words or shoot it short. Short is usually right.");
    }

    Val a = Val::dict(); a.d->set("wpm", wpm_v); a.d->set("target", target_v);
    a.d->set("total_seconds", Val::flt(round_n(total, 2)));
    long long tw_ = 0; for (const auto& r : rows) tw_ += r.words;
    a.d->set("total_words", Val::integer(tw_));
    Val bl = Val::list();
    for (const auto& r : rows) {
        Val d = Val::dict(); d.d->set("n", Val::integer(r.n)); d.d->set("start", Val::flt(r.start)); d.d->set("dur", Val::flt(r.dur));
        d.d->set("words", Val::integer(r.words)); d.d->set("text", Val::str(r.text)); d.d->set("concrete", Val::integer(r.concrete));
        d.d->set("label", Val::str(r.label)); Val f = Val::list(); for (const auto& x : r.flags) f.l->push_back(Val::str(x)); d.d->set("flags", f);
        bl.l->push_back(d);
    }
    a.d->set("beats", bl);
    Val nl = Val::list(); for (const auto& n : notes) nl.l->push_back(Val::str(n)); a.d->set("notes", nl);
    return a;
}

inline void render(const Val& a) {
    const std::string dot = " \xC2\xB7 ";
    const Val target = get(a, "target");
    const std::string head = "BEAT SHEET " + dot + " " + std::to_string(get(a, "total_words").i) + " words " + dot + " ~" + fmt_f(get(a, "total_seconds").num(), 1) + "s at " +
                             pystr(get(a, "wpm")) + " wpm" + (truthy(target) ? " " + dot + " target " + pystr(target) + "s" : "");
    print("\n" + head);
    print(std::string(std::max<std::size_t>(cplen(head), 72), '='));
    for (const auto& r : *get(a, "beats").l) {
        const std::string lab = get(r, "label").s;
        const std::string label = lab.empty() ? std::string(8, ' ') : ljust(lab, 8);
        print("  " + tc(get(r, "start").num()) + "  " + rjust(fmt_f(get(r, "dur").num(), 1), 4) + "s  " + label + get(r, "text").s);
        for (const auto& f : *get(r, "flags").l) print("  " + std::string(6, ' ') + "  " + std::string(5, ' ') + "  " + std::string(8, ' ') + "^ " + f.s);
    }
    print(std::string(std::max<std::size_t>(cplen(head), 72), '-'));
    for (const auto& n : *get(a, "notes").l) print("  - " + n.s);
    print("");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("beats", "Time a Reel script into a beat sheet.");
    p.pos("input").nargs("?").def("-").help("script file, or - for stdin");
    p.opt({"--wpm"}).number().def(165).help("speaking rate (default 165)");
    p.opt({"--target"}).number().help("target length in seconds");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    const std::string raw = read_input(S(args, "input"));
    const Val a = analyse(raw, get(args, "wpm"), get(args, "target"));
    if (a.is_none()) { eprint("empty script"); return 2; }
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(a, o)); }
    else render(a);
    return 0;
}
PACKTOOL("ig-reel/beats", main);
}  // namespace packtools::ig_beats

// ---------------------------------------------------------------------------------------------- ig-reel/hookscore
namespace packtools::ig_hook {
using namespace pyrt;
namespace mh = packtools::master_hookscore;
using packtools::ig::read_input;

struct Run { std::map<std::string, mh::Res> results; double overall = 0; std::string verdict; std::vector<std::string> flags; };
inline Run run(const std::string& text) {   // the calibrated panel, as in master-virality/hookscore
    Run r;
    r.results["LENGTH"] = mh::check_length(text); r.results["SPECIFICITY"] = mh::check_specificity(text); r.results["STAKES"] = mh::check_stakes(text);
    r.results["FRONTLOAD"] = mh::check_frontload(text); r.results["ADDRESS"] = mh::check_address(text);
    for (const auto& [pat, msg] : mh::DEALBREAKERS) if (re::search(pat, text)) r.flags.push_back(msg);
    double sum = 0, mn = 1e300; for (const auto& c : mh::CHECKS) { sum += r.results[c].first; mn = std::min(mn, r.results[c].first); }
    // the weakest property caps the hook: one bad property is enough for the thumb to keep moving
    r.overall = mh::clamp((sum / 5.0) * 0.6 + mn * 0.4 - static_cast<double>(r.flags.size()) * 15);
    r.verdict = r.overall >= 70 && mn >= 55 && r.flags.empty() ? "STRONG" : r.overall >= 50 ? "OK" : "WEAK";
    return r;
}
inline std::string weakest(const Run& r) { std::string w = mh::CHECKS[0]; for (const auto& c : mh::CHECKS) if (r.results.at(c).first < r.results.at(w).first) w = c; return w; }

inline void render_one(const std::string& text, const Run& r) {
    print("\nHOOK SCORE"); print(std::string(62, '='));
    print("  \"" + strip(text) + "\"\n");
    for (const auto& name : mh::CHECKS) {
        const auto& [score, detail] = r.results.at(name);
        print("  " + ljust(name, 13) + " " + mh::bar(score) + " " + rjust(fmt_f(score, 1), 5));
        print("  " + std::string(13, ' ') + " " + detail);
    }
    print(std::string(62, '-'));
    print("  " + ljust("HOOK SCORE", 13) + " " + mh::bar(r.overall) + " " + rjust(fmt_f(r.overall, 1), 5) + "   " + r.verdict);
    for (const auto& f : r.flags) print("\n  DEALBREAKER  " + f);
    if (r.verdict != "STRONG") print("\n  Weakest property: " + weakest(r) + ". Fix that one and re-run.");
    print("");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("hookscore", "Score a Reel hook on five properties.");
    p.pos("input").nargs("?").def("-").help("file with one hook per line, or -");
    p.opt({"--hook"}).help("score a single hook given on the command line");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    std::vector<std::string> lines;
    if (!S(a, "hook").empty()) lines.push_back(S(a, "hook"));
    else for (const auto& l : splitlines(read_input(S(a, "input")))) if (!strip(l).empty()) lines.push_back(strip(l));
    if (lines.empty()) { eprint("nothing to score"); return 2; }

    struct P { std::string hook; Run r; double score; std::string weakest; Val v; };
    std::vector<P> payload;
    for (const auto& line : lines) {
        P x; x.hook = line; x.r = run(line); x.score = round_n(x.r.overall, 1); x.weakest = weakest(x.r);
        Val d = Val::dict(); d.d->set("hook", Val::str(line));
        Val ch = Val::dict();
        for (const auto& c : mh::CHECKS) { Val e = Val::dict(); e.d->set("score", Val::flt(round_n(x.r.results[c].first, 1))); e.d->set("detail", Val::str(x.r.results[c].second)); ch.d->set(c, e); }
        d.d->set("checks", ch); d.d->set("weakest", Val::str(x.weakest));
        Val fl = Val::list(); for (const auto& f : x.r.flags) fl.l->push_back(Val::str(f));
        d.d->set("flags", fl); d.d->set("score", Val::flt(x.score)); d.d->set("verdict", Val::str(x.r.verdict));
        x.v = d; payload.push_back(x);
    }
    if (B(a, "json")) {
        DumpOpts o; o.indent = 2; o.ensure_ascii = false;
        if (payload.size() > 1) { Val l = Val::list(); for (const auto& x : payload) l.l->push_back(x.v); print(dumps(l, o)); }
        else print(dumps(payload[0].v, o));
        return 0;
    }
    if (payload.size() == 1) render_one(lines[0], payload[0].r);
    else {
        std::vector<P> rows = payload;
        std::stable_sort(rows.begin(), rows.end(), [](const P& x, const P& y) { return -x.score < -y.score; });
        print("\nHOOK RANKING\n" + std::string(78, '='));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const P& r = rows[i];
            const std::string hook = cplen(r.hook) <= 62 ? r.hook : cpslice(r.hook, 0, 59) + "...";
            print(std::string(i == 0 ? "->" : "  ") + " " + rjust(fmt_f(r.score, 1), 5) + " " + ljust(r.r.verdict, 7) + " " + hook);
            print("        weakest: " + r.weakest + " (" + fmt_f(round_n(r.r.results.at(r.weakest).first, 1), 0) + ")");
            for (const auto& f : r.r.flags) print("        dealbreaker: " + f);
        }
        print("\nShoot the top one. If the top one is under 50, none of these are the hook.\n");
    }
    double best = -1e300; for (const auto& x : payload) best = std::max(best, x.score);
    return best >= 70 ? 0 : 1;
}
PACKTOOL("ig-reel/hookscore", main);
}  // namespace packtools::ig_hook

// ---------------------------------------------------------------------------------------------- ig-viral/swipe
namespace packtools::ig_swipe {
using namespace pyrt;
using namespace packtools::ig;

struct Formula { Val id, name; std::string match; };
using Formulas = std::optional<std::vector<Formula>>;

inline Formulas load_formulas(const std::string& path) {
    Val d;
    try { d = loads(read_text_utf8(path)); }
    catch (const PyErr&) { return std::nullopt; }   // a missing or broken file: formulas are not named
    std::vector<std::pair<Val, Val>> by_id;   // {h["id"]: h}, Python dict semantics
    auto find = [&](const Val& k) -> Val* { for (auto& kv : by_id) if (py_eq(kv.first, k)) return &kv.second; return nullptr; };
    for (const auto& h : iterate_py(getitem(d, "hooks"))) {
        const Val id = getitem(h, "id"); hashable(id);
        if (Val* v = find(id)) *v = h; else by_id.push_back({id, h});
    }
    List order;
    const Val* co = d.d->get("classify_order");
    if (co && truthy(*co)) order = iterate_py(*co);
    else {   // sorted(by_id)
        for (const auto& kv : by_id) order.push_back(kv.first);
        auto num = [](const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; };
        for (std::size_t k = 1; k < order.size(); ++k) {
            const Val &x = order[k], &y = order[k - 1];
            if (!(num(x) && num(y)) && !(x.t == Val::STR && y.t == Val::STR))
                throw PyErr("TypeError", "'<' not supported between instances of '" + type_of(x) + "' and '" + type_of(y) + "'");
        }
        std::stable_sort(order.begin(), order.end(), [&](const Val& x, const Val& y) { return x.t == Val::STR ? x.s < y.s : x.num() < y.num(); });
    }
    std::vector<Formula> out;
    for (const auto& i : order) {
        hashable(i);
        const Val* h = find(i);
        if (!h) continue;
        const Val m = getitem(*h, "match");
        Formula f{getitem(*h, "id"), getitem(*h, "name"), ""};
        if (m.t != Val::STR) throw PyErr("TypeError", "first argument must be string or compiled pattern");
        f.match = m.s;
        re::compile(f.match, re::I);   // a bad pattern fails here, as re.compile does
        out.push_back(f);
    }
    return out;
}
inline std::pair<Val, Val> classify(const std::string& hook, const Formulas& formulas) {
    if (!formulas || formulas->empty()) return {Val(), Val::str("unclassified")};
    for (const auto& f : *formulas) if (re::compile(f.match, re::I).search(hook)) return {f.id, f.name};
    return {Val(), Val::str("unclassified")};
}

inline Val digits_int(const std::string& s) {   // int(re.sub(r"[^\d]", "", s) or 0); None when there are no digits
    std::string d;
    for (uint32_t c : ucase_detail::u32(s)) if (uprop::decimal(c)) d += static_cast<char>('0' + packtools::master::digit_value(c));
    if (d.empty()) return Val();
    const std::size_t nz = d.find_first_not_of('0');
    d = nz == std::string::npos ? "0" : d.substr(nz);
    if (d.size() > 18) throw PyErr("OverflowError", "int too large for this build");
    return Val::integer(std::stoll(d));
}
inline std::vector<Val> read_rows(const std::string& path) {
    std::vector<std::string> lines;
    for (const auto& l : splitlines(read_input(path))) if (!strip(l).empty() && !starts(packtools::master::lstrip_ws(l), "#")) lines.push_back(l);
    if (lines.empty()) return {};
    std::vector<std::string> head; for (const auto& c : split(lines[0], "\t")) head.push_back(lower(strip(c)));
    std::vector<std::string> cols; std::size_t from = 0;
    if (std::find(head.begin(), head.end(), "views") != head.end() && std::find(head.begin(), head.end(), "hook") != head.end()) { cols = head; from = 1; }
    else cols = {"account", "followers", "views", "hook"};
    std::vector<Val> rows;
    for (std::size_t li = from; li < lines.size(); ++li) {
        auto cells = split(lines[li], "\t");
        if (cells.size() < cols.size()) cells.resize(cols.size(), "");
        Val r = Val::dict();
        for (std::size_t k = 0; k < cols.size() && k < cells.size(); ++k) r.d->set(cols[k], Val::str(strip(cells[k])));
        const Val* vw = r.d->get("views");
        const Val views = digits_int(vw ? pystr(*vw) : "");
        r.d->set("views", views.is_none() ? Val::integer(0) : views);
        for (const char* k : {"followers", "median"}) { const Val* v = r.d->get(k); r.d->set(k, digits_int(v ? pystr(*v) : "")); }
        const Val* hk = r.d->get("hook");
        if (get(r, "views").i && hk && truthy(*hk)) rows.push_back(r);
    }
    return rows;
}
inline Val med(const std::vector<Val>& items, const std::string& key) {   // round(statistics.median(vals), 1)
    std::vector<Val> vals; for (const auto& i : items) { const Val* v = i.d->get(key); if (v && !v->is_none()) vals.push_back(*v); }
    if (vals.empty()) return Val();
    std::stable_sort(vals.begin(), vals.end(), [](const Val& x, const Val& y) { return x.num() < y.num(); });
    const std::size_t n = vals.size();
    if (n % 2) { const Val& m = vals[n / 2]; return m.t == Val::FLOAT ? Val::flt(round_n(m.f, 1)) : m; }   // round(int, 1) stays an int
    const Val &x = vals[n / 2 - 1], &y = vals[n / 2];
    double v;
    if (x.t == Val::INT && y.t == Val::INT) v = static_cast<double>(x.i + y.i) / 2.0;   // int / 2 is true division
    else v = (x.num() + y.num()) / 2.0;
    return Val::flt(round_n(v, 1));
}

inline Val analyse(std::vector<Val> rows, const Formulas& formulas) {
    bool used_median = false; for (const auto& r : rows) if (truthy(get(r, "median"))) used_median = true;
    for (auto& r : rows) {
        const Val m = get(r, "median"), f = get(r, "followers");
        const long long base = truthy(m) ? m.i : truthy(f) ? f.i : 0;
        r.d->set("baseline", Val::integer(base));
        r.d->set("outlier", base ? Val::flt(round_n(static_cast<double>(get(r, "views").i) / static_cast<double>(base), 2)) : Val());
        const std::string hook = get(r, "hook").s;
        const auto [fid, name] = classify(hook, formulas);
        r.d->set("formula_id", fid); r.d->set("formula", name);
        r.d->set("words", Val::integer(static_cast<long long>(re::findall(WORD_RE, hook).size())));
        const auto s = packtools::ig_hook::run(hook);
        r.d->set("hook_score", Val::flt(round_n(s.overall, 1))); r.d->set("hook_verdict", Val::str(s.verdict));
    }
    auto key = [](const Val& r) { const Val o = get(r, "outlier"); return -(truthy(o) ? o.num() : 0.0); };
    std::vector<Val> ranked = rows;
    std::stable_sort(ranked.begin(), ranked.end(), [&](const Val& x, const Val& y) { return key(x) < key(y); });
    const std::size_t third = std::max<std::size_t>(1, ranked.size() / 3);
    const std::vector<Val> top(ranked.begin(), ranked.begin() + static_cast<long>(third)), bottom(ranked.end() - static_cast<long>(third), ranked.end());
    std::vector<std::pair<Val, long long>> counts;
    for (const auto& r : top) {
        const Val f = get(r, "formula"); bool hit = false;
        for (auto& kv : counts) if (py_eq(kv.first, f)) { kv.second++; hit = true; break; }
        if (!hit) counts.push_back({f, 1});
    }
    std::stable_sort(counts.begin(), counts.end(), [](const auto& x, const auto& y) { return -x.second < -y.second; });
    std::set<std::string> accounts; for (const auto& r : ranked) { const Val* a = r.d->get("account"); accounts.insert(a ? pystr(*a) : ""); }
    long long uncl = 0; for (const auto& r : ranked) { const Val f = get(r, "formula"); if (f.t == Val::STR && f.s == "unclassified") uncl++; }

    Val a = Val::dict();
    a.d->set("baseline", Val::str(used_median ? "account median" : "follower count"));
    a.d->set("n", Val::integer(static_cast<long long>(ranked.size())));
    a.d->set("accounts", Val::integer(static_cast<long long>(accounts.size())));
    a.d->set("reels", Val::list(ranked));
    Val tf = Val::list(); for (const auto& [f, c] : counts) { Val p = Val::list(); p.l->push_back(f); p.l->push_back(Val::integer(c)); tf.l->push_back(p); }
    a.d->set("top_formulas", tf);
    a.d->set("top_hook_score", med(top, "hook_score")); a.d->set("bottom_hook_score", med(bottom, "hook_score"));
    a.d->set("top_words", med(top, "words")); a.d->set("bottom_words", med(bottom, "words"));
    a.d->set("unclassified", Val::integer(uncl));
    return a;
}

inline std::string acct(const Val& r) { const Val* a = r.d->get("account"); return a ? pystr(*a) : ""; }
inline std::string fmt_id(const Val& id) {   // f"#{id:<2}"
    if (id.t == Val::BOOL) return "#" + ljust(id.b ? "1" : "0", 2);
    return "#" + ljust(pystr(id), 2);
}
inline void render(const Val& a) {
    const std::string dot = " \xC2\xB7 ";
    const std::string head = "SWIPE FILE " + dot + " " + std::to_string(get(a, "n").i) + " reels " + dot + " " + std::to_string(get(a, "accounts").i) + " accounts " + dot +
                             " baseline: " + get(a, "baseline").s;
    print("\n" + head);
    print(std::string(std::max<std::size_t>(cplen(head), 78), '='));
    for (const auto& r : *get(a, "reels").l) {
        const Val o = get(r, "outlier"), hs = get(r, "hook_score"), fid = get(r, "formula_id");
        const std::string mult = truthy(o) ? fmt_f(o.num(), 1) + "x" : "   ?";
        const std::string score = hs.is_none() ? " -" : fmt_f(hs.num(), 0);
        const std::string id = truthy(fid) ? fmt_id(fid) : "-  ";
        print("  " + rjust(mult, 7) + "  hook " + rjust(score, 3) + "  " + id + " " + ljust(cpslice(pystr(get(r, "formula")), 0, 22), 22) + " " +
              ljust(cpslice(acct(r), 0, 16), 16) + " " + rjust(commas(std::to_string(get(r, "views").i)), 9));
        print("           \"" + cpslice(get(r, "hook").s, 0, 96) + "\"");
    }
    print(std::string(std::max<std::size_t>(cplen(head), 78), '-'));
    print("WHAT IS WORKING IN THIS BATCH");
    const Val tf = get(a, "top_formulas");
    if (tf.size()) {
        std::vector<std::string> parts;
        for (std::size_t k = 0; k < tf.l->size() && k < 4; ++k) parts.push_back(pystr((*tf.l)[k][0]) + " x" + pystr((*tf.l)[k][1]));
        print("  top third by outlier:  " + join(parts, ", "));
    }
    if (!get(a, "top_hook_score").is_none())
        print("  median hook score:     top " + fmt_f(get(a, "top_hook_score").num(), 0) + "  vs bottom " + fmt_f(get(a, "bottom_hook_score").num(), 0));
    print("  median hook length:    top " + pystr(get(a, "top_words")) + " words  vs bottom " + pystr(get(a, "bottom_words")) + " words");
    print("  unclassified:          " + std::to_string(get(a, "unclassified").i) + " of " + std::to_string(get(a, "n").i) +
          ". Read those by hand, they are where a formula you do not have yet is hiding.");
    print("\n  A hand-collected batch is evidence, not proof. Twelve reels shows you nothing;\n  forty across six accounts shows you something. Collect more before you believe it.\n");
}
inline std::string to_markdown(const Val& a) {
    std::vector<std::string> lines = {"# Swipe file", "",
        std::to_string(get(a, "n").i) + " reels across " + std::to_string(get(a, "accounts").i) + " accounts. Ranked by multiple over " + get(a, "baseline").s + ".", ""};
    for (const auto& r : *get(a, "reels").l) {
        const Val o = get(r, "outlier");
        const std::string mult = truthy(o) ? fmt_f(o.num(), 1) + "x" : "?";
        lines.push_back("## " + mult + "  " + pystr(get(r, "formula")) + "  (" + acct(r) + ")");
        lines.push_back("- views: " + commas(std::to_string(get(r, "views").i)) + "  baseline: " + commas(std::to_string(get(r, "baseline").i)));
        lines.push_back("- hook score: " + pystr(get(r, "hook_score")) + "  words: " + pystr(get(r, "words")));
        lines.push_back("- hook: \"" + get(r, "hook").s + "\"");
        lines.push_back("");
    }
    return join(lines, "\n") + "\n";
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("swipe", "Rank collected reels by outlier multiple.");
    p.pos("input").nargs("?").def("-").help("TSV file, or - for stdin");
    p.opt({"--hooks"}).def(Val::str(skill_file("../ig-reel/hook-formulas.json"))).help("path to ig-reel/hooks.json");
    p.opt({"--out"}).help("also write the swipe file as markdown here");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    const auto rows = read_rows(S(args, "input"));
    if (rows.empty()) { eprint("no usable rows. Need a tab-separated file with at least views and hook."); return 2; }
    const Formulas formulas = load_formulas(S(args, "hooks"));
    const Val a = analyse(rows, formulas);
    if (!formulas || formulas->empty()) eprint("note: hooks.json not found, formulas not named. Pass --hooks.");
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(a, o)); }
    else render(a);
    const std::string outp = S(args, "out");
    if (!outp.empty()) {
        const std::string path = expanduser(outp);
        const std::string dir = dirname(path);
        flush();
        if (dir.empty()) throw OSError(ENOENT, "");   // os.makedirs("") - Python fails the same way
        makedirs(dir);
        write_text(path, to_markdown(a));
        eprint("wrote " + path);
    }
    return 0;
}
PACKTOOL("ig-viral/swipe", main);
}  // namespace packtools::ig_swipe
