// The YouTube Pack's helpers (youtube-agent-skill, Jake Schincariol, MIT) and the helpers the LinkedIn and Instagram
// packs share with master-skills' virality copy. Group 2: the shared ones are the master ports with another skill
// folder (slop.json next to the skill instead of tools/slop.json) or the original's small differences switched on.
//   yt-package/title      lint a title + thumbnail pairing             (master_title, without -h/--help and --cover)
//   yt-viral/swipe        rank videos against their channel's median   (master_swipe, yt-script's formulas, title only)
//   yt-script/hookscore   score a spoken hook on five properties
//   yt-edit/deadair       an edit decision list from a transcript
//   yt-chapters/chapters  YouTube chapters from a transcript
//   yt-retention/retention  read a Studio audience-retention export
//   li-human/detect, li-human/humanize, ig-human/detect             (master_detect / master_humanize)
// Each prints what the Python original printed and exits with the same code.
namespace packtools::yt {
using namespace pyrt;
using namespace packtools::master;

// o[k] on a JSON value, raising like Python
inline Val subscript(const Val& o, const std::string& k) {
    if (o.t == Val::DICT) {
        if (!o.d->has(k)) throw PyErr("KeyError", str_repr(k));
        return o.d->at(k);
    }
    if (o.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
    if (o.t == Val::STR) throw PyErr("TypeError", "string indices must be integers, not 'str'");
    throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
}
// int(str): Unicode decimal digits too
inline long long pint(const std::string& s) {
    std::string t;
    for (uint32_t c : ucase_detail::u32(s)) {
        if (c > 127 && uprop::decimal(c)) t += static_cast<char>('0' + digit_value(c));
        else t += utf8(c);
    }
    const auto v = py_int(t);
    if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(s));
    return *v;
}
// str.count(sub): non-overlapping occurrences (byte search finds the same ones in UTF-8)
inline long long count_sub(const std::string& s, const std::string& sub) {
    if (sub.empty()) return static_cast<long long>(cplen(s)) + 1;
    long long n = 0;
    for (std::size_t k = s.find(sub); k != std::string::npos; k = s.find(sub, k + sub.size())) ++n;
    return n;
}
inline Val dict_of(std::initializer_list<std::pair<const char*, Val>> kv) {
    Val d = Val::dict();
    for (const auto& [k, v] : kv) d.d->set(k, v);
    return d;
}
inline void print_json(const Val& v, int indent) { DumpOpts o; o.indent = indent; print(dumps(v, o)); }
inline void drop_flag(std::vector<std::string>& a, const std::string& f) { a.erase(std::remove(a.begin(), a.end(), f), a.end()); }
}  // namespace packtools::yt

// ---------------------------------------------------------------------------------------------- yt-package/title
namespace packtools::yt_title {
inline int main(const std::vector<std::string>& argv) {
    static const std::string doc = pyrt::replace_all(master_title::DOC, "master-virality/title", "yt-package/title");
    return master_title::main_with(argv, false, doc);
}
PACKTOOL("yt-package/title", main);
}  // namespace packtools::yt_title

// ---------------------------------------------------------------------------------------------- yt-viral/swipe
namespace packtools::yt_swipe {
inline const char* DOC = R"DOC(swipe - rank collected videos by how far each beat its OWN channel, then name the formula.

    astro-server tool yt-viral/swipe collected.json
    astro-server tool yt-viral/swipe collected.json --min 2.0 --json

Input is a list you collected - one object per video:

    [{"channel":"Some Channel","title":"...","views":412000,"url":"...","duration":613}, ...]

Raw view counts rank channel size, not ideas. A 400k video on a 2M-subscriber channel is a normal
Tuesday; a 400k video on a channel whose median is 30k is the thing worth studying. So every video
is scored as a MULTIPLE OF ITS OWN CHANNEL'S MEDIAN, which needs at least four videos per channel
to mean anything - the tool says so rather than quietly ranking on noise.

The formula comes from skills/yt-script/hook-formulas.json, matched against the TITLE. It is a judgement
about the words on screen, not a claim about why the video worked.
)DOC";
inline int main(const std::vector<std::string>& argv) { return master_swipe::main_with(argv, master_swipe::Cfg{DOC, "../yt-script/hook-formulas.json", false}); }
PACKTOOL("yt-viral/swipe", main);
}  // namespace packtools::yt_swipe

