// x-agent-skill helpers (Jake Schincariol, MIT), ported to C++: same output, same exit codes as the Python originals.
//   x-human/detect      the five-check AI detection panel, tuned for short posts (checks that cannot judge sit out)
//   x-human/humanize    master_humanize with x's emoji-joiner and flag protection (social_youtube.inc registers it)
//   x-post/count        count a post the way X counts it (twitter-text v3 weights) and lint it
//   x-post/hookscore    score the first line of an X post
//   x-thread/thread     cut a draft into a thread that fits
//   x-plan/weights      X's published For You ranking weights (live from GitHub, or the snapshot)
//   x-viral/pull        X post URLs -> a swipe sheet via X's oEmbed API
//   x-audit/audit, x-viral/swipe live in social_x2.inc.

namespace packtools::xs {
using namespace pyrt;

// The hook of a post is its first non-empty line (hookscore.py's first_line; count.py's returns "" in the same case).
inline std::string first_line(const std::string& post) {
    for (const auto& line : splitlines(post)) if (!strip(line).empty()) return strip(line);
    return strip(post);
}
inline std::string rstrip_chars(const std::string& s, const std::string& chars) {   // str.rstrip(chars)
    const auto set = ucase_detail::u32(chars); auto v = to_u32(s);
    while (!v.empty() && std::find(set.begin(), set.end(), v.back()) != set.end()) v.pop_back();
    return from_u32(v);
}
inline std::string http_reason(long code) {
    static const std::map<long, std::string> R = {{400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"},
        {408, "Request Timeout"}, {410, "Gone"}, {429, "Too Many Requests"}, {500, "Internal Server Error"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}, {504, "Gateway Timeout"}};
    const auto it = R.find(code);
    return it == R.end() ? "" : it->second;
}
struct HttpError : std::runtime_error {   // urllib.error.HTTPError
    long code;
    explicit HttpError(long c) : std::runtime_error("HTTP Error " + std::to_string(c) + ": " + http_reason(c)), code(c) {}
};
struct NetError : std::runtime_error { using std::runtime_error::runtime_error; };   // urllib.error.URLError
// urllib.request.urlopen(Request(url, headers={"User-Agent": ua}), timeout=t).read().decode("utf-8")
inline std::string url_get(const std::string& url, const std::string& ua, long timeout) {
    const HttpResp r = http_get(url, {"User-Agent: " + ua}, timeout);
    if (r.status >= 400) throw HttpError(r.status);
    if (!r.error.empty() || r.status == 0) throw NetError("<urlopen error " + (r.error.empty() ? std::string("no response") : r.error) + ">");
    if (const auto e = utf8_decode_error(r.body)) throw PyErr("UnicodeDecodeError", *e);
    return r.body;
}
inline std::string err_text(const std::exception& e) {   // str(e)
    (void)0;
    return e.what();
}
inline std::string int_list(const std::vector<long long>& v) {   // str([1, 2])
    std::vector<std::string> s; for (const long long x : v) s.push_back(std::to_string(x));
    return "[" + join(s, ", ") + "]";
}
inline Val check(const std::string& status, const std::string& name, const std::string& msg) {
    Val c = Val::dict(); c.d->set("status", Val::str(status)); c.d->set("check", Val::str(name)); c.d->set("detail", Val::str(msg));
    return c;
}
}  // namespace packtools::xs

// --------------------------------------------------------------------------------------------- x-human/detect
namespace packtools::x_detect {
using namespace pyrt;
namespace md = packtools::master_detect;
namespace pm = packtools::master;
namespace mh = packtools::master_humanize;

inline const char* WORD_RE = R"([A-Za-z'’]+)";   // both apostrophes: a phone types the curly one
inline const char* CONTRACTIONS = R"(\b\w+['’](?:s|t|re|ve|ll|d|m)\b)";
struct Res { bool na = false; double v = 0; bool intv = false; std::string detail; };   // intv: Python kept an int (clamp(100 - 50))
inline Res judged(double v, std::string d) { Res r; r.v = v; r.detail = std::move(d); return r; }
inline Res sat_out(std::string d) { Res r; r.na = true; r.detail = std::move(d); return r; }
inline std::vector<std::string> words(const std::string& text) { return re::findall(WORD_RE, text); }

inline Res check_burstiness(const std::string& text) {   // humans vary sentence length hard; models write even
    std::vector<long long> lens; for (const auto& s : md::sentences(text)) lens.push_back(static_cast<long long>(split_ws(s).size()));
    if (lens.size() < 4) return sat_out("n/a: " + std::to_string(lens.size()) + " sentence(s), needs 4 to measure variation");
    const double mean = pm::int_mean(lens);
    const double cv = mean != 0.0 ? pm::int_pstdev(lens) / mean : 0.0;
    return judged(md::scale(cv, 0.70, 0.22), "variation " + fmt_f(cv, 2) + " across " + std::to_string(lens.size()) + " sentences (want 0.55+)");
}
inline Res check_specificity(const std::string& text) {   // numbers, names and concrete nouns
    const auto w = words(text);
    if (w.size() < 25) return sat_out("n/a: " + std::to_string(w.size()) + " words, needs 25 for a density");
    const double per100 = 100.0 / static_cast<double>(w.size());
    std::set<std::string> propers; for (const auto& p : re::findall(md::PROPER, text, re::M)) propers.insert(p);
    const long long hits = static_cast<long long>(re::findall(md::NUMBERS, text).size() + propers.size());
    const double density = static_cast<double>(hits) * per100;
    return judged(md::scale(density, 6.0, 0.5), std::to_string(hits) + " concrete markers, " + fmt_f(density, 1) + " per 100 words (want 4+)");
}
inline Res check_slop(const std::string& text, const Val& lex) {   // stock vocabulary density against the lexicon
    const auto w = words(text);
    if (w.empty()) return judged(50.0, "empty");
    long long hits = 0; std::vector<std::string> found;
    List entries = pm::iterate(get(lex, "words")); for (const auto& e : pm::iterate(get(lex, "phrases"))) entries.push_back(e);
    for (const auto& e : entries) {
        const std::string find = pm::re_text(get(e, "find"));
        const long long n = static_cast<long long>(re::findall(pm::slop_pattern(find), text, re::I).size());
        if (n) { hits += n; found.push_back(find); }
    }
    const double density = static_cast<double>(hits * 100) / static_cast<double>(w.size());
    std::string detail = std::to_string(hits) + " stock terms, " + fmt_f(density, 1) + " per 100 words";
    if (!found.empty()) {
        std::vector<std::string> s = found; std::sort(s.begin(), s.end()); s.resize(std::min<std::size_t>(4, s.size()));
        detail += " (" + join(s, ", ") + (found.size() > 4 ? ", ..." : "") + ")";
    }
    return judged(md::scale(density, 0.0, 4.0), detail);
}
inline Res check_fingerprint(const std::string& text) {   // characters a keyboard does not produce on its own
    const std::string bare = re::sub(mh::X_FLAG_TAG, "", re::sub(mh::x_emoji_zwj(), "", text));
    long long invisible = 0; for (const uint32_t c : ucase_detail::u32(bare)) if (std::string(uni::category(c)) == "Cf") ++invisible;
    const long long em = static_cast<long long>(md::count_sub(text, "\xE2\x80\x94"));
    long long curly = 0; for (const char* c : {"\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D"}) curly += static_cast<long long>(md::count_sub(text, c));
    const long long ellip = static_cast<long long>(md::count_sub(text, "\xE2\x80\xA6"));
    long long nbsp = 0; for (const char* c : {"\xC2\xA0", "\xE2\x80\xAF", "\xE2\x80\x89"}) nbsp += static_cast<long long>(md::count_sub(text, c));
    // curly quotes weigh a quarter: iOS Smart Punctuation types them by default, so on their own they say very little
    const double total = static_cast<double>(invisible * 4 + em * 2) + static_cast<double>(curly) * 0.25 + static_cast<double>(ellip) + static_cast<double>(nbsp);
    const double per1k = total * 1000 / static_cast<double>(std::max<std::size_t>(cplen(text), 1));
    return judged(md::scale(per1k, 0.0, 12.0), std::to_string(invisible) + " invisible, " + std::to_string(em) + " em dash, " + std::to_string(curly) + " curly quote, " +
                                               std::to_string(ellip) + " ellipsis, " + std::to_string(nbsp) + " hard space");
}
inline std::string first4(const std::vector<std::string>& names) {
    std::vector<std::string> h(names.begin(), names.begin() + static_cast<long>(std::min<std::size_t>(4, names.size())));
    return " [" + join(h, ", ") + "]";
}
inline Res check_voice(const std::string& text, const Val& lex) {   // contractions, person, and the shapes models default to
    const auto w = words(text);
    const bool shrt = w.size() < 25;
    const double per100 = 100.0 / static_cast<double>(std::max<std::size_t>(w.size(), 1));
    const double contractions = static_cast<double>(re::findall(CONTRACTIONS, text, re::I).size()) * per100;
    const double person = static_cast<double>(re::findall(R"(\b(i|me|my|mine|we|us|our|you|your)\b)", text, re::I).size()) * per100;
    long long tells = 0; std::vector<std::string> names;
    for (const auto& s : pm::iterate(get(lex, "structures"))) {
        long long n;
        try { n = static_cast<long long>(re::compile(pm::re_text(get(s, "regex")), re::M).findall(text).size()); }
        catch (const PyErr& e) { if (e.type != "re.error") throw; continue; }
        if (n) { tells += n; names.push_back(pystr(get(s, "id"))); }
    }
    if (shrt) {   // contraction and pronoun rates are noise in one sentence; structural tells are not
        std::string detail = "short post: structural tells only, " + std::to_string(tells) + " found";
        if (!names.empty()) detail += first4(names);
        const long long n = 100 - tells * 50;
        Res r = judged(md::clamp(static_cast<double>(n)), detail);
        r.intv = n > 0 && n < 100;   // max(0.0, min(100.0, n)) hands back the int itself
        return r;
    }
    std::vector<long long> bullets; for (const auto& b : re::findall(R"((?m)^\s*[-*•]\s+(.+)$)", text)) bullets.push_back(static_cast<long long>(split_ws(b).size()));
    const bool uniform = bullets.size() >= 3 && pm::int_pstdev(bullets) < 1.6;
    double score = md::scale(contractions, 3.0, 0.0) * 0.35 + md::scale(person, 8.0, 1.0) * 0.35 + md::clamp(static_cast<double>(100 - tells * 22)) * 0.30;
    if (uniform) { score -= 12; names.push_back("uniform-bullets"); }
    std::string detail = fmt_f(contractions, 1) + " contractions, " + fmt_f(person, 1) + " personal pronouns per 100 words, " + std::to_string(tells) + " structural tell(s)";
    if (!names.empty()) detail += first4(names);
    return judged(md::clamp(score), detail);
}

struct Run { std::map<std::string, Res> results; double overall = 0; std::string verdict; };
inline Run run(const std::string& text, const Val& lex) {
    Run r;
    r.results["BURSTINESS"] = check_burstiness(text); r.results["SPECIFICITY"] = check_specificity(text); r.results["SLOP DENSITY"] = check_slop(text, lex);
    r.results["FINGERPRINT"] = check_fingerprint(text); r.results["VOICE"] = check_voice(text, lex);
    std::vector<double> scores; for (const auto& c : md::CHECKS) if (!r.results[c].na) scores.push_back(r.results[c].v);
    const double mn = *std::min_element(scores.begin(), scores.end());
    // the weakest check drags the verdict; checks that could not judge (n/a) sit out rather than count as 50
    r.overall = pm::stat_mean(scores) * 0.6 + mn * 0.4;
    r.verdict = r.overall >= 70 && mn >= 55 ? "PASS" : r.overall >= 50 ? "REVIEW" : "FLAGGED";
    return r;
}
inline void render(const Run& r, const std::string& label) {
    const std::string title = "AI DETECTION PANEL" + (label.empty() ? std::string() : "  -  " + label);
    print("\n" + title);
    print(std::string(std::max<std::size_t>(cplen(title), 62), '='));
    for (const auto& name : md::CHECKS) {
        const Res& x = r.results.at(name);
        if (x.na) print("  " + ljust(name, 13) + " " + std::string(24, '-') + "   n/a");
        else print("  " + ljust(name, 13) + " " + md::bar(x.v) + " " + md::f51(x.v));
        print("  " + std::string(13, ' ') + " " + x.detail);
    }
    print(std::string(62, '-'));
    print("  " + ljust("HUMAN SCORE", 13) + " " + md::bar(r.overall) + " " + md::f51(r.overall) + "   " + r.verdict);
    long long out = 0; for (const auto& c : md::CHECKS) if (r.results.at(c).na) ++out;
    if (out) print("  " + std::string(13, ' ') + " short post: scored on " + std::to_string(5 - out) + " of 5 checks");
    if (r.verdict != "PASS") {
        std::string weakest;
        for (const auto& c : md::CHECKS) { const Res& x = r.results.at(c); if (!x.na && (weakest.empty() || x.v < r.results.at(weakest).v)) weakest = c; }
        print("\n  Weakest signal: " + weakest + ". Fix that first.");
    }
    print("");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("detect", "Score how machine-written a draft looks.");
    p.pos("input").nargs("?").def("-").help("file, or - for stdin");
    p.pos("compare").nargs("?").help("second file, to show before/after");
    p.opt({"--json"}).store_true();
    p.opt({"--lexicon"}).def(Val::str(skill_file("slop.json")));
    const Val a = parse_args(p, argv);
    const Val lex = loads(read_text_utf8(S(a, "lexicon")));
    auto read = [](const std::string& path) { return path == "-" ? pm::read_stdin_text() : read_text_utf8(path); };
    std::vector<std::pair<std::string, std::string>> targets;
    targets.emplace_back(S(a, "input"), read(S(a, "input")));
    const std::string compare = S(a, "compare");
    if (!compare.empty()) targets.emplace_back(compare, read(compare));
    std::vector<Run> runs; Val payload = Val::list();
    for (const auto& [name, text] : targets) {
        const Run r = run(text, lex); runs.push_back(r);
        Val d = Val::dict(); d.d->set("source", Val::str(name));
        Val ch = Val::dict();
        for (const auto& c : md::CHECKS) {
            const Res& x = r.results.at(c);
            Val e = Val::dict();
            e.d->set("score", x.na ? Val() : x.intv ? Val::integer(static_cast<long long>(x.v)) : Val::flt(round_n(x.v, 1)));
            e.d->set("detail", Val::str(x.detail)); ch.d->set(c, e);
        }
        d.d->set("checks", ch); d.d->set("human_score", Val::flt(round_n(r.overall, 1))); d.d->set("verdict", Val::str(r.verdict));
        payload.l->push_back(d);
    }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(compare.empty() ? payload[0] : payload, o)); return 0; }
    for (std::size_t k = 0; k < targets.size(); ++k) render(runs[k], compare.empty() ? "" : basename(targets[k].first));
    if (!compare.empty()) {
        const double x = get(payload[0], "human_score").f, y = get(payload[1], "human_score").f;
        print("  " + fmt_f(x, 1) + " " + get(payload[0], "verdict").s + "  ->  " + fmt_f(y, 1) + " " + get(payload[1], "verdict").s + "   (" + fmt_fs(y - x, 1) + ")\n");
    }
    return get(payload[payload.size() - 1], "verdict").s == "PASS" ? 0 : 1;
}
PACKTOOL("x-human/detect", main);
}  // namespace packtools::x_detect

