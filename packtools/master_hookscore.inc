// master-virality/hookscore - score a short-form hook on five calibrated properties (LENGTH, SPECIFICITY, STAKES,
// FRONTLOAD, ADDRESS) with dealbreakers, name its formula from tools/hook-formulas.json, and report where the payoff sits.
// C++ port of master-skills/skills/virality/tools/hookscore.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::master_hookscore {
using namespace pyrt;
using namespace packtools::master;

// Counts "$18,000" as one token; the LENGTH band (5 to 12) was calibrated with this exact tokenizer.
inline const char* WORD_RE = R"([A-Za-z0-9$%'’-]+)";
inline const std::string NUMBER_RE = R"(\$\s?\d[\d,]*(?:\.\d+)?|\b\d[\d,]*(?:\.\d+)?\s?(?:%|k\b|x\b|hrs?\b|hours?\b|mins?\b|minutes?\b|days?\b|weeks?\b|months?\b|years?\b)?)";
inline const std::string PROPER_RE = R"((?<!^)\b[A-Z][a-z]{2,}\b)";
inline const char* HASHTAG_RE = R"((?:^|\s)#\w+)";
inline const char* EMOJI_RE = "[\\U0001F300-\\U0001FAFF\xE2\x98\x80-\xE2\x9E\xBF]";

inline const std::set<std::string> SPOKEN_NUMBERS = {
    "zero", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "fifteen", "twenty", "thirty", "forty", "fifty",
    "sixty", "seventy", "eighty", "ninety", "hundred", "thousand", "million",
    "billion", "dozen", "half", "twice", "triple"};
inline const std::set<std::string> MONEY_WORDS = {
    "dollars", "dollar", "bucks", "grand", "percent", "cents",
    "millionaire", "billionaire", "revenue", "profit", "salary", "rent"};
inline const std::set<std::string> STAKES = {
    "stop", "never", "wrong", "mistake", "mistakes", "lost", "lose", "losing",
    "cost", "costs", "broke", "broken", "failed", "failure", "fail", "nobody",
    "no", "not", "don't", "dont", "doesn't", "didn't", "can't", "won't",
    "quit", "quitting", "fired", "deleted", "delete", "killed", "kills", "kill",
    "replaced", "replaces", "cut", "beat", "free", "paid", "charged", "hired",
    "saved", "first",
    "banned", "illegal", "worst", "hate", "hated", "wasted", "waste", "scam",
    "lie", "lied", "lying", "truth", "secret", "hidden", "stole", "stolen",
    "before", "until", "instead", "but", "except", "unless", "problem",
    "risk", "danger", "warning", "regret", "wish", "should", "shouldn't",
    "still", "already", "only", "without", "versus", "vs", "actually"};
inline const std::vector<std::string> WEAK_OPENERS = {
    "so", "ok", "okay", "hey", "hi", "hello", "guys", "yo", "alright",
    "welcome", "today", "basically", "honestly", "look", "listen", "um",
    "just", "let", "lets", "let's", "i wanted", "i want", "one of",
    "have you", "did you", "do you", "are you", "in this", "in today",
    "the thing", "a lot", "there is", "there are", "this is", "it is",
    "as a", "when it", "if you've", "you know"};
inline const std::set<std::string> IMPERATIVES = {
    "stop", "steal", "copy", "delete", "try", "watch", "read", "save",
    "use", "build", "make", "write", "send", "take", "start", "quit",
    "never", "always", "don't", "dont", "do", "put", "run", "check"};
inline const std::vector<std::pair<std::string, std::string>> DEALBREAKERS = {
    {R"((?i)^\s*(?:stop scrolling|don'?t scroll))", "Opens with \"stop scrolling\". Asking for attention proves you have not earned it."},
    {R"((?i)\b(?:in (?:this|today'?s) (?:video|reel)|i'?m going to show you|i'?ll show you how)\b)", "Video preamble. Delete it and open on the payoff."},
    {R"((?i)^\s*(?:hey |hi |what'?s up |welcome ))", "Greeting. Nobody came to the feed to be greeted."},
    {HASHTAG_RE, "Hashtag in the hook. Hashtags belong at the bottom of the caption, if anywhere."},
    {EMOJI_RE, "Emoji in the hook. On-screen text at hook size has room for words or for an emoji, not both."},
};
inline const std::vector<std::string> CHECKS = {"LENGTH", "SPECIFICITY", "STAKES", "FRONTLOAD", "ADDRESS"};