// ---------------------------------------------------------------------------------------------- li-human, ig-human
namespace packtools::social_human {
inline int detect(const std::vector<std::string>& argv) { return master_detect::main_with(argv, "slop.json"); }
inline int humanize(const std::vector<std::string>& argv) { return master_humanize::main_with(argv, "slop.json"); }
inline int ig_humanize(const std::vector<std::string>& argv) { return master_humanize::main_with(argv, "slop.json", true); }   /* fuller clean-up + restore_capitals */
PACKTOOL("li-human/detect", detect);
PACKTOOL("li-human/humanize", humanize);
PACKTOOL("ig-human/detect", detect);
PACKTOOL("ig-human/humanize", ig_humanize);
}  // namespace packtools::social_human

// ---------------------------------------------------------------------------------------------- yt-script/hookscore
namespace packtools::yt_hookscore {
using namespace pyrt;
using namespace packtools::master;
using namespace packtools::yt;

inline const char* DOC = R"DOC(hookscore - score a YouTube hook before you waste a take on it.

Five properties, 0-100 each, and a verdict that is 60% the mean and 40% the weakest one. The
weakest-link weighting is deliberate: a hook with four strong properties and one dead one is a hook
that leaks at the dead one, and averaging hides that.

    astro-server tool yt-script/hookscore hooks.txt            # one hook per line, ranked
    astro-server tool yt-script/hookscore --hook "one line"    # score a single hook
    astro-server tool yt-script/hookscore --json hooks.txt     # machine-readable

WHAT THIS CAN AND CANNOT TELL YOU. Measured against 74 real short-form hooks (first 15 seconds of
auto-captions, top-8 and bottom-8 by views across five channels): it separates deliberately bad
hooks from real ones well, and it separates a creator's own hits from their own misses barely at
all. Treat a low score as a reason to look again, never a high score as a promise.
)DOC";

inline const std::set<std::string> FILLER = {"basically", "actually", "literally", "just", "really", "very", "so", "kind", "sort", "like",
                                             "guys", "hey", "welcome", "today", "video", "subscribe", "channel"};
inline const std::set<std::string> VAGUE = {"amazing", "incredible", "insane", "crazy", "huge", "massive", "game", "changer", "secret",
                                            "powerful", "ultimate", "best", "revolutionary", "mind", "blowing", "unbelievable"};
inline const std::string CONCRETE = R"(\b(\d[\d,.]*\s?(%|k|m|x|s|m|h)?|\$\d|\d+\s?(second|minute|hour|day|week|month|year)s?)\b)";
inline const std::string YOU = R"(\b(you|your|you're|youre|yourself)\b)";
inline const std::string STAKE = R"(\b(lose|lost|wasting|waste|quit|fail|broke|cost|risk|before|stop|never|die|dying|dead)\b)";
inline const std::string CURIOSITY = R"(\b(why|how|what|which|until|before|but|nobody|almost|except|reason|actually)\b)";
inline const std::string CLOSED = R"(\b(because|so that|which means)\b)";
inline const char* PROPS[] = {"SPECIFICITY", "ADDRESS", "STAKES", "CURIOSITY", "BREVITY"};
inline const std::map<std::string, std::string> FIX = {
    {"SPECIFICITY", "swap one adjective for a number, a name or a date"},
    {"ADDRESS", "say 'you' in the first six words"},
    {"STAKES", "name what it costs them to keep doing it the current way"},
    {"CURIOSITY", "cut the half of the sentence that answers itself"},
    {"BREVITY", "9 to 24 words. Read it out loud and stop where you run out of breath"},
};

inline long long clamp100(long long v) { return std::max(0LL, std::min(100LL, v)); }
inline std::vector<std::string> words(const std::string& t) { return re::findall(R"([a-z0-9'%$.]+)", lower(t)); }
inline long long nfind(const std::string& pat, const std::string& t) { return static_cast<long long>(re::findall(pat, t, re::I).size()); }

inline long long specificity(const std::string& t) {
    const auto w = words(t);
    if (w.empty()) return 0;
    const long long nums = nfind(CONCRETE, t);
    long long vague = 0, filler = 0;
    for (const auto& x : w) {
        if (VAGUE.count(x)) ++vague;
        if (FILLER.count(x)) ++filler;
    }
    long long s = 34 + nums * 22 - vague * 16 - filler * 5;
    const auto ws = split_ws(t); long long caps = 0;
    for (std::size_t k = 1; k < ws.size(); ++k) if (isupper_str(cpslice(ws[k], 0, 1))) ++caps;
    s += std::min(18LL, 6 * caps);
    return clamp100(s);
}
inline long long address(const std::string& t) {
    const long long n = nfind(YOU, t);
    const auto ws = split_ws(t);
    const std::vector<std::string> head(ws.begin(), ws.begin() + static_cast<long>(std::min<std::size_t>(6, ws.size())));
    const long long first = re::search(YOU, join(head, " "), re::I) ? 30 : 0;
    return clamp100(26 + n * 20 + first);
}
inline long long stakes(const std::string& t) {
    const long long n = nfind(STAKE, t);
    return clamp100(22 + n * 26 + (re::search(CONCRETE, t, re::I) ? 14 : 0));
}
inline long long curiosity(const std::string& t) {
    const long long n = nfind(CURIOSITY, t);
    const long long q = ends(strip(t), "?") ? 18 : 0;
    const long long closed = re::search(CLOSED, t, re::I) ? -18 : 0;   // a hook that resolves itself has no gap left
    return clamp100(24 + n * 17 + q + closed);
}
inline long long brevity(const std::string& t) {
    const long long n = static_cast<long long>(words(t).size());
    if (n == 0) return 0;
    if (9 <= n && n <= 24) return 100;   // the band a spoken hook lands in at ~150wpm inside 10 seconds
    if (n < 9) return std::max(30LL, 100 - (9 - n) * 11);
    return std::max(10LL, 100 - (n - 24) * 7);
}