// ----------------------------------------------------------------------------------------------- x-post/count
namespace packtools::x_count {
using namespace pyrt;

inline constexpr long long LIMIT = 280, PREMIUM_LIMIT = 25000, FOLD = 280, URL_WEIGHT = 23, HASHTAG_LIMIT = 1;
inline bool light(uint32_t cp) { return cp <= 4351 || (cp >= 8192 && cp <= 8205) || (cp >= 8208 && cp <= 8223) || (cp >= 8242 && cp <= 8247); }   // twitter-text v3
inline const std::string TLDS = "com|net|org|io|ai|co|dev|app|me|xyz|gg|so|tv|us|uk|ca|de|fr|es|it|nl|se|no|au|nz|in|"
                                "jp|ly|sh|to|fm|info|biz|edu|gov|news|blog|site|tech|studio|design|link|page|pro|"
                                "store|shop|club|live|world|today|online|space|cloud|run|build|ink|is|am|be|cc|ws";
inline const std::string& url_re() {
    static const std::string p = std::string(R"P(https?://[^\s<>\"]+|(?<![@\w.-])(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+(?:)P") + TLDS + R"P()\b(?::\d+)?(?:/[^\s<>\"]*)?)P";
    return p;
}
inline const std::string TRAILING = ".,!?:;)]}'\"";
inline const std::string& emoji_re() {
    static const std::string picto = R"(\U0001F000-\U0001FAFF\u2600-\u27BF\u2300-\u23FF\u2B00-\u2BFF\u2190-\u21FF\u3030\u303D\u3297\u3299\u00A9\u00AE\u2122\u2139)";
    static const std::string mod = R"(\U0001F3FB-\U0001F3FF)";
    static const std::string p = std::string(R"([\U0001F1E6-\U0001F1FF]{2})")   // flags
        + R"(|[0-9#*]\uFE0F?\u20E3)"                                                  // keycaps
        + R"(|\U0001F3F4[\U000E0020-\U000E007E]+\U000E007F)"                          // England etc.
        + "|[" + picto + "]\\uFE0F?[" + mod + "]?(?:\\u200D[" + picto + "]\\uFE0F?[" + mod + "]?)*";
    return p;
}
inline const char* HASHTAG_RE = R"((?:^|(?<=\s))#[A-Za-z0-9_]+)";
inline const char* MENTION_RE = R"((?:^|(?<=[\s(]))@[A-Za-z0-9_]{1,15}\b)";
inline const char* BAIT_RE = R"((?i)\b(?:thoughts|agree|am i wrong|who else)\?\s*$|\b(?:rt|repost|retweet|like) if\b|\bfollow (?:me )?for more\b)";
inline const char* LABEL_RE = R"((?i)^\s*(?:a\s+)?(?:(?:mega|short|quick|long|new)\s+)?(?:thread|\U0001F9F5)\b|^\s*\U0001F9F5)";

struct Url { long a = 0, b = 0; std::string u; };
inline std::vector<Url> find_urls(const std::string& text) {
    std::vector<Url> o;
    for (const auto& m : re::compile(url_re(), re::I).finditer(text)) {
        const std::string u = xs::rstrip_chars(m.group(0), TRAILING);
        o.push_back({m.start(), m.start() + static_cast<long>(cplen(u)), u});
    }
    return o;
}
struct Weighted { long long total = 0, emoji = 0; std::vector<std::string> urls, heavy; };
inline Weighted weighted(const std::string& text0) {
    const std::string text = uni::normalize("NFC", text0);
    const auto urls = find_urls(text);
    const U32 u = to_u32(text);
    std::map<long, long> url_at; for (const auto& x : urls) url_at[x.a] = x.b;
    const re::Regex& em = re::compile(emoji_re());
    Weighted w; std::size_t i = 0;
    while (i < u.size()) {
        const auto it = url_at.find(static_cast<long>(i));
        if (it != url_at.end()) { w.total += URL_WEIGHT; i = static_cast<std::size_t>(it->second); continue; }
        const re::Match m = em.match_at(u, i);
        if (m && m.end() > static_cast<long>(i)) { w.total += 2; w.emoji++; i = static_cast<std::size_t>(m.end()); continue; }
        if (light(u[i])) w.total += 1;
        else { w.total += 2; w.heavy.push_back(utf8(u[i])); }
        ++i;
    }
    for (const auto& x : urls) w.urls.push_back(x.u);
    return w;
}
inline std::string visible_prefix(const std::string& text, long long budget) {   // the longest prefix that fits in budget
    const U32 u = to_u32(text);
    std::size_t lo = 0, hi = u.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (weighted(from_u32(u, 0, mid)).total <= budget) lo = mid; else hi = mid - 1;
    }
    return from_u32(u, 0, lo);
}
inline Val strs(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(Val::str(s)); return l; }

