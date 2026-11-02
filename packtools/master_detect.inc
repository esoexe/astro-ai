// master-virality/detect - a five-check panel (burstiness, specificity, slop density, typographic fingerprint, voice) that
// scores how machine-written a draft looks, locally, from the text alone. Not GPTZero or any other detector.
// C++ port of master-skills/skills/virality/tools/detect.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::master_detect {
using namespace pyrt;
using namespace packtools::master;

inline const char* SENT_RE = R"([^.!?\n]+[.!?]*)";
inline const char* WORD_RE = R"([A-Za-z']+)";
inline const char* CONTRACTIONS = R"(\b\w+'(?:s|t|re|ve|ll|d|m)\b)";
inline const char* PRONOUNS = R"(\b(i|me|my|mine|we|us|our|you|your)\b)";
inline const char* NUMBERS = R"(\b\d[\d,.]*%?\b|\$\d)";
inline const char* PROPER = R"((?<![.!?]\s)(?<!^)\b[A-Z][a-z]{2,}\b)";
inline const std::vector<std::string> CHECKS = {"BURSTINESS", "SPECIFICITY", "SLOP DENSITY", "FINGERPRINT", "VOICE"};

inline double clamp(double n) { return std::max(0.0, std::min(100.0, n)); }
inline double scale(double value, double human, double machine) {   // human -> 100, machine -> 0
    if (human == machine) return 50.0;
    return clamp((value - machine) / (human - machine) * 100);
}
inline std::vector<std::string> sentences(const std::string& text) {
    std::vector<std::string> o; for (const auto& s : re::findall(SENT_RE, text)) if (split_ws(s).size() > 2) o.push_back(strip(s));
    return o;
}
inline std::vector<std::string> words(const std::string& text) { return re::findall(WORD_RE, text); }
inline std::size_t count_sub(const std::string& s, const std::string& sub) {   // str.count(sub), non-overlapping
    if (sub.empty()) return cplen(s) + 1;
    std::size_t n = 0, k = 0; while ((k = s.find(sub, k)) != std::string::npos) { ++n; k += sub.size(); }
    return n;
}
using Res = std::pair<double, std::string>;