// the first formula with the most matching patterns
inline std::pair<std::string, long long> classify(const List& formulas, const std::string& t) {
    const Val* best = nullptr; long long hits = 0;
    for (const auto& f : formulas) {
        long long n = 0;
        for (const auto& p : iterate(subscript(f, "match"))) if (re::search(re_text(p), t, re::I)) ++n;
        if (n > hits) { best = &f; hits = n; }
    }
    return {best ? pystr(subscript(*best, "name")) : std::string("Unclassified"), hits};
}

struct Row { std::string hook; std::vector<std::pair<std::string, long long>> parts; long long verdict = 0; std::string band, formula; long long matched = 0; };
inline std::string band(long long v) { return v >= 72 ? "STRONG" : v >= 55 ? "WORKABLE" : "WEAK"; }

inline Row score(const List& formulas, const std::string& t) {
    Row r;
    r.parts = {{"SPECIFICITY", specificity(t)}, {"ADDRESS", address(t)}, {"STAKES", stakes(t)}, {"CURIOSITY", curiosity(t)}, {"BREVITY", brevity(t)}};
    long long sum = 0, mn = r.parts[0].second;
    for (const auto& [k, v] : r.parts) { sum += v; mn = std::min(mn, v); }
    r.verdict = static_cast<long long>(std::nearbyint(0.6 * (static_cast<double>(sum) / 5.0) + 0.4 * static_cast<double>(mn)));
    const auto [name, hits] = classify(formulas, t);
    r.formula = name; r.matched = hits; r.hook = strip(t); r.band = band(r.verdict);
    return r;
}

inline void report(const Row& r) {
    const std::string t = strip(r.hook);
    print("\n  " + t);
    print("  " + std::string(static_cast<std::size_t>(std::min<long long>(72, std::max<long long>(20, static_cast<long long>(cplen(t))))), '-'));
    for (const auto& [k, v] : r.parts) print("    " + ljust(k, 12) + " " + rjust(std::to_string(v), 3) + "  " + std::string(static_cast<std::size_t>(v / 5), '#'));
    print("    " + ljust("VERDICT", 12) + " " + rjust(std::to_string(r.verdict), 3) + "  " + band(r.verdict));
    print("    formula      " + r.formula + (r.matched ? "  (" + std::to_string(r.matched) + " pattern" + (r.matched != 1 ? "s" : "") + " matched)"
                                                       : std::string("  (no formula matched - that is usually a summary, not a hook)")));
    std::string low = r.parts[0].first; long long lv = r.parts[0].second;
    for (const auto& [k, v] : r.parts) if (v < lv) { low = k; lv = v; }
    print("    weakest      " + low + " - " + FIX.at(low));
}

inline int main(const std::vector<std::string>& argv) {
    const List formulas = iterate(subscript(loads(read_text_utf8(skill_file("hook-formulas.json"))), "hooks"));   // module import
    std::vector<std::string> a = argv;
    const bool as_json = index_of(a, "--json") >= 0;
    drop_flag(a, "--json");
    std::vector<std::string> lines;
    if (const int k = index_of(a, "--hook"); k >= 0) lines.push_back(arg_after(a, k));
    else if (!a.empty() && exists(a[0])) {
        for (const auto& l : splitlines(read_text_utf8(a[0]))) if (!strip(l).empty()) lines.push_back(l);
    } else { print(DOC); return a.empty() ? 1 : 0; }
    std::vector<Row> out;
    for (const auto& t : lines) out.push_back(score(formulas, t));
    std::stable_sort(out.begin(), out.end(), [](const Row& x, const Row& y) { return x.verdict > y.verdict; });
    if (as_json) {
        Val l = Val::list();
        for (const auto& r : out) {
            Val p = Val::dict(); for (const auto& [k, v] : r.parts) p.d->set(k, Val::integer(v));
            l.l->push_back(dict_of({{"hook", Val::str(r.hook)}, {"properties", p}, {"verdict", Val::integer(r.verdict)}, {"band", Val::str(r.band)}, {"formula", Val::str(r.formula)}}));
        }
        print_json(l, 1);
        return 0;
    }
    for (const auto& r : out) report(r);
    if (out.size() > 1) print("\n  winner: " + strip(out[0].hook) + "  (" + std::to_string(out[0].verdict) + ", " + out[0].band + ")\n");
    return 0;
}
PACKTOOL("yt-script/hookscore", main);
}  // namespace packtools::yt_hookscore