inline Val analyse(const std::string& raw, bool premium) {
    const std::string text = uni::normalize("NFC", strip(raw));
    const long long limit = premium ? PREMIUM_LIMIT : LIMIT;
    const Weighted d = weighted(text);
    const long long total = d.total;
    Val checks = Val::list();
    auto add = [&](const std::string& st, const std::string& name, const std::string& msg) { checks.l->push_back(xs::check(st, name, msg)); };
    const long long over = total - limit;
    if (over > 0) add("FAIL", "LENGTH", std::to_string(total) + " / " + std::to_string(limit) + ", " + std::to_string(over) + " over. X will not let you post it.");
    else if (total > LIMIT)
        add("WARN", "LENGTH", std::to_string(total) + " / " + std::to_string(limit) + " (Premium long post). The timeline shows about the first " + std::to_string(FOLD) +
                              ", then \"Show more\". Everything that matters goes above the fold.");
    else add("PASS", "LENGTH", std::to_string(total) + " / " + std::to_string(limit) + ", " + std::to_string(limit - total) + " left");

    std::string hook; for (const auto& line : splitlines(text)) if (!strip(line).empty()) { hook = strip(line); break; }
    const long long hw = weighted(hook).total;
    if (re::search(LABEL_RE, hook)) add("WARN", "FIRST LINE", "opens on \"thread\". That is packaging, not a hook. Lead with the payoff.");
    else if (hw > 110) add("WARN", "FIRST LINE", std::to_string(hw) + " characters. It wraps to a third line on a phone before the point lands.");
    else add("PASS", "FIRST LINE", std::to_string(hw) + " characters, reads in one glance");

    if (!d.urls.empty()) {
        bool in_hook = false; for (const auto& u : d.urls) if (hook.find(u) != std::string::npos) in_hook = true;
        add(in_hook ? "WARN" : "NOTE", "LINKS",
            std::to_string(d.urls.size()) + " link(s), " + std::to_string(URL_WEIGHT) + " characters each. " +
            (in_hook ? "One is in the first line, which hands the reader a way out before the post lands. " : "") +
            "X says it no longer down-ranks link posts (July 2026). An independent analysis in January 2026 still measured fewer views on them. "
            "Your own /x-audit will tell you which is true for you.");
    } else add("PASS", "LINKS", "none");

    const auto tags = re::findall(HASHTAG_RE, text);
    if (static_cast<long long>(tags.size()) > HASHTAG_LIMIT) {
        std::vector<std::string> t5(tags.begin(), tags.begin() + static_cast<long>(std::min<std::size_t>(5, tags.size())));
        add("WARN", "HASHTAGS", std::to_string(tags.size()) + ": " + join(t5, " ") + ". Each is a link out of your own post. "
            "Elon Musk, Dec 2024: the system doesn't need them anymore. Keep one, or none.");
    } else add("PASS", "HASHTAGS", std::to_string(tags.size()) + (tags.empty() ? "" : ": " + tags[0]));

    const auto mentions = re::findall(MENTION_RE, text);
    std::vector<std::string> late;
    if (total > LIMIT && !mentions.empty()) {
        const std::string head = visible_prefix(text, LIMIT);
        for (const auto& m : mentions) if (head.find(m) == std::string::npos) late.push_back(m);
    }
    if (!late.empty()) add("WARN", "MENTIONS", join(late, " ") + " sits past character " + std::to_string(LIMIT) + ". X does not notify a "
                                               "mention that far into a long post. Move it up if you want them to see it.");
    else add("PASS", "MENTIONS", std::to_string(mentions.size()));

    if (re::search(BAIT_RE, text)) add("WARN", "ENDING", "ends on reflex bait (\"Thoughts?\", \"RT if\"). Ask a question only this post could ask, or end on the last true sentence.");
    else add("PASS", "ENDING", "no reflex bait");

    const std::size_t em = packtools::master_detect::count_sub(text, "\xE2\x80\x94");
    if (em) add("WARN", "EM DASH", std::to_string(em) + ". Run it through /x-human before it goes anywhere.");

    Val a = Val::dict();
    a.d->set("weighted", Val::integer(total)); a.d->set("limit", Val::integer(limit)); a.d->set("fits", Val::boolean(total <= limit));
    a.d->set("urls", strs(d.urls)); a.d->set("emoji", Val::integer(d.emoji)); a.d->set("double_weight_chars", Val::integer(static_cast<long long>(d.heavy.size())));
    a.d->set("fold", total > FOLD ? Val::str(visible_prefix(text, FOLD)) : Val());
    a.d->set("checks", checks);
    return a;
}
inline void render(const std::string& raw, const Val& a, std::size_t width = 52) {
    const std::string text = strip(raw);
    const long long wt = get(a, "weighted").i, limit = get(a, "limit").i;
    const Val fold = get(a, "fold");
    print(wt > limit ? "\n  WHAT FITS" : "\n  WHAT THE TIMELINE SHOWS");
    print("  +" + std::string(width + 2, '-') + "+");
    const std::string shown = fold.is_none() ? text : fold.s;
    std::vector<std::string> lines;
    std::size_t k = 0;
    for (;;) {   // shown.split("\n"), each line through textwrap.wrap(raw, width) or [""]
        const std::size_t e = shown.find('\n', k);
        const std::string part = shown.substr(k, e == std::string::npos ? std::string::npos : e - k);
        auto w = ap::tw::wrap(packtools::ig_caption::munge(part), static_cast<int>(width));
        if (w.empty()) lines.push_back(""); else lines.insert(lines.end(), w.begin(), w.end());
        if (e == std::string::npos) break;
        k = e + 1;
    }
    for (std::size_t i = 0; i < lines.size() && i < 14; ++i) print("  | " + ljust(lines[i], width) + " |");
    if (lines.size() > 14) print("  | " + ljust("...", width) + " |");
    std::string tail;
    if (wt > limit || (limit == LIMIT && !fold.is_none())) tail = "cut here, " + std::to_string(wt - limit) + " over";
    else if (!fold.is_none()) tail = "Show more";
    else tail = std::to_string(wt) + "/" + std::to_string(limit);
    const long long dashes = static_cast<long long>(width) + 2 - static_cast<long long>(cplen(tail)) - 2;
    print("  +" + std::string(static_cast<std::size_t>(std::max(0LL, dashes)), '-') + " " + tail + " " + "+");
    std::vector<std::string> extras;
    if (get(a, "urls").size()) extras.push_back(std::to_string(get(a, "urls").size()) + " link(s) at " + std::to_string(URL_WEIGHT));
    if (get(a, "emoji").i) extras.push_back(std::to_string(get(a, "emoji").i) + " emoji at 2");
    if (get(a, "double_weight_chars").i) extras.push_back(std::to_string(get(a, "double_weight_chars").i) + " other double-weight character(s)");
    if (!extras.empty()) print("  counted as: " + join(extras, ", "));
    print("");
    for (const auto& c : *get(a, "checks").l) print("  " + ljust(get(c, "status").s, 5) + " " + ljust(get(c, "check").s, 11) + " " + get(c, "detail").s);
    print("");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("count", "Count and lint an X post the way X counts it.");
    p.pos("input").nargs("?").def("-").help("file, or - for stdin");
    p.opt({"--premium"}).store_true().help("use the 25,000 Premium limit");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    const std::string raw = packtools::ig::read_input(S(args, "input"));
    if (strip(raw).empty()) { eprint("nothing to count"); return 2; }
    const Val a = analyse(raw, B(args, "premium"));
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(a, o)); }
    else render(raw, a);
    return get(a, "fits").b ? 0 : 1;
}
PACKTOOL("x-post/count", main);
}  // namespace packtools::x_count