inline Res check_burstiness(const std::string& text) {   // humans vary sentence length hard; models write even
    std::vector<long long> lens; for (const auto& s : sentences(text)) lens.push_back(static_cast<long long>(split_ws(s).size()));
    if (lens.size() < 4) return {50.0, "too short to judge"};
    const double mean = int_mean(lens);
    const double cv = mean != 0.0 ? int_pstdev(lens) / mean : 0.0;
    return {scale(cv, 0.70, 0.22), "variation " + fmt_f(cv, 2) + " across " + std::to_string(lens.size()) + " sentences (want 0.55+)"};
}
inline Res check_specificity(const std::string& text) {   // numbers, names and concrete nouns
    const auto w = words(text);
    if (w.size() < 25) return {50.0, "too short to judge"};
    const double per100 = 100.0 / static_cast<double>(w.size());
    std::set<std::string> propers; for (const auto& p : re::findall(PROPER, text, re::M)) propers.insert(p);
    const long long hits = static_cast<long long>(re::findall(NUMBERS, text).size() + propers.size());
    const double density = static_cast<double>(hits) * per100;
    return {scale(density, 6.0, 0.5), std::to_string(hits) + " concrete markers, " + fmt_f(density, 1) + " per 100 words (want 4+)"};
}
inline Res check_slop(const std::string& text, const Val& lex) {   // stock vocabulary density against the lexicon
    const auto w = words(text);
    if (w.empty()) return {50.0, "empty"};
    long long hits = 0; std::vector<std::string> found;
    List entries = iterate(get(lex, "words")); for (const auto& e : iterate(get(lex, "phrases"))) entries.push_back(e);
    for (const auto& e : entries) {
        const std::string find = re_text(get(e, "find"));
        const long long n = static_cast<long long>(re::findall(slop_pattern(find), text, re::I).size());
        if (n) { hits += n; found.push_back(find); }
    }
    const double density = static_cast<double>(hits * 100) / static_cast<double>(w.size());
    std::string detail = std::to_string(hits) + " stock terms, " + fmt_f(density, 1) + " per 100 words";
    if (!found.empty()) {
        std::vector<std::string> s = found; std::sort(s.begin(), s.end()); s.resize(std::min<std::size_t>(4, s.size()));
        detail += " (" + join(s, ", ") + (found.size() > 4 ? ", ..." : "") + ")";
    }
    return {scale(density, 0.0, 4.0), detail};
}
inline Res check_fingerprint(const std::string& text) {   // characters a phone keyboard does not produce
    long long invisible = 0; for (const uint32_t c : ucase_detail::u32(text)) if (std::string(uni::category(c)) == "Cf") ++invisible;
    const long long em = static_cast<long long>(count_sub(text, "\xE2\x80\x94"));
    long long curly = 0; for (const char* c : {"\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D"}) curly += static_cast<long long>(count_sub(text, c));
    const long long ellip = static_cast<long long>(count_sub(text, "\xE2\x80\xA6"));
    long long nbsp = 0; for (const char* c : {"\xC2\xA0", "\xE2\x80\xAF", "\xE2\x80\x89"}) nbsp += static_cast<long long>(count_sub(text, c));
    const long long total = invisible * 4 + em * 2 + curly + ellip + nbsp;
    const double per1k = static_cast<double>(total * 1000) / static_cast<double>(std::max<std::size_t>(cplen(text), 1));
    return {scale(per1k, 0.0, 12.0), std::to_string(invisible) + " invisible, " + std::to_string(em) + " em dash, " + std::to_string(curly) + " curly quote, " +
                                     std::to_string(ellip) + " ellipsis, " + std::to_string(nbsp) + " hard space"};
}
inline Res check_voice(const std::string& text, const Val& lex) {   // contractions, person, and the shapes models default to
    const auto w = words(text);
    if (w.size() < 25) return {50.0, "too short to judge"};
    const double per100 = 100.0 / static_cast<double>(w.size());
    const double contractions = static_cast<double>(re::findall(CONTRACTIONS, text, re::I).size()) * per100;
    const double person = static_cast<double>(re::findall(PRONOUNS, text, re::I).size()) * per100;
    long long tells = 0; std::vector<std::string> names;
    for (const auto& s : iterate(get(lex, "structures"))) {
        long long n;
        try { n = static_cast<long long>(re::compile(re_text(get(s, "regex")), re::M).findall(text).size()); }
        catch (const PyErr& e) { if (e.type != "re.error") throw; continue; }
        if (n) { tells += n; names.push_back(pystr(get(s, "id"))); }
    }
    std::vector<long long> bullets; for (const auto& b : re::findall(R"((?m)^\s*[-*•]\s+(.+)$)", text)) bullets.push_back(static_cast<long long>(split_ws(b).size()));
    const bool uniform = bullets.size() >= 3 && int_pstdev(bullets) < 1.6;
    double score = scale(contractions, 3.0, 0.0) * 0.35 + scale(person, 8.0, 1.0) * 0.35 + clamp(static_cast<double>(100 - tells * 22)) * 0.30;
    if (uniform) { score -= 12; names.push_back("uniform-bullets"); }
    std::string detail = fmt_f(contractions, 1) + " contractions, " + fmt_f(person, 1) + " personal pronouns per 100 words, " + std::to_string(tells) + " structural tell(s)";
    if (!names.empty()) { std::vector<std::string> h(names.begin(), names.begin() + static_cast<long>(std::min<std::size_t>(4, names.size()))); detail += " [" + join(h, ", ") + "]"; }
    return {clamp(score), detail};
}