// ---------------------------------------------------------------------------------------------- yt-edit/deadair
namespace packtools::yt_deadair {
using namespace pyrt;
using namespace packtools::master;
using namespace packtools::yt;

inline const char* DOC = R"DOC(deadair - an edit decision list from a timestamped transcript.

    astro-server tool yt-edit/deadair transcript.srt            # or .vtt, or whisper .json
    astro-server tool yt-edit/deadair transcript.srt --floor 0.35 --json

Finds three things and prints the cuts as a list you can act on, newest problem first:
  DEAD    gaps between spoken cues longer than the floor
  FILLER  cues that are only filler ("um", "so yeah", "basically")
  REPEAT  a sentence restarted - the second take of the same opening

WHAT IT DOES NOT DO. It does not cut the file. It prints an EDL, the total it would remove, and the
runtime you would land on, and you apply it in whatever editor you use. Nothing here touches media.
)DOC";

inline const std::string FILLER_ONLY = R"(^[\s,.-]*((um+|uh+|er+|ah+|so|okay|ok|right|yeah|like|anyway|basically|)"
                                       R"(actually|you know|i mean|let me see|hold on)[\s,.-]*)+$)";
inline bool filler(const std::string& t) { return static_cast<bool>(re::match(FILLER_ONLY, t, re::I)); }

struct Cue { double a = 0, b = 0; std::string t; };

inline double parse_ts(const std::string& s0) {
    const std::string s = replace_all(strip(s0), ",", ".");
    const auto p = split(s, ":");
    if (p.size() == 3) return static_cast<double>(pint(p[0]) * 3600 + pint(p[1]) * 60) + py_float(p[2]);
    const long long m = pint(p[0]);
    if (p.size() < 2) throw PyErr("IndexError", "list index out of range");
    return static_cast<double>(m * 60) + py_float(p[1]);
}

// srt / vtt cues, or whisper JSON segments (read with errors="replace")
inline std::vector<Cue> load(const std::string& path) {
    const std::string raw = read_text_utf8(path, true);
    std::vector<Cue> out;
    if (ends(path, ".json")) {
        const Val d = loads(raw);
        const Dict& dd = as_dict(d);
        const Val segs = dd.has("segments") ? dd.at("segments") : Val::list();
        for (const auto& s : iterate(segs)) {
            Cue c; c.a = to_float(subscript(s, "start")); c.b = to_float(subscript(s, "end"));
            const Val tx = as_dict(s).at("text");
            if (truthy(tx)) {
                if (tx.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(tx) + "' object has no attribute 'strip'");
                c.t = strip(tx.s);
            }
            out.push_back(c);
        }
        return out;
    }
    static const std::string TIMING = R"(\s*(\d[\d:.,]+)\s*-->\s*(\d[\d:.,]+))";
    std::vector<Cue> cues; std::vector<std::vector<std::string>> texts;
    for (const auto& line : splitlines(raw)) {
        const re::Match m = re::match(TIMING, line);
        if (m) {
            Cue c; c.a = parse_ts(m.group(1)); c.b = parse_ts(m.group(2));
            cues.push_back(c); texts.emplace_back();
        } else if (!cues.empty()) {
            const std::string st = strip(line);
            if (!st.empty() && !isdigit_str(st)) texts.back().push_back(st);
        }
    }
    for (std::size_t k = 0; k < cues.size(); ++k) {
        if (texts[k].empty()) continue;
        Cue c = cues[k]; c.t = join(texts[k], " "); out.push_back(c);
    }
    return out;
}

inline std::vector<std::string> norm5(const std::string& t) {
    auto w = split_ws(re::sub("[^a-z ]", "", lower(t)));
    if (w.size() > 5) w.resize(5);
    return w;
}

struct Cut { std::string kind; double start = 0, end = 0; std::string why; };