inline double clamp(double n) { return std::max(0.0, std::min(100.0, n)); }
inline std::vector<std::string> words(const std::string& text) { return re::findall(WORD_RE, re::sub(R"((?<=\d),(?=\d))", "", text)); }
inline std::string low1(const std::string& w) { return strip_chars(lower(w), "'’"); }
inline std::vector<std::string> lows(const std::vector<std::string>& w) { std::vector<std::string> o; for (const auto& x : w) o.push_back(low1(x)); return o; }
inline std::string pc(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

using Res = std::pair<double, std::string>;
inline Res check_length(const std::string& text) {   // a hook has to land before the thumb moves
    const long long n = static_cast<long long>(words(text).size());
    const double secs = static_cast<double>(n) / 2.75;   // ~165 words per minute, spoken
    const long long chars = static_cast<long long>(cplen(strip(text)));
    double score;
    if (n >= 5 && n <= 12) score = 100.0;
    else if (n < 5) score = clamp(static_cast<double>(100 - (5 - n) * 20));
    else score = clamp(static_cast<double>(100 - (n - 12) * 11));
    if (chars > 60) score -= 12;                          // two lines of big on-screen text
    return {clamp(score), std::to_string(n) + " words, " + std::to_string(chars) + " chars, ~" + fmt_f(secs, 1) + "s spoken (want 5-12 words)"};
}
inline Res check_specificity(const std::string& text) {   // one concrete thing beats three abstract ones
    std::vector<std::string> nums;
    for (const auto& n : re::findall(NUMBER_RE, text, re::I)) if (!strip(n).empty()) nums.push_back(strip(n));
    std::set<std::string> propers; for (const auto& p : re::findall(PROPER_RE, text)) propers.insert(p);
    std::vector<std::string> spoken;
    for (const auto& w : lows(words(text))) if (SPOKEN_NUMBERS.count(w) || MONEY_WORDS.count(w)) spoken.push_back(w);
    const long long hits = static_cast<long long>(nums.size() + propers.size() + spoken.size());
    const double score = hits == 0 ? 15.0 : clamp(static_cast<double>(45 + hits * 30));
    std::vector<std::string> found;
    for (std::size_t k = 0; k < nums.size() && k < 2; ++k) found.push_back(nums[k]);
    std::size_t c = 0; for (const auto& p : propers) { if (c++ >= 2) break; found.push_back(p); }
    for (std::size_t k = 0; k < spoken.size() && k < 2; ++k) found.push_back(spoken[k]);
    const std::string f = join(found, ", ");
    return {score, std::to_string(hits) + " concrete marker(s)" + (!f.empty() ? ": " + f : std::string(" - no number, no name, nothing checkable"))};
}
inline Res check_stakes(const std::string& text) {   // tension, cost, negation
    std::set<std::string> hs; for (const auto& x : lows(words(text))) if (STAKES.count(x)) hs.insert(x);
    std::vector<std::string> markers(hs.begin(), hs.end());
    if (re::search(R"(\$\s?\d)", text)) markers.push_back("a price");
    const std::size_t n = markers.size();
    const double score = n == 0 ? 20.0 : n == 1 ? 70.0 : 100.0;
    std::vector<std::string> head(markers.begin(), markers.begin() + static_cast<long>(std::min<std::size_t>(4, n)));
    return {clamp(score), std::to_string(n) + " tension marker(s)" + (!markers.empty() ? ": " + join(head, ", ") : std::string(" - nothing is at stake in this line"))};
}
inline Res check_frontload(const std::string& text) {   // the interesting word cannot be in position nine
    const auto w = words(text);
    if (w.empty()) return {0.0, "empty"};
    const auto low = lows(w);
    const std::string opener = low.size() >= 2 ? low[0] + " " + low[1] : low[0];
    double penalty = 0; std::string hit_opener; bool hit = false;
    for (const auto& weak : WEAK_OPENERS) if (starts(opener, weak) || low[0] == weak) { penalty = 30; hit_opener = weak; hit = true; break; }
    long payload = -1;
    const auto& num = re::compile(NUMBER_RE, re::I); const auto& prop = re::compile(PROPER_RE);
    for (std::size_t i = 0; i < low.size(); ++i) {
        const auto& t = low[i];
        if (STAKES.count(t) || SPOKEN_NUMBERS.count(t) || MONEY_WORDS.count(t) || num.match(w[i]) || (i && prop.match(w[i]))) { payload = static_cast<long>(i); break; }
    }
    double base; std::string where;
    if (payload < 0) { base = 30.0; where = "no payload word anywhere in the line"; }
    else if (payload <= 3) { base = 100.0; where = "payload at word " + std::to_string(payload + 1); }
    else if (payload <= 6) { base = 70.0; where = "payload at word " + std::to_string(payload + 1) + ", could move forward"; }
    else { base = 40.0; where = "payload at word " + std::to_string(payload + 1) + ", too late"; }
    return {clamp(base - penalty), where + (hit ? "; weak opener \"" + hit_opener + "\"" : std::string())};
}
inline Res check_address(const std::string& text) {   // aimed at one viewer, or floating in the air
    const std::string low = lower(text);
    const auto w = lows(words(text));
    if (re::search(R"(\b(you|your|you're|youre|yourself)\b)", low)) return {100.0, "speaks to the viewer"};
    if (!w.empty() && IMPERATIVES.count(w[0])) return {90.0, "imperative opener (\"" + w[0] + "\")"};
    if (re::search(R"(\b(i|my|me|we|our)\b)", low)) return {70.0, "first person, no viewer named"};
    return {35.0, "third person, nobody in the room"};
}

// ------------------------------------------------------------------ payoff rule
inline const char* MONEY_RE = R"([$£€]\s?\d[\d,.]*(?:\s?(?:k|m|b|bn)\b|\s(?:million|billion|thousand|grand)\b)?|\b\d[\d,.]*\s?(?:k\s|m\s)?(?:dollars|bucks|grand|euros|pounds)\b|\b(?:a|one|two|three|four|five|six|seven|eight|nine|ten|twenty|fifty|hundred)\s+(?:grand|thousand dollars|million dollars|hundred dollars|bucks|dollars)\b)";
inline const char* FIGURE_RE = R"(\b\d[\d,.]*\s?(?:%|x\b|k\b|m\b)?|\b(?:two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|fifteen|twenty|thirty|forty|fifty|sixty|ninety|hundred|thousand|million|billion|half|double|triple|twice)\b)";
constexpr long LEAD_WORDS = 8;   // about three seconds at 165 wpm

struct Analysis {
    std::string hook; std::map<std::string, Res> checks; std::string weakest; std::vector<std::string> flags;
    double score = 0; std::string verdict; Val formula; Val payoff;
};
inline Val payoff(const std::string& text) {
    const auto money = re::search(MONEY_RE, text, re::I), figure = re::search(FIGURE_RE, text, re::I);
    Val d = Val::dict();
    const re::Match& hit = money ? money : figure;
    if (!hit) { d.d->set("kind", Val()); d.d->set("token", Val()); d.d->set("word", Val()); d.d->set("status", Val::str("MISSING")); return d; }
    const re::Match& first = (!figure || (money && money.start() <= figure.start())) ? money : figure;
    const long long word = static_cast<long long>(split_ws(cpslice(text, 0, first.start())).size()) + 1;
    d.d->set("kind", Val::str(money ? "money" : "number")); d.d->set("token", Val::str(strip(hit.group(0))));
    d.d->set("word", Val::integer(word)); d.d->set("status", Val::str(word <= LEAD_WORDS ? "LEADS" : "LATE"));
    return d;
}
inline std::string payoff_line(const Val& p) {
    const std::string st = get(p, "status").s;
    if (st == "MISSING") return "MISSING  no money and no number. Lead with what the viewer gets, or ask for the real figure";
    const std::string what = get(p, "kind").s == "money" ? "money (" + get(p, "token").s + ")" : "a number (" + get(p, "token").s + ")";
    const std::string w = std::to_string(get(p, "word").i);
    if (st == "LEADS") return "LEADS    " + what + " at word " + w;
    return "LATE     " + what + " at word " + w + ". Move it into the first " + std::to_string(LEAD_WORDS) + " words";
}

// ------------------------------------------------------------------ formula classifier
// count each formula's matching patterns, most hits wins; ties go to the formula listed first
inline const List& formulas() {
    static List f; static bool loaded = false;
    if (!loaded) { f = iterate(get(load_hooks(), "hooks")); for (const auto& x : f) for (const auto& p : iterate(get(x, "match"))) re::compile(re_text(p), re::I); loaded = true; }
    return f;
}
inline Val classify(const std::string& text) {
    const Val* best = nullptr; long long hits = 0;
    for (const auto& f : formulas()) {
        long long n = 0;
        for (const auto& p : iterate(get(f, "match"))) if (re::search(p.s, text, re::I)) ++n;
        if (n > hits) { best = &f; hits = n; }
    }
    Val d = Val::dict();
    if (best) {
        d.d->set("id", get(*best, "id")); d.d->set("name", get(*best, "name")); d.d->set("matched", Val::integer(hits));
        d.d->set("payoff_first", get(*best, "payoff_first", Val::boolean(false)));
    } else { d.d->set("id", Val()); d.d->set("name", Val::str("Unclassified")); d.d->set("matched", Val::integer(0)); d.d->set("payoff_first", Val::boolean(false)); }
    return d;
}

inline Analysis analyse(const std::string& text) {
    Analysis a;
    a.checks["LENGTH"] = check_length(text); a.checks["SPECIFICITY"] = check_specificity(text); a.checks["STAKES"] = check_stakes(text);
    a.checks["FRONTLOAD"] = check_frontload(text); a.checks["ADDRESS"] = check_address(text);
    for (const auto& [pat, msg] : DEALBREAKERS) if (re::search(pat, text)) a.flags.push_back(msg);
    double sum = 0, mn = 1e300; for (const auto& c : CHECKS) { sum += a.checks[c].first; mn = std::min(mn, a.checks[c].first); }
    // the weakest property caps the hook: one bad property is enough for the thumb to keep moving
    double overall = (sum / 5.0) * 0.6 + mn * 0.4 - static_cast<double>(a.flags.size()) * 15;
    overall = clamp(overall);
    a.verdict = overall >= 70 && mn >= 55 && a.flags.empty() ? "STRONG" : overall >= 50 ? "OK" : "WEAK";
    a.formula = classify(text);
    a.hook = strip(text);
    a.weakest = CHECKS[0]; for (const auto& c : CHECKS) if (a.checks[c].first < a.checks[a.weakest].first) a.weakest = c;
    a.score = round_n(overall, 1);
    a.payoff = payoff(text);
    return a;
}
inline Val to_val(const Analysis& a) {
    Val d = Val::dict(); d.d->set("hook", Val::str(a.hook));
    Val ch = Val::dict();
    for (const auto& c : CHECKS) { Val x = Val::dict(); x.d->set("score", Val::flt(round_n(a.checks.at(c).first, 1))); x.d->set("detail", Val::str(a.checks.at(c).second)); ch.d->set(c, x); }
    d.d->set("checks", ch); d.d->set("weakest", Val::str(a.weakest));
    Val fl = Val::list(); for (const auto& f : a.flags) fl.l->push_back(Val::str(f));
    d.d->set("flags", fl); d.d->set("score", Val::flt(a.score)); d.d->set("verdict", Val::str(a.verdict));
    d.d->set("formula", a.formula); d.d->set("payoff", a.payoff);
    return d;
}
inline std::string bar(double score, int width = 24) {
    const long long filled = round_int(score / 100.0 * width);
    return std::string(static_cast<std::size_t>(std::max(0LL, filled)), '#') + std::string(static_cast<std::size_t>(std::max(0LL, width - filled)), '.');
}
inline void render_one(const Analysis& a) {
    print("\nHOOK SCORE"); print(std::string(62, '='));
    print("  \"" + a.hook + "\"\n");
    for (const auto& name : CHECKS) {
        const double s = round_n(a.checks.at(name).first, 1);
        print("  " + ljust(name, 13) + " " + bar(s) + " " + pc("%5.1f", s));
        print("  " + std::string(13, ' ') + " " + a.checks.at(name).second);
    }
    print(std::string(62, '-'));
    print("  " + ljust("HOOK SCORE", 13) + " " + bar(a.score) + " " + pc("%5.1f", a.score) + "   " + a.verdict);
    const long long m = get(a.formula, "matched").i;
    print("  " + ljust("FORMULA", 13) + " " + pystr(get(a.formula, "name")) + (m ? "  (" + std::to_string(m) + " pattern" + (m == 1 ? "" : "s") + " matched)"
                                                                               : std::string("  (no formula matched; that is usually a summary, not a hook)")));
    print("  " + ljust("PAYOFF", 13) + " " + payoff_line(a.payoff));
    for (const auto& f : a.flags) print("\n  DEALBREAKER  " + f);
    if (a.verdict != "STRONG") print("\n  Weakest property: " + a.weakest + ". Fix that one and re-run.");
    print("");
}
inline void render_table(std::vector<Analysis> rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const Analysis& x, const Analysis& y) { return -x.score < -y.score; });
    print("\nHOOK RANKING\n" + std::string(78, '='));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Analysis& r = rows[i];
        const std::string hook = cplen(r.hook) <= 62 ? r.hook : cpslice(r.hook, 0, 59) + "...";
        print(std::string(i == 0 ? "->" : "  ") + " " + pc("%5.1f", r.score) + " " + ljust(r.verdict, 7) + " " + hook);
        print("        " + pystr(get(r.formula, "name")) + " \xC2\xB7 payoff " + get(r.payoff, "status").s + " \xC2\xB7 weakest " + r.weakest + " (" +
              pc("%.0f", round_n(r.checks.at(r.weakest).first, 1)) + ")");
        for (const auto& f : r.flags) print("        dealbreaker: " + f);
    }
    print("\nShoot the top one. If the top one is under 50, none of these is the hook yet.\n");
}
inline void render_list() {
    print("\n26 HOOK FORMULAS  (P = payoff-first)\n" + std::string(78, '='));
    for (const auto& f : formulas()) {
        print("  " + std::string(truthy(get(f, "payoff_first")) ? "P" : " ") + " " + ljust(pystr(get(f, "name")), 22) + " " + ljust(pystr(get(f, "on_screen")), 28) + " " + pystr(get(f, "source")));
        print("     " + pystr(get(f, "example")));
    }
    print("");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("hookscore", "Score short-form hooks on five calibrated properties and name the formula.");
    p.epilog = "Exit 0 when the best hook is STRONG, 1 when none is yet, 2 on bad input.";
    p.pos("input").nargs("?").help("file with one hook per line, or - for stdin");
    p.opt({"--hook"}).append().help("a hook to score (repeatable)");
    p.opt({"--json"}).store_true().help("machine-readable output");
    p.opt({"--list"}).store_true().help("print the formula library and exit");
    const Val a = parse_args(p, argv);
    if (B(a, "list")) {
        if (B(a, "json")) { DumpOpts o; o.indent = 1; print(dumps(Val::list(formulas()), o)); }
        else render_list();
        return 0;
    }
    std::vector<std::string> lines;
    const List hooks = iter(get(a, "hook"));
    const Val inp = get(a, "input"); const std::string input = inp.t == Val::STR ? inp.s : "";
    if (!hooks.empty()) { for (const auto& h : hooks) if (!strip(h.s).empty()) lines.push_back(h.s); }
    else if (!input.empty()) {
        if (input != "-" && !exists(input)) { eprint("no such file: " + input); return 2; }
        const std::string raw = input == "-" ? read_stdin_text() : read_text(input);
        for (const auto& l : splitlines(raw)) if (!strip(l).empty() && !starts(lstrip_ws(l), "#")) lines.push_back(strip(l));
    } else { print_help_stderr(p); return 2; }
    if (lines.empty()) { eprint("nothing to score"); return 2; }
    std::vector<Analysis> rows; for (const auto& l : lines) rows.push_back(analyse(l));
    if (B(a, "json")) {
        DumpOpts o; o.indent = 2; o.ensure_ascii = false;
        if (rows.size() > 1) { Val l = Val::list(); for (const auto& r : rows) l.l->push_back(to_val(r)); print(dumps(l, o)); }
        else print(dumps(to_val(rows[0]), o));
    } else if (rows.size() == 1) render_one(rows[0]);
    else render_table(rows);
    for (const auto& r : rows) if (r.verdict == "STRONG") return 0;
    return 1;
}
PACKTOOL("master-virality/hookscore", main);
}  // namespace packtools::master_hookscore
