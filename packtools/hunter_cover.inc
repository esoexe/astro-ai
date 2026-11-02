// hunter-cover/cover - checks a cover letter before anyone reads it: length, greeting, the company / role / team named,
// one concrete proof point, no cliches or placeholders, no other company from the pipeline, every number in the resume,
// reading grade, an ask at the end, sentence openers. Each check is PASS, FAIL or WARN; exit 0 when nothing FAILs.
// C++ port of hunter-skill/skills/hunter-cover/cover.py (Jake Schincariol, MIT); same output, same exit codes.
// std::regex has no lookbehind, so the sentence split, the number scan and the name match are written out by hand with
// the same semantics (a lookbehind only looks at the one character before the match).
namespace packtools::hunter_cover {
using namespace pyrt;
struct CoverError : std::runtime_error { using std::runtime_error::runtime_error; };
constexpr int WORDS_MIN = 150, WORDS_MAX = 400;
constexpr double GRADE_MAX = 12.0;
inline const std::vector<std::string> CLICHES = {
    "to whom it may concern", "dear sir or madam", "dear sir/madam", "i am writing to express my interest",
    "i am writing to apply", "please accept this letter", "i believe i would be a great fit",
    "i would be a great fit", "perfect fit", "perfect candidate", "ideal candidate", "dream job",
    "team player", "hard-working", "hardworking", "hard worker", "go-getter", "self-starter",
    "think outside the box", "outside the box", "synergy", "synergies", "results-driven",
    "detail-oriented", "detail oriented", "passionate about", "i am passionate", "proven track record",
    "track record of success", "fast-paced environment", "fast paced environment", "wear many hats",
    "hit the ground running", "dynamic individual", "highly motivated", "motivated individual",
    "excellent communication skills", "strong work ethic", "i am confident that", "thank you for your consideration",
    "leverage my skills", "add value", "value-add", "rockstar", "ninja", "guru", "best of breed",
    "in today's fast-paced", "i have always wanted to work", "since i was a child", "game changer", "game-changer",
};
using std::regex_constants::icase;
inline const std::regex PLACEHOLDER_RE(R"(\[[A-Z][^\]]{0,40}\]|\{\{?[a-z_ ]{1,30}\}?\}|<[A-Z][a-zA-Z ]{1,30}>|\bX{3,}\b|\bTODO\b|\bTBD\b|\[fill:[^\]]*\])");
inline const std::regex ACTION_RE(R"(\b(built|rebuilt|rewrote|shipped|launched|led|cut|cutting|reduced|increased|lifted|raised|grew|saved|improved|doubled|halved|delivered|designed|created|wrote|ran|scaled|migrated|moved|won|sold|closed|drove|automated|fixed|dropped|lowered|serves|handles|up|down)\b)", std::regex::ECMAScript | icase);
inline const std::regex ASK_RE(R"(\?|\b(chat|talk|conversation|discuss|interview|call|meet|coffee|hear from you|next steps?|speak)\b)", std::regex::ECMAScript | icase);
inline const std::regex SIGNOFF_RE(R"(^\s*(thank you|thanks|best|sincerely|regards|kind regards|warmly|cheers|all the best|yours)\b)", std::regex::ECMAScript | icase);
inline const std::regex GREETING_LINE(R"(^(dear|hi|hello|hey)\b[^\n]*)", std::regex::ECMAScript | icase);

inline bool az(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool dig(char c) { return c >= '0' && c <= '9'; }
inline std::vector<std::string> findall(const std::string& s, const std::regex& re) {
    std::vector<std::string> o;
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it) o.push_back((*it)[0].str());
    return o;
}
inline std::vector<std::string> words(const std::string& text) { static const std::regex W(R"([A-Za-z0-9][A-Za-z0-9'%$.,-]*)"); return findall(text, W); }
inline std::vector<std::string> nums(const std::string& s) {   // NUM_RE = (?<![A-Za-z])\d+(?:[.,]\d+)*
    std::vector<std::string> o;
    for (std::size_t i = 0; i < s.size();) {
        if (!dig(s[i]) || (i > 0 && az(s[i - 1]))) { ++i; continue; }
        std::size_t j = i; while (j < s.size() && dig(s[j])) ++j;
        while (j + 1 < s.size() && (s[j] == '.' || s[j] == ',') && dig(s[j + 1])) { ++j; while (j < s.size() && dig(s[j])) ++j; }
        o.push_back(s.substr(i, j - i)); i = j;
    }
    return o;
}
inline std::string collapse_ws(const std::string& s) {   // re.sub(r"\s+", " ", s)
    std::string o; bool sp = false;
    for (const auto& ch : cps(s)) { if (py_space(cp_of(ch))) { if (!sp) o += ' '; sp = true; } else { o += ch; sp = false; } }
    return o;
}
inline std::vector<std::string> sentences(const std::string& text) {   // split on (?<=[.!?])\s+(?=[A-Z"'(]) after flattening
    const std::string flat = strip(collapse_ws(text));
    std::vector<std::string> parts; std::size_t start = 0;
    for (std::size_t i = 1; i + 1 < flat.size(); ++i) {   // after the collapse every whitespace run is one space
        if (flat[i] != ' ') continue;
        const char p = flat[i - 1], n = flat[i + 1];
        if ((p == '.' || p == '!' || p == '?') && ((n >= 'A' && n <= 'Z') || n == '"' || n == '\'' || n == '(')) { parts.push_back(flat.substr(start, i - start)); start = i + 1; }
    }
    parts.push_back(flat.substr(start));
    std::vector<std::string> o;
    for (const auto& p : parts) if (words(p).size() >= 3) o.push_back(strip(p));
    return o;
}
inline int syllables(const std::string& word) {
    std::string w; for (char c : lower(word)) if (c >= 'a' && c <= 'z') w += c;
    if (w.empty()) return 0;
    if (w.size() <= 3) return 1;
    static const std::regex END(R"((?:[^laeiouy]es|ed|[^laeiouy]e)$)"), Y(R"(^y)"), V(R"([aeiouy]{1,2})");
    w = std::regex_replace(w, END, "", std::regex_constants::format_first_only);
    w = std::regex_replace(w, Y, "", std::regex_constants::format_first_only);
    return std::max(1, static_cast<int>(findall(w, V).size()));
}
struct Reading { double grade, ease, wps; };
inline Reading reading(const std::string& text) {
    auto sents = sentences(text); if (sents.empty()) sents = {text};
    static const std::regex AZ(R"([A-Za-z]+)");
    const auto ws = findall(text, AZ);
    if (ws.empty()) return {0.0, 100.0, 0.0};
    long long syl = 0; for (const auto& w : ws) syl += syllables(w);
    const double wps = static_cast<double>(ws.size()) / static_cast<double>(sents.size());
    const double spw = static_cast<double>(syl) / static_cast<double>(ws.size());
    const double grade = 0.39 * wps + 11.8 * spw - 15.59, ease = 206.835 - 1.015 * wps - 84.6 * spw;
    return {round_n(grade, 1), round_n(ease, 1), round_n(wps, 1)};
}
inline std::string body_of(const std::string& text0) {   // without a markdown title, a subject line or HTML comments
    static const std::regex COMMENT(R"(<!--[\s\S]*?-->)"), SKIP(R"(^\s*(#|subject:))", std::regex::ECMAScript | icase);
    const std::string text = std::regex_replace(text0, COMMENT, "");
    std::vector<std::string> lines;
    for (const auto& l : splitlines(text)) if (!std::regex_search(l, SKIP)) lines.push_back(l);
    return strip(join(lines, "\n"));
}
inline std::vector<std::string> paragraphs(const std::string& text) {
    static const std::regex SEP(R"(\n\s*\n)");
    std::vector<std::string> o;
    for (auto it = std::sregex_token_iterator(text.begin(), text.end(), SEP, -1); it != std::sregex_token_iterator(); ++it) { const std::string p = strip(it->str()); if (!p.empty()) o.push_back(p); }
    return o;
}
inline std::vector<std::string> lower_cps(const std::string& s) { std::vector<std::string> o; for (const auto& c : cps(s)) o.push_back(lower(c)); return o; }
inline bool alnum_ascii(const std::string& c) { return c.size() == 1 && (az(c[0]) || dig(c[0])); }
inline bool contains_i(const std::string& text, const std::string& needle, bool bounded) {   // re.search(re.escape(n), text, re.I), optionally (?<![A-Za-z0-9])..(?![A-Za-z0-9])
    const auto t = lower_cps(text), n = lower_cps(needle);
    if (n.empty()) return bounded ? true : true;
    for (std::size_t i = 0; i + n.size() <= t.size(); ++i) {
        if (!std::equal(n.begin(), n.end(), t.begin() + static_cast<long>(i))) continue;
        if (!bounded) return true;
        if (i > 0 && alnum_ascii(t[i - 1])) continue;
        if (i + n.size() < t.size() && alnum_ascii(t[i + n.size()])) continue;
        return true;
    }
    return false;
}
inline bool mentions(const std::string& text, const std::string& name) { return contains_i(text, name, true); }
inline std::vector<std::string> other_companies(const std::string& pipeline_path, const std::string& company) {
    if (pipeline_path.empty() || !exists(pipeline_path)) return {};
    const Val data = loads(read_text(pipeline_path));
    std::vector<std::string> names;
    const Val roles = dget(data, "roles");
    if (roles.t == Val::LIST) for (const Val& r : *roles.l) {
        const Val c0 = dget(r, "company");
        const std::string c = strip(truthy(c0) ? pystr(c0) : "");
        if (!c.empty() && lower(c) != lower(company) && std::find(names.begin(), names.end(), c) == names.end()) names.push_back(c);
    }
    return names;
}
inline bool isupper_py(const std::string& w) { bool cased = false; for (char c : w) { if (c >= 'a' && c <= 'z') return false; if (c >= 'A' && c <= 'Z') cased = true; } return cased; }
inline Val check(const std::string& text, const std::string& company, const std::string& role, const std::string& team,
                 const std::optional<std::string>& resume_text, const std::vector<std::string>& others) {
    const std::string body = body_of(text), low = lower(body);
    Val results = Val::list();
    const auto add = [&](const std::string& name, const std::string& status, const std::string& detail) {
        Val x = Val::dict(); x.d->set("check", Val::str(name)); x.d->set("status", Val::str(status)); x.d->set("detail", Val::str(detail)); results.l->push_back(x);
    };
    const long long n = static_cast<long long>(words(body).size());
    const std::string span = std::to_string(WORDS_MIN) + " to " + std::to_string(WORDS_MAX);
    if (n < WORDS_MIN) add("length", "FAIL", std::to_string(n) + " words: too thin to be specific (aim for " + span + ")");
    else if (n > WORDS_MAX) add("length", "FAIL", std::to_string(n) + " words: over one page of reading (aim for " + span + ")");
    else add("length", "PASS", std::to_string(n) + " words");

    std::optional<std::string> g;   // GREETING_RE with re.M: the first line that starts (after spaces) with a greeting word
    const auto blines = splitlines(body);
    { std::size_t a = 0;
      while (a <= body.size()) {
          std::size_t e = body.find('\n', a); if (e == std::string::npos) e = body.size();
          const std::string line = body.substr(a, e - a);
          std::size_t k = 0; while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
          std::smatch m; const std::string rest = line.substr(k);
          if (std::regex_search(rest, m, GREETING_LINE)) { g = strip(m[0].str()); break; }
          a = e + 1;
      } }
    const std::string first = lower(strip(blines.empty() ? std::string() : blines.front()));
    if (low.find("to whom it may concern") != std::string::npos || low.find("dear sir or madam") != std::string::npos || low.find("dear sir/madam") != std::string::npos)
        add("greeting", "FAIL", "generic greeting: use the hiring manager's name or the team");
    else if (g) add("greeting", "PASS", *g);
    else add("greeting", "WARN", "no greeting found (e.g. \"Hi Maya,\" or \"Dear Google Cloud team,\"); first line: " + cpslice(first, 0, 60));

    if (!company.empty()) add("company", mentions(body, company) ? "PASS" : "FAIL", mentions(body, company) ? "names " + company : "never names " + company);
    if (!role.empty()) {
        static const std::regex RW(R"([A-Za-z0-9+#]+)");
        std::vector<std::string> rwords, missing;
        for (const auto& w : findall(role, RW)) if (w.size() > 2 || isupper_py(w)) rwords.push_back(w);
        for (const auto& w : rwords) if (!mentions(body, w)) missing.push_back(w);
        if (missing.empty() && contains_i(body, role, false)) add("role", "PASS", "names the " + role + " role");
        else if (missing.empty()) add("role", "PASS", "names the role's words (" + join(rwords, ", ") + ")");
        else add("role", "FAIL", "the role title is not named (missing: " + join(missing, ", ") + ")");
    }
    if (!team.empty()) {
        std::vector<std::string> parts, hit; std::string cur;
        for (char c : team + ",") { if (c == ',' || c == '/' || c == ';') { const std::string p = strip(cur); if (!p.empty()) parts.push_back(p); cur.clear(); } else cur += c; }
        for (const auto& p : parts) if (mentions(body, p)) hit.push_back(p);
        add("team", hit.empty() ? "FAIL" : "PASS", hit.empty() ? "never names the team (" + team + ")" : "names " + join(hit, ", "));
    }
    const auto sents = sentences(body);
    std::vector<std::string> proofs;
    for (const auto& s : sents) if (!nums(s).empty() && std::regex_search(s, ACTION_RE)) proofs.push_back(s);
    static const std::regex STRONG(R"(\d\s*%|\$\s*\d|\d+x\b)");
    std::stable_sort(proofs.begin(), proofs.end(), [](const std::string& a, const std::string& b) { return std::regex_search(a, STRONG) && !std::regex_search(b, STRONG); });
    if (!proofs.empty()) add("proof", "PASS", std::to_string(proofs.size()) + " proof point(s), e.g. " + cpslice(proofs.front(), 0, 90));
    else add("proof", "FAIL", "no sentence with a number and what you did: add one concrete result from your resume");

    std::vector<std::string> found; for (const auto& c : CLICHES) if (low.find(c) != std::string::npos) found.push_back("\"" + c + "\"");
    add("cliches", found.empty() ? "PASS" : "FAIL", found.empty() ? "none" : "cut: " + join(found, "; "));
    std::set<std::string> phs; for (const auto& m : findall(body, PLACEHOLDER_RE)) phs.insert(m);
    const std::vector<std::string> ph(phs.begin(), phs.end());
    add("placeholders", ph.empty() ? "PASS" : "FAIL", ph.empty() ? "none" : "left in: " + join(ph, ", "));
    if (!others.empty()) {
        std::vector<std::string> named; for (const auto& o : others) if (mentions(body, o)) named.push_back(o);
        add("other names", named.empty() ? "PASS" : "FAIL", named.empty() ? "no other company named" : "names " + join(named, ", ") + ": copied from another letter?");
    }
    if (resume_text) {
        std::set<std::string> have; for (const auto& x : nums(*resume_text)) have.insert(replace_all(x, ",", ""));
        std::set<std::string> nw; for (const auto& x : nums(body)) { const std::string y = replace_all(x, ",", ""); if (!have.count(y)) nw.insert(y); }
        std::vector<std::string> newv(nw.begin(), nw.end());
        std::sort(newv.begin(), newv.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
        add("numbers", newv.empty() ? "PASS" : "FAIL", newv.empty() ? "every number is in the resume" : "not in your resume, confirm or cut: " + join(newv, ", "));
    }
    const Reading r = reading(body);
    add("reading", r.grade > GRADE_MAX ? "WARN" : "PASS", "grade " + fmt_f(r.grade, 1) + " (Flesch-Kincaid), " + fmt_f(r.wps, 1) + " words a sentence");
    std::vector<std::string> closing;
    for (const auto& p : paragraphs(body)) if (!std::regex_search(p, SIGNOFF_RE) || words(p).size() > 8) closing.push_back(p);
    const std::string last = closing.empty() ? "" : closing.back();
    const bool ask = std::regex_search(last, ASK_RE);
    add("ask", ask ? "PASS" : "WARN", ask ? "ends with a next step" : "the last paragraph does not ask for anything (a chat, a call?)");
    static const std::regex ISTART(R"(^I\b)");
    long long i_starts = 0; for (const auto& s : sents) if (std::regex_search(s, ISTART)) ++i_starts;
    const std::string od = std::to_string(i_starts) + " of " + std::to_string(sents.size()) + " sentences start with \"I\"";
    add("openers", !sents.empty() && i_starts * 2 >= static_cast<long long>(sents.size()) ? "WARN" : "PASS", od);
    long long fails = 0, warns = 0;
    for (const Val& x : *results.l) { if (get(x, "status").s == "FAIL") ++fails; if (get(x, "status").s == "WARN") ++warns; }
    Val rd = Val::dict(); rd.d->set("grade", Val::flt(r.grade)); rd.d->set("ease", Val::flt(r.ease)); rd.d->set("words_per_sentence", Val::flt(r.wps));
    Val res = Val::dict();
    res.d->set("ready", Val::boolean(fails == 0)); res.d->set("fails", Val::integer(fails)); res.d->set("warns", Val::integer(warns));
    res.d->set("words", Val::integer(n)); res.d->set("reading", rd); res.d->set("checks", results);
    return res;
}
inline std::string render(const Val& res, const std::string& path) {
    const long long f = get(res, "fails").i;
    const std::string head = truthy(get(res, "ready")) ? "READY" : "FIX " + std::to_string(f) + " THING" + (f == 1 ? "" : "S");
    std::vector<std::string> o = {"Cover letter check" + (path.empty() ? "" : " (" + path + ")") + ": " + head};
    for (const Val& c : *get(res, "checks").l) o.push_back("  " + ljust(pystr(get(c, "status")), 4) + "  " + ljust(pystr(get(c, "check")), 13) + " " + pystr(get(c, "detail")));
    return join(o, "\n");
}
inline std::string read_(const std::string& path) {
    if (!exists(path)) throw CoverError("no file at " + path);
    return read_text(path);
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("cover", "Check a cover letter: specific, true, short, no cliches.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("check"); s.help_line = "run every check (exit 1 if any FAILs)";
      s.pos("letter"); s.opt({"--company"}); s.opt({"--role"}); s.opt({"--team"});
      s.opt({"--resume"}).help("the tailored resume: every number in the letter must be in it");
      s.opt({"--pipeline"}).help("hunter/pipeline.json: flags other companies named by mistake");
      s.opt({"--other"}).help("comma-separated company names that must not appear");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("cliches"); s.help_line = "print the banned phrases"; }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    if (cmd == "cliches") { print(join(CLICHES, "\n")); return 0; }
    Val res;
    try {
        auto others = other_companies(S(a, "pipeline"), S(a, "company"));
        if (truthy(get(a, "other"))) {
            std::vector<std::string> add;
            for (const auto& o : split(S(a, "other"), ",")) { const std::string t = strip(o); if (!t.empty() && std::find(others.begin(), others.end(), t) == others.end()) add.push_back(t); }
            for (const auto& t : add) others.push_back(t);
        }
        std::optional<std::string> resume;
        if (truthy(get(a, "resume"))) resume = read_(S(a, "resume"));
        res = check(read_(S(a, "letter")), S(a, "company"), S(a, "role"), S(a, "team"), resume, others);
    } catch (const CoverError& e) {
        eprint(std::string("cover: ") + e.what()); return 2;
    } catch (const PyErr& e) {   // ValueError and its subclasses (JSONDecodeError, UnicodeDecodeError)
        if (e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError" && e.type != "JSONDecodeError" && e.type != "UnicodeDecodeError") throw;
        eprint(std::string("cover: ") + e.what()); return 2;
    }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(res, o)); }
    else print(render(res, basename(S(a, "letter"))));
    return truthy(get(res, "ready")) ? 0 : 1;
}
PACKTOOL("hunter-cover/cover", main);
}  // namespace packtools::hunter_cover