inline int main(const std::vector<std::string>& argv) {
    std::vector<std::string> a = argv;
    const bool as_json = index_of(a, "--json") >= 0;
    drop_flag(a, "--json");
    double floor = 0.45;
    if (const int k = index_of(a, "--floor"); k >= 0) floor = py_float(arg_after(a, k));
    std::vector<std::string> files;
    for (const auto& x : a) if (!starts(x, "--") && !re::match(R"(^[\d.]+$)", x)) files.push_back(x);
    if (files.empty() || !exists(files[0])) { print(DOC); return 1; }
    const std::vector<Cue> cues = load(files[0]);
    if (cues.empty()) { print("no cues found - is this an srt, vtt or whisper json?"); return 1; }
    const double dur = cues.back().b;
    std::vector<Cut> cuts;
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const Cue& c = cues[i];
        if (filler(c.t)) cuts.push_back({"FILLER", c.a, c.b, cpslice(strip(c.t), 0, 48)});
        if (i) {
            const double gap = c.a - cues[i - 1].b;
            if (gap > floor) {
                const double keep = floor / 2;
                cuts.push_back({"DEAD", round_n(cues[i - 1].b + keep, 3), round_n(c.a - keep, 3), fmt_f(gap, 2) + "s gap"});
            }
        }
        // A restart is compared against the last cue that was actually SPEECH (an "um" between two takes is common).
        if (!strip(c.t).empty() && !filler(c.t)) {
            long j = static_cast<long>(i) - 1;
            while (j >= 0 && (filler(cues[static_cast<std::size_t>(j)].t) || strip(cues[static_cast<std::size_t>(j)].t).empty())) --j;
            if (j >= 0) {
                const Cue& p = cues[static_cast<std::size_t>(j)];
                const auto a1 = norm5(p.t), b1 = norm5(c.t);
                if (a1.size() >= 3 && a1 == b1) cuts.push_back({"REPEAT", p.a, p.b, "restart of \"" + join(a1, " ") + "\""});
            }
        }
    }
    std::vector<Cut> kept;
    for (const auto& c : cuts) if (c.end > c.start) kept.push_back(c);
    std::stable_sort(kept.begin(), kept.end(), [](const Cut& x, const Cut& y) { return x.start < y.start; });
    std::vector<double> lens; for (const auto& c : kept) lens.push_back(c.end - c.start);
    const double removed = fsum(lens);   // sum() of no cuts is the int 0
    if (as_json) {
        Val cl = Val::list();
        for (const auto& c : kept) cl.l->push_back(dict_of({{"kind", Val::str(c.kind)}, {"start", Val::flt(c.start)}, {"end", Val::flt(c.end)}, {"why", Val::str(c.why)}}));
        print_json(dict_of({{"source", Val::str(files[0])}, {"duration", Val::flt(dur)}, {"cuts", cl},
                            {"removed", kept.empty() ? Val::integer(0) : Val::flt(round_n(removed, 3))}, {"out", Val::flt(round_n(dur - removed, 3))}}), 1);
        return 0;
    }
    print("\n  " + files[0] + "   " + fmt_f(dur, 2) + "s in, " + std::to_string(cues.size()) + " cues, dead-air floor " + float_repr(floor) + "s\n");
    for (const auto& c : kept)
        print("    " + ljust(c.kind, 7) + " " + rjust(fmt_f(c.start, 2), 8) + " -> " + rjust(fmt_f(c.end, 2), 8) + "   " + rjust(fmt_f(c.end - c.start, 2), 5) + "s   " + c.why);
    if (dur == 0.0) throw PyErr("ZeroDivisionError", "float division by zero");
    print("\n  " + std::to_string(kept.size()) + " cuts, " + fmt_f(removed, 2) + "s removed, " + fmt_f(dur - removed, 2) + "s out (" + fmt_f(removed / dur * 100, 1) + "% shorter)\n");
    return 0;
}
PACKTOOL("yt-edit/deadair", main);
}  // namespace packtools::yt_deadair

// ---------------------------------------------------------------------------------------------- yt-chapters/chapters
namespace packtools::yt_chapters {
using namespace pyrt;
using namespace packtools::master;
using namespace packtools::yt;

inline const char* DOC = R"DOC(chapters - YouTube chapters from a timestamped transcript.

    astro-server tool yt-chapters/chapters transcript.srt            # or .vtt / whisper .json
    astro-server tool yt-chapters/chapters transcript.srt --target 8 --json

Prints a description block you can paste straight under a video. YouTube's own rules, enforced here
rather than assumed: the list must start at 00:00, needs at least three entries, and each chapter
must be at least 10 seconds long. A block that breaks any of those silently does not become
chapters, which is why this checks instead of trusting.

Boundaries come from the gaps - the pauses you actually took between sections - scored by how long
the pause was and how much the vocabulary changes across it. It is a first draft you retitle, not a
summariser.
)DOC";