struct Run { std::map<std::string, Res> results; double overall = 0; std::string verdict; };
inline Run run(const std::string& text, const Val& lex) {
    Run r;
    r.results["BURSTINESS"] = check_burstiness(text); r.results["SPECIFICITY"] = check_specificity(text); r.results["SLOP DENSITY"] = check_slop(text, lex);
    r.results["FINGERPRINT"] = check_fingerprint(text); r.results["VOICE"] = check_voice(text, lex);
    std::vector<double> scores; for (const auto& c : CHECKS) scores.push_back(r.results[c].first);
    const double mn = *std::min_element(scores.begin(), scores.end());
    // the weakest check drags the verdict: a detector only needs one signal
    r.overall = stat_mean(scores) * 0.6 + mn * 0.4;
    r.verdict = r.overall >= 70 && mn >= 55 ? "PASS" : r.overall >= 50 ? "REVIEW" : "FLAGGED";
    return r;
}
inline std::string bar(double score, int width = 24) {
    const long long filled = round_int(score / 100 * width);
    return std::string(static_cast<std::size_t>(std::max(0LL, filled)), '#') + std::string(static_cast<std::size_t>(std::max(0LL, width - filled)), '.');
}
inline std::string f51(double v) { char b[64]; std::snprintf(b, sizeof b, "%5.1f", v); return b; }
inline void render(const Run& r, const std::string& label) {
    const std::string title = "AI DETECTION PANEL" + (label.empty() ? std::string() : "  -  " + label);
    print("\n" + title);
    print(std::string(std::max<std::size_t>(cplen(title), 62), '='));
    for (const auto& name : CHECKS) {
        const auto& [score, detail] = r.results.at(name);
        print("  " + ljust(name, 13) + " " + bar(score) + " " + f51(score));
        print("  " + std::string(13, ' ') + " " + detail);
    }
    print(std::string(62, '-'));
    print("  " + ljust("HUMAN SCORE", 13) + " " + bar(r.overall) + " " + f51(r.overall) + "   " + r.verdict);
    if (r.verdict != "PASS") {
        std::string weakest = CHECKS[0];
        for (const auto& c : CHECKS) if (r.results.at(c).first < r.results.at(weakest).first) weakest = c;
        print("\n  Weakest signal: " + weakest + ". Fix that first.");
    }
    print("");
}

// lexicon: the skill-relative default for --lexicon (tools/slop.json in the virality copy, slop.json next to li-human/ig-human)
inline int main_with(const std::vector<std::string>& argv, const std::string& lexicon) {
    Parser p("detect", "Score how machine-written a draft looks.");
    p.pos("input").nargs("?").def("-").help("file, or - for stdin");
    p.pos("compare").nargs("?").help("second file, to show before/after");
    p.opt({"--json"}).store_true();
    p.opt({"--lexicon"}).def(Val::str(skill_file(lexicon)));
    const Val a = parse_args(p, argv);
    const Val lex = loads(read_text_utf8(S(a, "lexicon")));
    auto read = [](const std::string& path) { return path == "-" ? read_stdin_text() : read_text_utf8(path); };
    std::vector<std::pair<std::string, std::string>> targets;
    targets.emplace_back(S(a, "input"), read(S(a, "input")));
    const std::string compare = S(a, "compare");
    if (!compare.empty()) targets.emplace_back(compare, read(compare));
    std::vector<Run> runs; Val payload = Val::list();
    for (const auto& [name, text] : targets) {
        const Run r = run(text, lex); runs.push_back(r);
        Val d = Val::dict(); d.d->set("source", Val::str(name));
        Val ch = Val::dict();
        for (const auto& c : CHECKS) { Val x = Val::dict(); x.d->set("score", Val::flt(round_n(r.results.at(c).first, 1))); x.d->set("detail", Val::str(r.results.at(c).second)); ch.d->set(c, x); }
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
inline int main(const std::vector<std::string>& argv) { return main_with(argv, "tools/slop.json"); }
PACKTOOL("master-virality/detect", main);
}  // namespace packtools::master_detect