// ------------------------------------------------------------------------------------------- x-post/hookscore
namespace packtools::x_hook {
using namespace pyrt;
namespace md = packtools::master_detect;
namespace pm = packtools::master;
using packtools::ig_swipe::Formulas;

inline const char* NUMBER_RE = R"(\$\s?\d[\d,]*(?:\.\d+)?k?|\b\d[\d,]*(?:\.\d+)?\s?(?:%|k\b|m\b|x\b|hrs?\b|hours?\b|mins?\b|minutes?\b|days?\b|weeks?\b|months?\b|years?\b)?)";   // re.I
inline const char* CAP_RE = R"(\b[A-Z][a-z]{2,}\b)";
inline const char* ACRONYM_RE = R"(\b(?:[A-Z]{2,6}|X)\b)";   // AI, GPT, MRR, and X itself
inline const std::set<std::string>& common() {   // a capital at the start of a sentence is grammar, not a name, unless the word is not an ordinary one
    static const std::set<std::string> s = [] {
        const char* w = "a about after again all also always an and another any are as at back be because been before "
            "being best better both but by can could day did do does doing done down each even ever every "
            "few first for from get gets getting go going good got had has have having he her here hers him "
            "his how however i if in into is it its just keep know last less let like little look lot made "
            "make many may me might more most much must my never new next no not now of off often on once "
            "one only or other our out over own people really right said same say see she should since so "
            "some something still such take than that the their them then there these they thing things "
            "think this those though through time to today too two under until up us use used very was way "
            "we well were what when where which while who why will with without would yes yet you your";
        std::set<std::string> o; for (const auto& x : split_ws(w)) o.insert(x); return o;
    }();
    return s;
}
inline const char* HASHTAG_RE = R"((?:^|\s)#\w+)";
inline const char* LINK_RE = R"(https?://\S+|\bwww\.\S+|\b[a-z0-9-]+\.(?:com|io|ai|co|dev|app|net|org|me|xyz|so|gg)\b(?:/\S*)?)";   // re.I
inline const std::set<std::string> SPELLED_NUMBERS = {"zero", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "fifteen",
    "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety", "hundred", "thousand", "million", "billion", "dozen", "half", "twice", "triple"};
inline const std::set<std::string> MONEY_WORDS = {"dollars", "dollar", "bucks", "grand", "percent", "cents", "revenue", "profit", "salary", "mrr", "arr", "followers",
    "views", "impressions"};
inline const std::set<std::string> STAKES = {   // words that put something on the line
    "stop", "never", "wrong", "mistake", "mistakes", "lost", "lose", "losing", "cost", "costs", "broke", "broken", "failed", "failure", "fail", "nobody",
    "no", "not", "don't", "dont", "doesn't", "didn't", "can't", "won't", "quit", "quitting", "fired", "deleted", "delete", "killed", "kills", "kill",
    "replaced", "replaces", "cut", "beat", "free", "paid", "charged", "hired", "saved", "first", "banned", "illegal", "worst", "hate", "hated", "wasted",
    "waste", "scam", "lie", "lied", "lying", "truth", "secret", "hidden", "stole", "stolen", "before", "until", "instead", "but", "except", "unless",
    "problem", "risk", "warning", "regret", "wish", "should", "shouldn't", "still", "already", "only", "without", "versus", "vs", "actually",
    "hottest", "new", "overrated", "underrated", "dead", "shipped", "launched", "released", "announced", "introducing", "breaking", "leaked"};
inline const char* LOOPS = R"((?i)\bhere'?s (?:what|why|how|the|my)\b|\bwhat happened\b|\bturns out\b|\bthe (?:truth|reason|part|catch)\b|\bnobody\b)"
                           R"(|\beveryone (?:says|thinks|tells)\b|\bmost people\b|\bunpopular opinion\b)"
                           R"(|\bused to\b[^.!?]*[.!?]?[^.!?]*\bnow\b|\bago\b[^.!?]*[.!?]?[^.!?]*\btoday\b)";
inline const std::vector<std::string> WEAK_OPENERS = {"so", "ok", "okay", "hey", "hi", "hello", "guys", "yo", "alright", "gm", "welcome", "today", "basically",
    "honestly", "look", "listen", "just", "let", "lets", "let's", "i wanted", "i want", "one of", "have you", "did you", "do you", "are you", "in this", "in today",
    "the thing", "a lot", "there is", "there are", "this is", "it is", "as a", "when it", "if you've", "you know", "thread", "a thread", "quick thread",
    "new thread", "friendly reminder", "reminder"};
inline const std::set<std::string> IMPERATIVES = {"stop", "steal", "copy", "delete", "try", "watch", "read", "save", "use", "build", "make", "write", "send", "take",
    "start", "quit", "never", "always", "don't", "dont", "do", "put", "run", "check", "bookmark", "unfollow", "mute", "go"};
struct Deal { std::string pattern; int flags; const char* msg; };
inline const std::vector<Deal>& dealbreakers() {
    static const std::vector<Deal> d = {
        {R"((?i)^\s*(?:a\s+)?(?:(?:mega|short|quick|long|new)\s+)?thread\b|^\s*\U0001F9F5)", 0,
         "Opens on the word \"thread\". That is packaging. Put the payoff in line 1 and let X's own UI say it is a thread."},
        {R"((?i)^\s*(?:stop scrolling|don'?t scroll))", 0, "Opens with \"stop scrolling\". Asking for attention proves you have not earned it."},
        {R"((?i)\b(?:in (?:this|today'?s) (?:thread|post|video)|i'?m going to (?:show|teach|walk) you|i'?ll show you how)\b)", 0, "Preamble. Delete it and open on the payoff."},
        {R"((?i)^\s*(?:hey |hi |gm\b|good morning\b|what'?s up |welcome ))", 0, "Greeting. Nobody opened X to be greeted."},
        {R"(^\s*\(?\d+\s*/\s*(?:\d+|n)\)?\s)", 0, "Opens on a thread counter. \"1/12\" in front of the hook spends the most expensive characters you have on a page number."},
        {LINK_RE, re::I, "Link in the hook. Put it in a reply. It costs 23 characters here and it gives the reader a way to leave."},
        {HASHTAG_RE, 0, "Hashtag in the hook. On X a hashtag is a link out of your own post."},
    };
    return d;
}
inline const std::vector<std::string> CHECKS = {"LENGTH", "SPECIFICITY", "STAKES", "FRONTLOAD", "ADDRESS"};

inline std::set<std::string> propers(const std::string& text) {   // capitalised words that are names, not sentence starts
    std::set<std::string> found;
    const U32 u = to_u32(text);
    for (const auto& m : re::compile(CAP_RE).finditer(u)) {
        const std::string before = rstrip(from_u32(u, 0, static_cast<std::size_t>(m.start())));
        bool sentence_start = before.empty();
        if (!sentence_start) {
            const uint32_t c = to_u32(before).back();
            for (const uint32_t s : ucase_detail::u32(".!?:\n\"\xE2\x80\x9C(")) if (c == s) sentence_start = true;
        }
        if (sentence_start && common().count(lower(m.group(0)))) continue;
        found.insert(m.group(0));
    }
    for (const auto& a : re::findall(ACRONYM_RE, text)) found.insert(a);
    return found;
}
inline std::vector<std::string> lows(const std::vector<std::string>& w) { std::vector<std::string> o; for (const auto& x : w) o.push_back(pm::strip_chars(lower(x), "'\xE2\x80\x99")); return o; }

inline md::Res check_length(const std::string& text) {   // a hook has to land in one glance: on a phone that is about one line
    const long long n = static_cast<long long>(packtools::ig::words(text).size());
    const long long chars = static_cast<long long>(cplen(strip(text)));
    double score;
    if (n >= 4 && n <= 14) score = 100.0;
    else if (n < 4) score = md::clamp(static_cast<double>(100 - (4 - n) * 20));
    else score = md::clamp(static_cast<double>(100 - (n - 14) * 9));
    if (chars > 110) score -= 12;   // wraps to a third line on a phone
    return {md::clamp(score), std::to_string(n) + " words, " + std::to_string(chars) + " chars (want 4-14 words, under 110 chars)"};
}
inline md::Res check_specificity(const std::string& text) {   // one concrete thing beats three abstract ones
    std::vector<std::string> nums; for (const auto& n : re::findall(NUMBER_RE, text, re::I)) if (!strip(n).empty()) nums.push_back(strip(n));
    const auto names = propers(text);
    std::vector<std::string> spelled; for (const auto& w : lows(packtools::ig::words(text))) if (SPELLED_NUMBERS.count(w) || MONEY_WORDS.count(w)) spelled.push_back(w);
    const long long hits = static_cast<long long>(nums.size() + names.size() + spelled.size());
    const double score = hits == 0 ? 15.0 : md::clamp(static_cast<double>(45 + hits * 30));
    std::vector<std::string> f;
    for (std::size_t k = 0; k < nums.size() && k < 2; ++k) f.push_back(nums[k]);
    std::size_t k = 0; for (const auto& n : names) { if (k++ >= 2) break; f.push_back(n); }   // std::set iterates in sorted order
    for (std::size_t j = 0; j < spelled.size() && j < 2; ++j) f.push_back(spelled[j]);
    const std::string found = join(f, ", ");
    return {score, std::to_string(hits) + " concrete marker(s)" + (!found.empty() ? ": " + found : " - no number, no name, nothing checkable")};
}
inline md::Res check_stakes(const std::string& text) {   // tension, cost, negation, novelty
    std::set<std::string> m; for (const auto& x : lows(packtools::ig::words(text))) if (STAKES.count(x)) m.insert(x);
    std::vector<std::string> markers(m.begin(), m.end());
    if (re::search(R"(\$\s?\d)", text)) markers.push_back("a price");
    if (re::search(LOOPS, text)) markers.push_back("an open loop");
    if (ends(rstrip(text), "?") && re::search(R"((?i)\byou(?:r|'d|'re|'ve)?\b)", text)) markers.push_back("a question only the reader can answer");
    const std::size_t n = markers.size();
    const double score = n == 0 ? 20.0 : n == 1 ? 70.0 : 100.0;
    std::vector<std::string> m4(markers.begin(), markers.begin() + static_cast<long>(std::min<std::size_t>(4, n)));
    return {md::clamp(score), std::to_string(n) + " tension marker(s)" + (n ? ": " + join(m4, ", ") : " - nothing is at stake in this line")};
}
inline md::Res check_frontload(const std::string& text) {   // the interesting word cannot be in position nine
    const auto w = packtools::ig::words(text);
    if (w.empty()) return {0.0, "empty"};
    const auto low = lows(w);
    const std::string opener = low.size() > 1 ? low[0] + " " + low[1] : low[0];
    double penalty = 0; std::string hit_opener;
    for (const auto& weak : WEAK_OPENERS)
        if (starts(opener, weak + " ") || opener == weak || low[0] == weak) { penalty = 30; hit_opener = weak; break; }
    long long payload = -1;
    const auto names = propers(text);
    static const std::set<std::string> QW = {"what", "which", "who", "why", "how", "when", "where"};
    if (ends(rstrip(text), "?") && QW.count(low[0])) payload = 0;   // a question leads with its own payload
    for (std::size_t i = 0; i < low.size() && payload < 0; ++i) {
        const std::string& t = low[i];
        if (STAKES.count(t) || SPELLED_NUMBERS.count(t) || MONEY_WORDS.count(t) || re::match(NUMBER_RE, w[i], re::I) || names.count(w[i])) payload = static_cast<long long>(i);
    }
    double base; std::string where;
    if (payload < 0) { base = 30.0; where = "no payload word anywhere in the line"; }
    else if (payload <= 3) { base = 100.0; where = "payload at word " + std::to_string(payload + 1); }
    else if (payload <= 6) { base = 70.0; where = "payload at word " + std::to_string(payload + 1) + ", could move forward"; }
    else { base = 40.0; where = "payload at word " + std::to_string(payload + 1) + ", too late"; }
    return {md::clamp(base - penalty), where + (hit_opener.empty() ? "" : "; weak opener \"" + hit_opener + "\"")};
}
inline md::Res check_address(const std::string& text) {   // aimed at one reader, or floating in the air
    const std::string low = lower(text);
    const auto w = lows(packtools::ig::words(text));
    if (re::search(R"(\b(you|your|you're|youre|yourself)\b)", low)) return {100.0, "speaks to the reader"};
    if (!w.empty() && IMPERATIVES.count(w[0])) return {90.0, "imperative opener (\"" + w[0] + "\")"};
    if (re::search(R"(\b(i|i'm|im|my|me|we|our)\b)", low)) return {75.0, "first person, no reader named"};
    if (w.size() <= 10) return {70.0, "short declarative, no reader named"};   // a short declarative is its own address
    return {35.0, "third person, nobody in the room"};
}

struct Run { std::map<std::string, md::Res> results; double overall = 0; std::string verdict; std::vector<std::string> flags; };
inline Run run(const std::string& text0) {
    const std::string text = xs::first_line(text0);
    Run r;
    r.results["LENGTH"] = check_length(text); r.results["SPECIFICITY"] = check_specificity(text); r.results["STAKES"] = check_stakes(text);
    r.results["FRONTLOAD"] = check_frontload(text); r.results["ADDRESS"] = check_address(text);
    for (const auto& d : dealbreakers()) if (re::compile(d.pattern, d.flags).search(text)) r.flags.push_back(d.msg);
    std::vector<double> scores; for (const auto& c : CHECKS) scores.push_back(r.results[c].first);
    const double mn = *std::min_element(scores.begin(), scores.end());
    // the weakest property caps the hook: one bad property is enough for the thumb to keep moving
    r.overall = md::clamp(pm::stat_mean(scores) * 0.6 + mn * 0.4 - static_cast<double>(r.flags.size()) * 15);
    r.verdict = r.overall >= 70 && mn >= 55 && r.flags.empty() ? "STRONG" : r.overall >= 50 ? "OK" : "WEAK";
    return r;
}
inline std::string weakest(const Run& r) { std::string w = CHECKS[0]; for (const auto& c : CHECKS) if (r.results.at(c).first < r.results.at(w).first) w = c; return w; }
inline std::string tag_id(const Val& fid) { return fid.t == Val::BOOL ? (fid.b ? "True" : "False") : pystr(fid); }

inline void render_one(const std::string& text, const Run& r, const Formulas& formulas) {
    const std::string line = xs::first_line(text);
    const auto [fid, fname] = packtools::ig_swipe::classify(line, formulas);
    print("\nHOOK SCORE");
    print(std::string(62, '='));
    print("  \"" + line + "\"");
    print("  formula: " + (truthy(fid) ? "#" + tag_id(fid) + " " : std::string()) + pystr(fname) + "\n");
    for (const auto& name : CHECKS) {
        const auto& [score, detail] = r.results.at(name);
        print("  " + ljust(name, 13) + " " + md::bar(score) + " " + md::f51(score));
        print("  " + std::string(13, ' ') + " " + detail);
    }
    print(std::string(62, '-'));
    print("  " + ljust("HOOK SCORE", 13) + " " + md::bar(r.overall) + " " + md::f51(r.overall) + "   " + r.verdict);
    for (const auto& f : r.flags) print("\n  DEALBREAKER  " + f);
    if (r.verdict != "STRONG") print("\n  Weakest property: " + weakest(r) + ". Fix that one and re-run.");
    print("");
}
inline Val score_payload(const std::string& line, const Formulas& formulas) {
    const Run r = run(line);
    const auto [fid, fname] = packtools::ig_swipe::classify(xs::first_line(line), formulas);
    Val p = Val::dict();
    p.d->set("hook", Val::str(xs::first_line(line))); p.d->set("formula_id", fid); p.d->set("formula", fname);
    Val ch = Val::dict();
    for (const auto& c : CHECKS) { Val x = Val::dict(); x.d->set("score", Val::flt(round_n(r.results.at(c).first, 1))); x.d->set("detail", Val::str(r.results.at(c).second)); ch.d->set(c, x); }
    p.d->set("checks", ch); p.d->set("weakest", Val::str(weakest(r)));
    Val fl = Val::list(); for (const auto& f : r.flags) fl.l->push_back(Val::str(f));
    p.d->set("flags", fl); p.d->set("score", Val::flt(round_n(r.overall, 1))); p.d->set("verdict", Val::str(r.verdict));
    return p;
}
inline void render_table(const std::vector<Val>& rows) {
    print("\nHOOK RANKING\n" + std::string(78, '='));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Val& r = rows[i];
        const std::string mark = i == 0 ? "->" : "  ";
        const std::string h = get(r, "hook").s;
        const std::string hook = cplen(h) <= 60 ? h : cpslice(h, 0, 57) + "...";
        print(mark + " " + md::f51(get(r, "score").f) + " " + ljust(get(r, "verdict").s, 7) + " " + hook);
        const Val fid = get(r, "formula_id");
        const std::string tag = truthy(fid) ? "#" + tag_id(fid) + " " + pystr(get(r, "formula")) : pystr(get(r, "formula"));
        const std::string wk = get(r, "weakest").s;
        print("        " + tag + "  \xC2\xB7  weakest: " + wk + " (" + fmt_f(get(get(get(r, "checks"), wk), "score").f, 0) + ")");
        for (const auto& f : *get(r, "flags").l) print("        dealbreaker: " + f.s);
    }
    print("\nPost the top one. If the top one is under 50, none of these is the hook.\n");
}
inline Formulas formulas() { return packtools::ig_swipe::load_formulas(skill_file("../x-post/hook-formulas.json")); }
inline int main(const std::vector<std::string>& argv) {
    const Formulas fs = formulas();   // hookscore.py loads hooks.json when it is imported, before it reads its arguments
    Parser p("hookscore", "Score the first line of an X post.");
    p.pos("input").nargs("?").def("-").help("file with one hook per line, or -");
    p.opt({"--hook"}).help("score a single hook given on the command line");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    std::vector<std::string> lines;
    if (!S(args, "hook").empty()) lines.push_back(S(args, "hook"));
    else for (const auto& l : splitlines(packtools::ig::read_input(S(args, "input")))) if (!strip(l).empty()) lines.push_back(strip(l));
    if (lines.empty()) { eprint("nothing to score"); return 2; }
    std::vector<Val> payload; for (const auto& l : lines) payload.push_back(score_payload(l, fs));
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(payload.size() > 1 ? Val::list(payload) : payload[0], o)); return 0; }
    if (payload.size() == 1) render_one(lines[0], run(lines[0]), fs);
    else {
        std::vector<Val> sorted = payload;
        std::stable_sort(sorted.begin(), sorted.end(), [](const Val& x, const Val& y) { return -get(x, "score").f < -get(y, "score").f; });
        render_table(sorted);
    }
    double best = get(payload[0], "score").f; for (const auto& x : payload) best = std::max(best, get(x, "score").f);
    return best >= 70 ? 0 : 1;
}
PACKTOOL("x-post/hookscore", main);
}  // namespace packtools::x_hook