inline const std::set<std::string>& stop_words() {
    static const std::set<std::string> s = [] {
        std::set<std::string> o;
        for (const auto& w : split_ws("the a an of for to in on and or is are was were be been with this that it as at by from "
                                      "you your i my we our they them he she but so if then than there here what which who how "
                                      "when where why not no yes do does did just really very like about into over out up down "
                                      "can could will would should have has had get got make made go going went one two")) o.insert(w);
        return o;
    }();
    return s;
}
// the keyword set, in first-seen order (Python's set order is arbitrary; first-seen keeps ties stable)
inline std::vector<std::string> keywords(const std::string& text) {
    std::vector<std::string> out; std::set<std::string> seen;
    for (const auto& w : re::findall("[a-z']{4,}", lower(text))) if (!stop_words().count(w) && seen.insert(w).second) out.push_back(w);
    return out;
}
inline long long floordiv(long long x, long long y) { long long q = x / y; if ((x % y != 0) && ((x < 0) != (y < 0))) --q; return q; }
inline std::string mmss(double tf) {
    if (std::isnan(tf)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(tf)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    const long long t = static_cast<long long>(std::trunc(tf));
    const long long h = floordiv(t, 3600), m = floordiv(t - floordiv(t, 3600) * 3600, 60), s = t - floordiv(t, 60) * 60;
    char b[96];
    if (h) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", h, m, s);
    else std::snprintf(b, sizeof b, "%lld:%02lld", m, s);
    return b;
}

struct Chapter { double start = 0; std::string label, title; double seconds = 0; };

inline int main(const std::vector<std::string>& argv) {
    std::vector<std::string> a = argv;
    const bool as_json = index_of(a, "--json") >= 0;
    drop_flag(a, "--json");
    long long target = 7;
    if (const int k = index_of(a, "--target"); k >= 0) target = pint(arg_after(a, k));
    std::vector<std::string> files;
    for (const auto& x : a) if (!starts(x, "--") && !isdigit_str(x)) files.push_back(x);
    if (files.empty() || !exists(files[0])) { print(DOC); return 1; }
    const auto cues = yt_deadair::load(files[0]);
    if (cues.size() < 6) { print("too few cues to chapter"); return 1; }
    const double dur = cues.back().b;
    struct Cand { double score, t; long i; };
    std::vector<Cand> cand;
    for (std::size_t i = 1; i < cues.size(); ++i) {
        const double gap = cues[i].a - cues[i - 1].b;
        std::vector<std::string> before, after;
        for (std::size_t k = i >= 12 ? i - 12 : 0; k < i; ++k) before.push_back(cues[k].t);
        for (std::size_t k = i; k < cues.size() && k < i + 12; ++k) after.push_back(cues[k].t);
        const auto kbv = keywords(join(before, " ")), kav = keywords(join(after, " "));
        const std::set<std::string> kb(kbv.begin(), kbv.end()), ka(kav.begin(), kav.end());
        std::size_t inter = 0; for (const auto& w : kb) if (ka.count(w)) ++inter;
        const std::size_t uni = kb.size() + ka.size() - inter;
        const double shift = uni ? 1 - (static_cast<double>(inter) / static_cast<double>(uni)) : 0.0;
        cand.push_back({gap * 1.6 + shift * 3.2, cues[i].a, static_cast<long>(i)});
    }
    std::stable_sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) {   // sort(reverse=True) on (score, t, i)
        if (x.score != y.score) return x.score > y.score;
        if (x.t != y.t) return x.t > y.t;
        return x.i > y.i;
    });
    const double MIN = 10.0;
    std::vector<double> picked = {0.0};
    for (const auto& c : cand) {
        if (static_cast<long long>(picked.size()) >= target) break;
        bool far_enough = true;
        for (const double p : picked) if (!(std::fabs(c.t - p) >= MIN)) far_enough = false;
        if (far_enough && dur - c.t >= MIN) picked.push_back(c.t);
    }
    std::stable_sort(picked.begin(), picked.end());
    std::vector<Chapter> chapters;
    for (std::size_t n = 0; n < picked.size(); ++n) {
        const double t = picked[n], end = n + 1 < picked.size() ? picked[n + 1] : dur;
        std::vector<std::string> parts;
        for (const auto& c : cues) if (c.a >= t && c.b <= end) parts.push_back(c.t);
        const std::string text = join(parts, " "), low = lower(text);
        auto kw = keywords(text);
        std::stable_sort(kw.begin(), kw.end(), [&](const std::string& x, const std::string& y) { return count_sub(low, x) > count_sub(low, y); });
        std::vector<std::string> tw;
        for (std::size_t k = 0; k < kw.size() && k < 3; ++k) { std::string w = kw[k]; if (!w.empty() && w[0] >= 'a' && w[0] <= 'z') w[0] = static_cast<char>(w[0] - 32); tw.push_back(w); }   /* str.capitalize() of [a-z']+ */
        std::string title = join(tw, " ");
        if (title.empty()) title = "Section";
        chapters.push_back({round_n(t, 2), mmss(t), title, round_n(end - t, 2)});
    }
    bool ok = chapters.size() >= 3 && chapters[0].start == 0;
    for (const auto& c : chapters) if (!(c.seconds >= MIN)) ok = false;
    if (as_json) {
        Val cl = Val::list();
        for (const auto& c : chapters) cl.l->push_back(dict_of({{"start", Val::flt(c.start)}, {"label", Val::str(c.label)}, {"draft_title", Val::str(c.title)}, {"seconds", Val::flt(c.seconds)}}));
        print_json(dict_of({{"valid", Val::boolean(ok)}, {"chapters", cl}}), 1);
        return 0;
    }
    print();
    for (const auto& c : chapters) print("  " + c.label + " " + c.title);
    print("\n  " + std::to_string(chapters.size()) + " chapters" + (ok ? "" : "  -- INVALID: YouTube needs 3+, a 00:00 first entry and 10s minimum each"));
    print("  Retitle every line before pasting. These are the topic words, not your words.\n");
    return 0;
}
PACKTOOL("yt-chapters/chapters", main);
}  // namespace packtools::yt_chapters

// ---------------------------------------------------------------------------------------------- yt-retention/retention
namespace packtools::yt_retention {
using namespace pyrt;
using namespace packtools::master;
using namespace packtools::yt;

inline const char* DOC = R"DOC(retention - read a YouTube Studio audience-retention export and find the leaks.

    astro-server tool yt-retention/retention retention.csv
    astro-server tool yt-retention/retention retention.csv --transcript transcript.srt   # names what was said at each drop
    astro-server tool yt-retention/retention retention.csv --json

Get the file from Studio: Analytics -> a video -> Engagement -> the audience-retention chart ->
the download icon -> "Audience retention". Two columns, a position (percent or seconds) and a
percentage still watching.

It reports three things, because they are three different problems with three different fixes:
  HOOK LEAK    what you lost in the first 30 seconds
  CLIFFS       single steep drops - a specific moment people left at
  SLIDE        the steady bleed rate across the flat middle

With --transcript it prints what you were saying at each cliff, which is the only version of this
report you can act on without scrubbing the video yourself.
)DOC";

// csv.reader over the file (utf-8-sig, errors="replace", newline=""): the first two cells that read as numbers
inline std::vector<std::pair<double, double>> load_csv(const std::string& path) {
    std::vector<std::pair<double, double>> rows;
    for (const auto& r : csv_rows(read_text_utf8(path, true, true, true))) {
        std::vector<double> vals;
        for (const auto& c0 : r) {
            const std::string c = replace_all(replace_all(strip(c0), "%", ""), ",", "");
            try { vals.push_back(py_float(c)); } catch (const PyErr& e) { if (std::string(e.type) != "ValueError") throw; }
        }
        if (vals.size() >= 2) rows.emplace_back(vals[0], vals[1]);
    }
    return rows;
}

struct Drop { double rate, x0, x1, d; };
// Python tuple order: the first unequal element decides
inline bool tuple_lt(const Drop& x, const Drop& y) {
    const double a[4] = {x.rate, x.x0, x.x1, x.d}, b[4] = {y.rate, y.x0, y.x1, y.d};
    for (int k = 0; k < 4; ++k) if (!(a[k] == b[k])) return a[k] < b[k];
    return false;
}