// --------------------------------------------------------------------------------------------- x-thread/thread
namespace packtools::x_thread {
using namespace pyrt;

inline constexpr long long LIMIT = 280;
inline const char* SENT_RE = R"P([^.!?\n]+(?:[.!?]+["')\]]*|$)\s*)P";
inline const char* SEP_RE = R"((?m)^\s*-{3,}\s*$)";
inline const char* ASK_RE = R"((?i)\?|\b(?:follow|bookmark|repost|reply|comment|dm me|subscribe|join|grab|get the|link (?:is )?(?:below|in the reply)|try it|steal)\b)";

inline long long w(const std::string& text) { return x_count::weighted(text).total; }
inline std::vector<std::string> split_sentences(const std::string& par) {
    std::vector<std::string> parts; for (const auto& s : re::findall(SENT_RE, par)) if (!strip(s).empty()) parts.push_back(strip(s));
    if (parts.empty()) parts.push_back(strip(par));
    return parts;
}
inline std::vector<std::string> split_words(const std::string& sentence, long long limit) {
    std::vector<std::string> out; std::string cur;
    for (const auto& word : split_ws(sentence)) {
        const std::string trial = strip(cur + " " + word);
        if (w(trial) <= limit) cur = trial;
        else { if (!cur.empty()) out.push_back(cur); cur = word; }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
using Post = std::pair<std::string, bool>;   // (text, split mid-sentence)
inline std::vector<Post> fit(const std::string& par, long long limit) {   // one paragraph into as few posts as fit, breaking at sentences
    if (w(par) <= limit) return {{par, false}};
    std::vector<Post> posts; std::string cur;
    for (const auto& sent : split_sentences(par)) {
        const std::string trial = strip(cur + " " + sent);
        if (w(trial) <= limit) { cur = trial; continue; }
        if (!cur.empty()) { posts.push_back({cur, false}); cur.clear(); }
        if (w(sent) <= limit) cur = sent;
        else {
            const auto pieces = split_words(sent, limit);
            for (std::size_t k = 0; k + 1 < pieces.size(); ++k) posts.push_back({pieces[k], true});
            cur = pieces.back();
        }
    }
    if (!cur.empty()) posts.push_back({cur, false});
    return posts;
}
inline std::vector<Val> build(const std::string& raw, long long limit, bool number, bool pack) {
    const std::string text = strip(raw);
    const long long budget = limit - (number ? 7 : 0);   // "\n\n12/15" is 7 weighted characters
    std::vector<std::string> blocks; bool explicit_ = false;
    if (re::search(SEP_RE, text)) { for (const auto& b : re::split(SEP_RE, text)) if (!strip(b).empty()) blocks.push_back(strip(b)); explicit_ = true; }
    else for (const auto& b : re::split(R"(\n\s*\n)", text)) if (!strip(b).empty()) blocks.push_back(strip(b));
    std::vector<Post> posts;
    for (const auto& b : blocks) {
        if (explicit_ && w(b) <= budget) posts.push_back({b, false});
        else { const auto f = fit(b, budget); posts.insert(posts.end(), f.begin(), f.end()); }
    }
    if (pack && !explicit_) {
        std::vector<Post> merged;
        for (const auto& [p, cut] : posts) {
            if (!merged.empty() && !cut && !merged.back().second && w(merged.back().first + "\n\n" + p) <= budget) merged.back() = {merged.back().first + "\n\n" + p, false};
            else merged.push_back({p, cut});
        }
        posts = merged;
    }
    const std::size_t n = posts.size();
    std::vector<Val> out;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string body = number ? posts[i].first + "\n\n" + std::to_string(i + 1) + "/" + std::to_string(n) : posts[i].first;
        Val o = Val::dict();
        o.d->set("n", Val::integer(static_cast<long long>(i + 1))); o.d->set("text", Val::str(body));
        o.d->set("weighted", Val::integer(w(body))); o.d->set("split_mid_sentence", Val::boolean(posts[i].second));
        out.push_back(o);
    }
    return out;
}
inline Val lint(const std::vector<Val>& posts, long long limit) {
    Val checks = Val::list();
    auto add = [&](const std::string& st, const std::string& name, const std::string& msg) { checks.l->push_back(xs::check(st, name, msg)); };
    const long long n = static_cast<long long>(posts.size());
    if (n < 3) add("WARN", "LENGTH", std::to_string(n) + " post(s). That is a post, not a thread. Use /x-post, or a single long post if you have Premium.");
    else if (n > 15) add("WARN", "LENGTH", std::to_string(n) + " posts. Past about 15, fewer people reach the end than read post 2. Cut, or split it into two threads a week apart.");
    else add("PASS", "LENGTH", std::to_string(n) + " posts");

    std::vector<long long> over; for (const auto& p : posts) if (get(p, "weighted").i > limit) over.push_back(get(p, "n").i);
    if (!over.empty()) add("FAIL", "FITS", "post(s) " + xs::int_list(over) + " are over " + std::to_string(limit) + ". X will not post them.");
    else add("PASS", "FITS", "every post is " + std::to_string(limit) + " or under");

    std::string hook;
    if (!posts.empty()) {
        const auto ls = splitlines(strip(get(posts[0], "text").s));
        if (ls.empty()) throw PyErr("IndexError", "list index out of range");
        hook = ls[0];
    }
    if (re::search(x_count::LABEL_RE, hook))
        add("WARN", "POST 1", "opens on \"thread\". Post 1 is the only one most people read. Put the payoff in it and let X's own UI say it is a thread.");
    else if (w(hook) > 110) add("WARN", "POST 1", "first line is " + std::to_string(w(hook)) + " characters. It has to land in one glance.");
    else add("PASS", "POST 1", "first line " + std::to_string(w(hook)) + " characters. Score it: python3 ../x-post/hookscore.py --hook \"...\"");

    std::vector<long long> cuts; for (const auto& p : posts) if (get(p, "split_mid_sentence").b) cuts.push_back(get(p, "n").i);
    if (!cuts.empty()) add("WARN", "BREAKS", "post(s) " + xs::int_list(cuts) + " end mid-sentence because one sentence is longer than a post. Rewrite that sentence in two.");
    else add("PASS", "BREAKS", "every post ends on a sentence");

    std::vector<long long> mid; for (std::size_t k = 0; k + 1 < posts.size(); ++k) if (!x_count::find_urls(get(posts[k], "text").s).empty()) mid.push_back(get(posts[k], "n").i);
    if (!mid.empty()) add("NOTE", "LINKS", "link(s) in post(s) " + xs::int_list(mid) + ". A link before the last post is an exit before the payoff. "
                                           "Move it to the end unless the link IS the point.");
    else add("PASS", "LINKS", "none before the last post");

    if (!posts.empty() && !re::search(ASK_RE, get(posts.back(), "text").s))
        add("NOTE", "LAST POST", "no ask. The last post is the one people screenshot and the one they act on: a recap, then one next step. One.");
    else add("PASS", "LAST POST", "has an ask");

    long long heavy = 0; for (const auto& p : posts) if (static_cast<double>(get(p, "weighted").i) > static_cast<double>(limit) * 0.85) ++heavy;
    if (n >= 4 && static_cast<double>(heavy) > static_cast<double>(n) * 0.6)
        add("NOTE", "PACE", std::to_string(heavy) + " of " + std::to_string(n) + " posts are within 15% of the limit. Threads that get read to the end have short "
                            "posts in them. One idea per post, then stop.");
    return checks;
}
inline void render(const std::vector<Val>& posts, const Val& checks) {
    long long total = 0; for (const auto& p : posts) total += get(p, "weighted").i;
    print("\nTHREAD  \xC2\xB7  " + std::to_string(posts.size()) + " posts  \xC2\xB7  " + commas(std::to_string(total)) + " weighted characters");
    print(std::string(62, '='));
    for (const auto& p : posts) {
        const std::string mark = get(p, "split_mid_sentence").b ? "  ! split mid-sentence" : "";
        print("\n[" + std::to_string(get(p, "n").i) + "/" + std::to_string(posts.size()) + "]  " + std::to_string(get(p, "weighted").i) + "/" + std::to_string(LIMIT) + mark);
        print(get(p, "text").s);
    }
    print("\n" + std::string(62, '-'));
    for (const auto& c : *checks.l) print("  " + ljust(get(c, "status").s, 5) + " " + ljust(get(c, "check").s, 10) + " " + get(c, "detail").s);
    print("");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("thread", "Cut a draft into an X thread that fits.");
    p.pos("input").nargs("?").def("-").help("file, or - for stdin");
    p.opt({"--number"}).store_true().help("append i/n to each post");
    p.opt({"--pack"}).store_true().help("merge short paragraphs into one post");
    p.opt({"--limit"}).integer().def(LIMIT);
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    const std::string raw = packtools::ig::read_input(S(args, "input"));
    if (strip(raw).empty()) { eprint("nothing to thread"); return 2; }
    const long long limit = get(args, "limit").i;
    const auto posts = build(raw, limit, B(args, "number"), B(args, "pack"));
    const Val checks = lint(posts, limit);
    if (B(args, "json")) { Val o = Val::dict(); o.d->set("posts", Val::list(posts)); o.d->set("checks", checks); DumpOpts d; d.indent = 2; d.ensure_ascii = false; print(dumps(o, d)); }
    else render(posts, checks);
    for (const auto& c : *checks.l) if (get(c, "status").s == "FAIL") return 1;
    return 0;
}
PACKTOOL("x-thread/thread", main);
}  // namespace packtools::x_thread

// ---------------------------------------------------------------------------------------------- x-plan/weights
namespace packtools::x_weights {
using namespace pyrt;

inline const std::string REPO = "xai-org/x-algorithm";
inline const std::string PATH = "home-mixer/params/param.rs";
inline const std::string RAW = "https://raw.githubusercontent.com/" + REPO + "/main/" + PATH;
inline const std::string COMMITS = "https://api.github.com/repos/" + REPO + "/commits?path=" + PATH + "&per_page=1";
inline const char* UA = "x-agent-skill/1.0";
inline const std::vector<std::pair<std::string, std::string>> LABELS = {   // what the weights mean in plain words
    {"Favorite", "like"}, {"Reply", "reply"}, {"BidirectionalFollowReplyWeightBoost", "+ added to a reply between mutuals"}, {"Retweet", "repost"},
    {"Quote", "quote"}, {"Share", "share (any)"}, {"ShareViaDm", "share by DM"}, {"ShareViaCopyLink", "share by copying the link"},
    {"FollowAuthor", "follow the author"}, {"Click", "click into the post"}, {"OpenLink", "open a link"}, {"ProfileClick", "click the profile"},
    {"PhotoExpand", "expand a photo"}, {"VideoOpen", "open a video"}, {"Dwell", "stop and read (dwell)"}, {"ContClickDwellTime", "time spent after clicking in"},
    {"NotInterested", "\"not interested\""}, {"BlockAuthor", "block the author"}, {"MuteAuthor", "mute the author"}, {"Report", "report"},
    {"NotDwelled", "scroll straight past"}};
inline const char* SNAPSHOT_DATE = "2026-09-29 (commit a707cc2)";   // for --offline or when GitHub cannot be reached
using Weights = std::vector<std::pair<std::string, double>>;
inline const Weights SNAPSHOT = {
    {"Favorite", 0.5}, {"Reply", 5.0}, {"BidirectionalFollowReplyWeightBoost", 15.0}, {"Retweet", 1.0},
    {"PhotoExpand", 0.05}, {"VideoOpen", 0.07}, {"Click", 0.3}, {"OpenLink", 0.2}, {"ProfileClick", 0.0},
    {"Share", 2.0}, {"ShareViaDm", 5.0}, {"ShareViaCopyLink", 20.0}, {"Dwell", 0.05}, {"Quote", 5.0},
    {"FollowAuthor", 4.0}, {"ContClickDwellTime", 0.4}, {"NotInterested", -47.52}, {"BlockAuthor", -31.2},
    {"MuteAuthor", -58.8}, {"Report", -234.0}, {"NotDwelled", -0.02}};
inline const char* PARAM_RE = R"P(param!\(\s*(\w+)\s*,\s*f64\s*,\s*"[^"]+"\s*,\s*(-?\d+(?:\.\d+)?)\s*\))P";   // re.S

inline const std::string* label(const std::string& k) { for (const auto& [a, b] : LABELS) if (a == k) return &b; return nullptr; }
inline void wset(Weights& w, const std::string& k, double v) { for (auto& kv : w) if (kv.first == k) { kv.second = v; return; } w.emplace_back(k, v); }

inline std::pair<Weights, std::string> fetch() {
    const std::string src = xs::url_get(RAW, UA, 15);
    Weights weights;
    for (const auto& g : re::compile(PARAM_RE, re::S).findall_groups(src)) {
        const std::string& name = g[0];
        if (ends(name, "Weight") || ends(name, "WeightBoost")) wset(weights, ends(name, "Weight") ? cpslice(name, 0, -6) : name, packtools::master::py_float(g[1]));
    }
    std::string date = "unknown date";
    try {
        const Val c = loads(xs::url_get(COMMITS, UA, 15));
        if (c.t != Val::LIST || c.l->empty()) throw PyErr("IndexError", "list index out of range");
        const Val& first = (*c.l)[0];
        const Val d = packtools::ig::getitem(packtools::ig::getitem(packtools::ig::getitem(first, "commit"), "committer"), "date");
        const Val sha = packtools::ig::getitem(first, "sha");
        if (d.t != Val::STR || sha.t != Val::STR) throw PyErr("TypeError", "'int' object is not subscriptable");
        date = cpslice(d.s, 0, 10) + " (commit " + cpslice(sha.s, 0, 7) + ")";
    } catch (const std::exception&) {}   // the date is a nicety: any failure leaves "unknown date"
    return {weights, date};
}
inline std::string g7(double v) { return rjust(fmt_g(v), 7); }   // f"{v:>7g}"
inline void render(const Weights& weights, const std::string& date, const std::string& source) {
    double like = 0.5; for (const auto& [k, v] : weights) if (k == "Favorite" && v != 0.0) like = v;   // weights.get("Favorite") or 0.5
    Weights pos, neg;
    for (const auto& [k, v] : weights) if (label(k) && v != 0.0) (v > 0 ? pos : neg).emplace_back(k, v);
    std::stable_sort(pos.begin(), pos.end(), [](const auto& x, const auto& y) { return -x.second < -y.second; });
    std::stable_sort(neg.begin(), neg.end(), [](const auto& x, const auto& y) { return x.second < y.second; });
    print("\nX FOR YOU RANKER  \xC2\xB7  action weights  \xC2\xB7  " + source + ", last changed " + date);
    print(std::string(78, '='));
    print("REWARDED" + std::string(34, ' ') + "weight   vs a like");
    for (const auto& [k, v] : pos) print("  " + ljust(*label(k), 40) + " " + g7(v) + "   " + rjust(fmt_f(v / like, 1), 6) + "x");
    print("\nPUNISHED");
    for (const auto& [k, v] : neg) print("  " + ljust(*label(k), 40) + " " + g7(v));
    print(std::string(78, '-'));
    print("  These multiply the PREDICTED probability of each action for each viewer, not");
    print("  counts. X's own comment in the file: reading the ratios as count equivalences");
    print("  is \"incorrect\". Read the direction: write things people reply to, quote and");
    print("  send to one person. Avoid what gets you muted or marked not interested.");
    print("  Source: github.com/" + REPO + "/blob/main/" + PATH + "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("weights", "X's current published ranking weights.");
    p.opt({"--json"}).store_true();
    p.opt({"--offline"}).store_true();
    const Val args = parse_args(p, argv);
    Weights weights; std::string date, source;
    if (B(args, "offline")) { weights = SNAPSHOT; date = SNAPSHOT_DATE; source = "offline snapshot"; }
    else {
        try {
            std::tie(weights, date) = fetch();
            source = "live from GitHub";
            if (weights.empty()) throw PyErr("ValueError", "no weights parsed; the file format may have changed");
        } catch (const std::exception& e) {
            eprint("could not read the live file (" + xs::err_text(e) + "). Showing the snapshot instead.");
            weights = SNAPSHOT; date = SNAPSHOT_DATE; source = "offline snapshot";
        }
    }
    if (B(args, "json")) {
        Val o = Val::dict(); o.d->set("source", Val::str(source)); o.d->set("date", Val::str(date));
        Val w = Val::dict(); for (const auto& [k, v] : weights) w.d->set(k, Val::flt(v));
        o.d->set("weights", w);
        DumpOpts d; d.indent = 2; print(dumps(o, d));
    } else render(weights, date, source);
    return 0;
}
PACKTOOL("x-plan/weights", main);
}  // namespace packtools::x_weights

// ------------------------------------------------------------------------------------------------ x-viral/pull
namespace packtools::x_pull {
using namespace pyrt;

inline const char* ENDPOINT = "https://publish.x.com/oembed";
inline const char* UA = "x-agent-skill/1.0 (+https://github.com/Jakeschincariol/x-agent-skill)";
inline const char* STATUS_RE = R"(https?://(?:www\.|mobile\.)?(?:x|twitter)\.com/([A-Za-z0-9_]{1,15})/status(?:es)?/(\d+))";
inline const std::vector<std::string> COLUMNS = {"url", "account", "name", "date", "format", "lines", "words", "views", "likes", "replies", "reposts", "hook", "text"};

inline std::vector<std::string> parse_urls(const std::vector<std::string>& items) {
    std::vector<std::string> out; std::set<std::string> seen;
    for (const auto& item : items)
        for (const auto& m : re::compile(STATUS_RE).finditer(item)) {
            const std::string handle = m.group(1), pid = m.group(2);
            if (seen.insert(pid).second) out.push_back("https://x.com/" + handle + "/status/" + pid);
        }
    return out;
}
inline std::string quote_plus(const std::string& s) {   // urllib.parse.quote_plus, safe=""
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') o += static_cast<char>(c);
        else if (c == ' ') o += '+';
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
inline Val fetch(const std::string& url) {
    const std::string q = "url=" + quote_plus(url) + "&omit_script=true&dnt=true";
    return loads(xs::url_get(std::string(ENDPOINT) + "?" + q, UA, 15));
}
inline const std::string& need_str(const Val& v) {   // re.search(pattern, v)
    if (v.t != Val::STR) throw PyErr("TypeError", "expected string or bytes-like object, got '" + packtools::ig::type_of(v) + "'");
    return v.s;
}
inline Val oget(const Val& d, const std::string& k, const Val& dflt) {   // d.get(k, dflt)
    if (d.t != Val::DICT) throw PyErr("AttributeError", "'" + packtools::ig::type_of(d) + "' object has no attribute 'get'");
    const Val* v = d.d->get(k);
    return v ? *v : dflt;
}
inline Val parse_embed(const Val& d, const std::string& url) {   // oEmbed html -> text with its real line breaks, plus what the markup says
    const std::string block = need_str(oget(d, "html", Val::str("")));
    const re::Match m = re::search(R"(<p[^>]*>(.*?)</p>)", block, re::S);
    std::string inner = m ? m.group(1) : "";
    const bool media = static_cast<bool>(re::search(R"(>pic\.(?:twitter|x)\.com/)", inner));
    inner = re::sub(R"(<a[^>]*>pic\.(?:twitter|x)\.com/[^<]*</a>)", "", inner);
    const auto links = re::findall(R"P(<a href="(https://t\.co/[^"]+)")P", inner);
    inner = re::sub(R"(<br\s*/?>)", "\n", inner);
    inner = re::sub(R"(<a[^>]*>([^<]*)</a>)", R"(\1)", inner);
    const std::string text = strip(html_unescape(re::sub(R"(<[^>]+>)", "", inner)));
    const bool truncated = ends(text, "\xE2\x80\xA6");
    std::string date;
    if (const re::Match dm = re::search(R"P(<a href="[^"]+/status/\d+[^"]*">([^<]+)</a>\s*</blockquote>)P", block)) date = html_unescape(dm.group(1));
    const Val u = oget(d, "url", Val::str(url));
    const re::Match hm = re::search(STATUS_RE, need_str(u));
    if (!hm) throw PyErr("AttributeError", "'NoneType' object has no attribute 'group'");
    const std::string fmt = truncated ? "long" : media ? "media" : !links.empty() ? "link" : "text";   // long: a Premium long post, cut by the embed
    std::vector<std::string> lines; for (const auto& l : splitlines(text)) if (!strip(l).empty()) lines.push_back(l);
    Val r = Val::dict();
    r.d->set("url", u); r.d->set("account", Val::str("@" + hm.group(1))); r.d->set("name", oget(d, "author_name", Val::str("")));
    r.d->set("date", Val::str(date)); r.d->set("format", Val::str(fmt)); r.d->set("lines", Val::integer(static_cast<long long>(lines.size())));
    r.d->set("words", Val::integer(static_cast<long long>(re::findall(R"(\S+)", text).size())));
    for (const char* k : {"views", "likes", "replies", "reposts"}) r.d->set(k, Val::str(""));
    r.d->set("hook", Val::str(lines.empty() ? "" : strip(lines[0]))); r.d->set("text", Val::str(text));
    return r;
}
inline std::string tsv_cell(const Val& v) {
    return replace_all(replace_all(replace_all(pystr(v), "\t", " "), "\r", ""), "\n", " \xE2\x8F\x8E ");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("pull", "X post URLs -> a swipe sheet via X's oEmbed API.");
    p.pos("inputs").nargs("*").help("URLs, or a file of URLs, or - for stdin");
    p.opt({"--max"}).integer().def(100).help("hard cap per run (default 100)");
    p.opt({"--delay"}).number().def_float(1.0).help("seconds between requests (min 1)");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    std::vector<std::string> inputs; for (const auto& v : *get(args, "inputs").l) inputs.push_back(v.s);
    if (inputs.empty()) inputs.push_back("-");
    std::vector<std::string> items;
    for (const auto& i : inputs) {
        if (i == "-") items.push_back(packtools::master::read_stdin_text());
        else if (re::search(STATUS_RE, i)) items.push_back(i);
        else items.push_back(read_text_utf8(i));
    }
    auto urls = parse_urls(items);
    if (urls.empty()) { eprint("no x.com/.../status/... URLs found"); return 2; }
    const long long mx = get(args, "max").i;
    if (static_cast<long long>(urls.size()) > mx) {
        eprint(std::to_string(urls.size()) + " URLs, capped at " + std::to_string(mx) + ". This reads posts you picked; it is not a crawler. Run it again on the rest if you mean it.");
        // urls[:max]: a negative cap counts back from the end
        const long long n = static_cast<long long>(urls.size());
        const long long keep = mx >= 0 ? mx : std::max(0LL, n + mx);
        urls.resize(static_cast<std::size_t>(keep));
    }
    const double delay = get(args, "delay").num();
    std::vector<Val> rows; long long failed = 0;
    const std::string of = "/" + std::to_string(urls.size());
    for (std::size_t i = 0; i < urls.size(); ++i) {
        const std::string& url = urls[i];
        if (i) {
            const double s = std::isnan(delay) ? 1.0 : std::max(1.0, delay);
            if (std::isinf(s)) throw PyErr("OverflowError", "timestamp too large to convert to C _PyTime_t");
            flush();
            std::this_thread::sleep_for(std::chrono::duration<double>(s));
        }
        const std::string idx = rjust(std::to_string(i + 1), 3);
        try {
            rows.push_back(parse_embed(fetch(url), url));
            eprint("  " + idx + of + "  " + ljust(get(rows.back(), "account").s, 17) + " " + cpslice(get(rows.back(), "hook").s, 0, 48));
        } catch (const xs::HttpError& e) {
            const std::string reason = e.code == 404 ? "deleted, private, or protected" : e.code == 403 ? "not embeddable" : "HTTP " + std::to_string(e.code);
            ++failed;
            eprint("  " + idx + of + "  skipped: " + reason + "  " + url);
        } catch (const std::exception& e) {   // network, JSON
            const std::string why = cpslice(xs::err_text(e), 0, 60);
            ++failed;
            eprint("  " + idx + of + "  skipped: " + why + "  " + url);
        }
    }
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(Val::list(rows), o)); }
    else {
        print(join(COLUMNS, "\t"));
        for (const auto& r : rows) { std::vector<std::string> c; for (const auto& k : COLUMNS) c.push_back(tsv_cell(get(r, k))); print(join(c, "\t")); }
    }
    eprint("\n" + std::to_string(rows.size()) + " pulled, " + std::to_string(failed) + " skipped. Now fill in the views column from the posts themselves, then run swipe.py.");
    return 0;
}
PACKTOOL("x-viral/pull", main);
}  // namespace packtools::x_pull

// x-human/humanize: master_humanize with ig-human's fuller clean-up plus x's emoji-joiner and flag protection
namespace packtools::x_humanize {
inline int main(const std::vector<std::string>& argv) { return packtools::master_humanize::main_with(argv, "slop.json", false, true); }
PACKTOOL("x-human/humanize", main);
}  // namespace packtools::x_humanize