inline int main(const std::vector<std::string>& argv) {
    std::vector<std::string> a = argv;
    const bool as_json = index_of(a, "--json") >= 0;
    drop_flag(a, "--json");
    std::optional<std::string> tr;
    if (const int k = index_of(a, "--transcript"); k >= 0) tr = arg_after(a, k);
    std::vector<std::string> files;
    for (const auto& x : a) if (!starts(x, "--") && !(tr && x == *tr)) files.push_back(x);
    if (files.empty() || !exists(files[0])) { print(DOC); return 1; }
    const auto rows = load_csv(files[0]);
    if (rows.size() < 8) { print("could not read at least 8 data points from that csv"); return 1; }
    std::vector<double> xs, ys;
    for (const auto& [x, y] : rows) { xs.push_back(x); ys.push_back(y); }
    double mx = xs[0]; for (const double x : xs) if (x > mx) mx = x;   // max(): keeps the first unless a later one is greater
    const bool pct_axis = mx <= 100.5;
    std::optional<double> dur;
    if (const int k = index_of(a, "--duration"); k >= 0) dur = py_float(arg_after(a, k));
    const bool dur_true = dur && *dur != 0.0;   // NaN is truthy too
    auto at = [&](double x) { return (pct_axis && dur_true) ? (x / 100.0 * *dur) : x; };
    const double start = ys[0] != 0.0 ? ys[0] : 100.0;
    // HOOK: the first 30 seconds, or the first 10% when the axis is a percentage and we have no duration
    const double cutoff = !pct_axis ? 30.0 : (dur_true ? 30.0 / *dur * 100 : 10.0);
    bool any = false; double hook_end = start;
    for (const auto& [x, y] : rows) if (x <= cutoff) { if (!any || y < hook_end) hook_end = y; any = true; }
    const double hook_leak = start - hook_end;
    std::vector<Drop> drops;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        const double d = ys[i - 1] - ys[i];
        double span = xs[i] - xs[i - 1];
        if (span == 0.0) span = 1;
        drops.push_back({d / span, xs[i - 1], xs[i], d});
    }
    std::stable_sort(drops.begin(), drops.end(), [](const Drop& x, const Drop& y) { return tuple_lt(y, x); });
    struct Cliff { double from, to, lost; std::optional<double> at_seconds; };
    std::vector<Cliff> cliffs;
    for (std::size_t k = 0; k < drops.size() && k < 5; ++k) {
        const Drop& q = drops[k];
        if (!(q.d > 0.8)) continue;
        Cliff c{round_n(q.x0, 2), round_n(q.x1, 2), round_n(q.d, 2), std::nullopt};
        if (!pct_axis || dur_true) c.at_seconds = round_n(at(q.x0), 1);
        cliffs.push_back(c);
    }
    std::vector<double> mid;
    for (const auto& q : drops) if (q.x0 > cutoff) mid.push_back(q.rate);
    const double slide = mid.empty() ? 0.0 : fsum(mid) / static_cast<double>(mid.size());
    Val said = Val::dict();
    if (tr && !tr->empty() && exists(*tr)) {
        const auto cues = yt_deadair::load(*tr);
        for (const auto& c : cliffs) {
            if (!c.at_seconds) continue;
            const double t = *c.at_seconds;
            std::vector<std::string> near_text;
            for (const auto& q : cues) if (q.a <= t + 4 && q.b >= t - 4) near_text.push_back(q.t);
            said.d->set(float_repr(c.from), Val::str(cpslice(join(near_text, " "), 0, 140)));
        }
    }
    if (as_json) {
        Val cl = Val::list();
        for (const auto& c : cliffs)
            cl.l->push_back(dict_of({{"from", Val::flt(c.from)}, {"to", Val::flt(c.to)}, {"lost", Val::flt(c.lost)}, {"at_seconds", c.at_seconds ? Val::flt(*c.at_seconds) : Val()}}));
        print_json(dict_of({{"points", Val::integer(static_cast<long long>(rows.size()))}, {"start", Val::flt(start)}, {"hook_leak", Val::flt(round_n(hook_leak, 2))},
                            {"end", Val::flt(ys.back())}, {"cliffs", cl}, {"slide_per_unit", mid.empty() ? Val::integer(0) : Val::flt(round_n(slide, 3))}, {"said", said}}), 1);
        return 0;
    }
    print("\n  " + files[0] + "   " + std::to_string(rows.size()) + " points   " + fmt_f(ys[0], 1) + "% -> " + fmt_f(ys.back(), 1) + "%\n");
    const std::string verdict = hook_leak < 25 ? "healthy" : hook_leak < 40 ? "leaking" : "severe";
    print("  HOOK LEAK   " + fmt_f(hook_leak, 1) + "% lost in the opening   [" + verdict + "]");
    print("              under 25 is healthy for this length. Fix the first line before anything else.\n");
    print("  CLIFFS      the moments people actually left");
    for (const auto& c : cliffs) {
        const std::string where = c.at_seconds ? fmt_f(*c.at_seconds, 0) + "s" : float_repr(c.from);
        std::string line = "    -" + rjust(fmt_f(c.lost, 1), 5) + "%  at " + rjust(where, 8);
        if (said.size()) {
            const Val v = said.d->at(float_repr(c.from), Val::str(""));
            line += "   \"" + pystr(v) + "\"";
        }
        print(line);
    }
    if (cliffs.empty()) print("    none steeper than 0.8% - the loss is all slide, not moments");
    print("\n  SLIDE       " + fmt_f(slide, 3) + "% per unit across the middle");
    print("              a flat slide is pacing, not content. Cut the middle, do not rewrite it.\n");
    return 0;
}
PACKTOOL("yt-retention/retention", main);
}  // namespace packtools::yt_retention
