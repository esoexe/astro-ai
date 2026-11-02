// hunter-interview/star: mock interviews from a sourced question bank and a STAR check on every answer, a C++ port of
// hunter-skill/skills/hunter-interview/star.py (MIT, Jake Schincariol). Same subcommands, output, JSON and exit codes;
// --seed shuffles with CPython's random.Random (the timemachine port's PyRandom), so a seed picks the same questions.
//
// Regex notes. Python's \b, \w, \s and \d are Unicode-aware; std::regex works on bytes. So each pattern runs over a
// "shadow" of the text with one byte per code point: ASCII stays, other letters/numbers (Python's \w) become '_',
// other decimal digits '5', other whitespace Python's \s matches '\f' (std::regex's \s and . match it, a literal ' ' does
// not, as in Python), anything else '\x01'. For the re.I patterns the four non-ASCII letters
// re.I folds onto ASCII (U+0130, U+0131 -> i, U+017F -> s, U+212A -> k) become those letters. Match positions in the
// shadow are code point indexes into the text, so matched text (I + verb) is cut from the original.
namespace packtools::hunter_interview {
using namespace pyrt;
using packtools::daytrade_timemachine::PyRandom;

constexpr int WPM = 150;
struct StarError : std::runtime_error { using std::runtime_error::runtime_error; };

inline std::string bank_path() { return skill_file("questions.json"); }

const std::regex SITUATION_RE(
    "\\b(when (i|we|our|the|my)|while|during|last (year|month|quarter|spring|summer|fall|autumn|winter|week)|"
    "in (19|20)\\d\\d|our team|my team|the team|we were|we had|there was|there were|the company|our company|"
    "a customer|our customers|the project|the situation|back in|a few (months|years) ago|my first)\\b", std::regex::icase);
const std::regex SITUATION_AT_RE("\\b(At|at) [A-Z][\\w&.-]+");
const std::regex TASK_RE(
    "\\b(my (job|task|goal|role|responsibility|mandate|aim|challenge) (was|is|became)|"
    "i was (responsible|asked|tasked|supposed|assigned|on the hook|the one)|i needed to|i had to|"
    "we needed to|we had to|the goal was|the task was|the target was|i owned|i set out to|the problem was|"
    "i wanted to|i was trying to|i decided to)\\b", std::regex::icase);
const std::regex I_VERB_RE("\\bI\\s+(?:then\\s+|also\\s+|first\\s+|quickly\\s+|personally\\s+|finally\\s+|immediately\\s+)?([a-z]+)");
const std::regex RESULT_RE(
    "\\b(as a result|the result|resulted in|result was|the outcome|in the end|ended up|by the end|afterwards|"
    "since then|which (cut|reduced|increased|improved|saved|led|lifted|dropped|grew|doubled|halved|meant)|"
    "that (cut|reduced|increased|improved|saved|lifted|dropped|meant)|so (that )?(we|the|our|it))\\b", std::regex::icase);
const std::regex OUTCOME_RE(
    "\\b(cut|reduced|increased|improved|saved|lifted|dropped|fell|rose|grew|doubled|halved|tripled|shipped|"
    "launched|delivered|won|hit|beat|passed|stopped|eliminated|recovered)\\b", std::regex::icase);
// NUMBER_RE minus its first branch ((?<![A-Za-z])\d), which digit_after_nonletter() does by hand
const std::regex NUMBER_WORD_RE(
    "\\b(percent|half|halved|double|doubled|twice|triple|tripled|dozen|hundred|thousand|million|"
    "billion|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|fifteen|twenty|thirty|forty|fifty|"
    "sixty|seventy|eighty|ninety)\\b", std::regex::icase);
const std::regex WORD_RE("\\b[\\w'%$-]+\\b");
const std::regex I_RE("\\bI\\b");
const std::regex WE_RE("\\bwe\\b", std::regex::icase);

const std::set<std::string> IRREGULAR = {"built", "wrote", "led", "ran", "made", "took", "set", "sent", "sat", "met", "spoke",
    "gave", "chose", "found", "drove", "rewrote", "rebuilt", "brought", "began", "put", "cut", "split", "kept", "held", "told",
    "taught", "went", "got", "did", "shipped", "paired", "dug", "fought", "won", "sold", "bought", "broke", "rode"};
const std::set<std::string> NOT_ACTIONS = {"needed", "wanted", "was", "were", "had", "felt", "thought", "believed", "hoped",
    "liked", "loved", "guessed", "realized", "knew", "noticed", "wished", "expected", "assumed"};

inline bool word_cp(uint32_t c) {   // Python's \w for str patterns: letters, numbers, underscore
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    const char* cat = uni::category(c);
    return cat[0] == 'L' || cat[0] == 'N';
}
inline std::string shadow(const std::string& text, bool icase) {
    std::string o;
    for (const auto& ch : cps(text)) {
        const uint32_t c = cp_of(ch);
        if ((c >= 0x1C && c <= 0x1F) || c == '\r') o += '\f';   // \s in Python str patterns, not a literal ' '; Python's . matches \r, ECMAScript's does not
        else if (c < 0x80) o += static_cast<char>(c);
        else if (icase && (c == 0x130 || c == 0x131)) o += 'i';
        else if (icase && c == 0x17F) o += 's';
        else if (icase && c == 0x212A) o += 'k';
        else if (py_space(c)) o += '\f';
        else if (std::string(uni::category(c)) == "Nd") o += '5';
        else if (word_cp(c)) o += '_';
        else o += '\x01';
    }
    return o;
}
struct Text {   // a string with both shadows
    std::string s, cs, ci;
    explicit Text(const std::string& t) : s(t), cs(shadow(t, false)), ci(shadow(t, true)) {}
};
inline bool search(const std::regex& re, const Text& t, bool icase) { return std::regex_search(icase ? t.ci : t.cs, re); }
inline long count(const std::regex& re, const std::string& sh) {
    return static_cast<long>(std::distance(std::sregex_iterator(sh.begin(), sh.end(), re), std::sregex_iterator()));
}
// (?<![A-Za-z])\d
inline bool digit_after_nonletter(const std::string& sh) {
    for (std::size_t k = 0; k < sh.size(); ++k) {
        const unsigned char c = static_cast<unsigned char>(sh[k]);
        if (!(c >= '0' && c <= '9')) continue;
        if (k > 0) {
            const unsigned char p = static_cast<unsigned char>(sh[k - 1]);
            if ((p >= 'a' && p <= 'z') || (p >= 'A' && p <= 'Z')) continue;
        }
        return true;
    }
    return false;
}
inline bool number_re(const Text& t) { return digit_after_nonletter(t.ci) || search(NUMBER_WORD_RE, t, true); }

// ---------------------------------------------------------------- the check
inline std::vector<std::string> sentences(const std::string& text) {
    // re.sub(r"\s+", " ", text).strip(), then split on spaces that follow . ! or ?
    std::string flat; bool sp = false;
    for (const auto& ch : cps(text)) {
        const uint32_t c = cp_of(ch);
        if (py_space(c)) { sp = true; continue; }
        if (sp && !flat.empty()) flat += ' ';
        sp = false;
        flat += ch;
    }
    std::vector<std::string> out; std::string cur;
    for (std::size_t k = 0; k < flat.size(); ++k) {
        if (flat[k] == ' ' && k > 0 && (flat[k - 1] == '.' || flat[k - 1] == '!' || flat[k - 1] == '?')) {
            if (!strip(cur).empty()) out.push_back(strip(cur));
            cur.clear();
            continue;
        }
        cur += flat[k];
    }
    if (!strip(cur).empty()) out.push_back(strip(cur));
    return out;
}

inline std::vector<std::string> i_actions(const std::string& text) {
    const Text t(text);
    const auto v = cps(text);
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(t.cs.begin(), t.cs.end(), I_VERB_RE); it != std::sregex_iterator(); ++it) {
        const std::string verb = lower((*it)[1].str());
        if (NOT_ACTIONS.count(verb)) continue;
        if (IRREGULAR.count(verb) || (verb.size() > 3 && verb.compare(verb.size() - 2, 2, "ed") == 0)) {
            std::string m;
            const auto a = static_cast<std::size_t>(it->position(0));
            for (std::size_t k = a; k < a + static_cast<std::size_t>(it->length(0)); ++k) m += v[k];
            out.push_back(m);
        }
    }
    return out;
}

inline Val strs(const std::vector<std::string>& v) { List o; for (const auto& s : v) o.push_back(Val::str(s)); return Val::list(o); }
inline Val str_or_none(const std::vector<std::string>& v, bool last = false) {
    if (v.empty()) return Val();
    return Val::str(last ? v.back() : v.front());
}

inline Val check(const std::string& text, const std::string& mode = "spoken") {
    const auto sents = sentences(text);
    std::vector<Text> st;
    for (const auto& s : sents) st.emplace_back(s);
    const Text all(text);
    const long n = count(WORD_RE, all.cs);
    Val found = Val::dict();

    const auto is_sit = [&](const Text& s) { return search(SITUATION_RE, s, true) || search(SITUATION_AT_RE, s, false); };
    std::vector<std::string> sit;
    for (std::size_t k = 0; k < st.size() && k < 3; ++k) if (is_sit(st[k])) sit.push_back(sents[k]);
    if (sit.empty()) for (std::size_t k = 0; k < st.size(); ++k) if (is_sit(st[k])) sit.push_back(sents[k]);
    found.d->set("S", str_or_none(sit));
    std::vector<std::string> task;
    for (std::size_t k = 0; k < st.size(); ++k) if (search(TASK_RE, st[k], true)) task.push_back(sents[k]);
    found.d->set("T", str_or_none(task));
    const auto acts = i_actions(text);
    found.d->set("A", acts.size() >= 2 ? strs(acts) : Val());
    std::vector<std::string> res;
    for (std::size_t k = 0; k < st.size(); ++k) if (search(RESULT_RE, st[k], true)) res.push_back(sents[k]);
    if (res.empty() && !sents.empty()) {
        const auto from = static_cast<std::size_t>(std::max(0L, static_cast<long>(static_cast<double>(sents.size()) * 0.6)));
        for (std::size_t k = from; k < st.size(); ++k) if (search(OUTCOME_RE, st[k], true)) res.push_back(sents[k]);
    }
    const Val R = str_or_none(res, true);
    found.d->set("R", R);
    std::vector<std::string> digits, words_n;
    for (std::size_t k = 0; k < st.size(); ++k) {
        if (digit_after_nonletter(st[k].cs)) digits.push_back(sents[k]);
        if (number_re(st[k])) words_n.push_back(sents[k]);
    }
    Val N;
    if (truthy(R) && std::find(digits.begin(), digits.end(), R.s) != digits.end()) N = R;
    else if (!digits.empty()) N = Val::str(digits.front());
    else if (!words_n.empty()) N = Val::str(words_n.front());
    found.d->set("N", N);
    // dict order S, T, A, R, N
    Val f2 = Val::dict();
    for (const char* k : {"S", "T", "A", "R", "N"}) f2.d->set(k, get(found, k));
    found = f2;

    const long i_count = count(I_RE, all.cs), we_count = count(WE_RE, all.ci);
    std::vector<std::string> notes;
    if (!truthy(get(found, "S"))) notes.push_back("Situation: open with where and when (At <company>, last year, our team...).");
    if (!truthy(get(found, "T"))) notes.push_back("Task: say what YOU had to do (My job was..., I needed to...).");
    if (!truthy(get(found, "A"))) notes.push_back("Action: name at least two things you did, as 'I + verb' (I wrote..., I ran...).");
    if (!truthy(R)) notes.push_back("Result: end with what happened (As a result..., which cut...).");
    if (!truthy(N)) notes.push_back("Number: add one measure (how much, how many, how fast). Only a true one.");
    else if (truthy(R) && !number_re(Text(R.s))) notes.push_back("Put the number in the result sentence: accomplished X, as measured by Y.");
    if (we_count > std::max(2L, 2 * i_count))
        notes.push_back("\"we\" " + std::to_string(we_count) + " times, \"I\" " + std::to_string(i_count) + " times: say what YOU did.");
    const double minutes = static_cast<double>(n) / static_cast<double>(WPM);
    if (mode == "spoken") {
        if (n < 150) notes.push_back(std::to_string(n) + " words (about " + fmt_f(minutes, 1) + " minutes spoken): short; aim for 1 to 3 minutes (add the why behind one action).");
        else if (n > 450) notes.push_back(std::to_string(n) + " words (about " + fmt_f(minutes, 1) + " minutes spoken): too long; cut the setup, keep the actions.");
    } else if (n > 300) notes.push_back(std::to_string(n) + " words: long for a written answer; aim under 300.");
    long score = 0;
    for (const auto& [k, v] : found.d->items) if (truthy(v)) score++;
    Val o = Val::dict();
    o.d->set("score", Val::integer(score)); o.d->set("of", Val::integer(5)); o.d->set("found", found);
    o.d->set("words", Val::integer(n)); o.d->set("minutes", Val::flt(round_n(minutes, 1)));
    o.d->set("i", Val::integer(i_count)); o.d->set("we", Val::integer(we_count)); o.d->set("notes", strs(notes));
    return o;
}

const std::map<std::string, std::string> NAMES = {{"S", "Situation"}, {"T", "Task"}, {"A", "Action"}, {"R", "Result"}, {"N", "Number"}};

inline std::string render_check(const Val& res) {
    std::vector<std::string> out = {"STAR " + pystr(get(res, "score")) + "/5   " + pystr(get(res, "words")) + " words, about " +
                                    fmt_f(get(res, "minutes").f, 1) + " minutes spoken"};
    for (const char* k : {"S", "T", "A", "R", "N"}) {
        Val v = get(get(res, "found"), k);
        if (v.t == Val::LIST) {
            std::vector<std::string> p;
            for (std::size_t i = 0; i < v.l->size() && i < 3; ++i) p.push_back(pystr((*v.l)[i]));
            v = Val::str(join(p, "; "));
        }
        const bool yes = truthy(v);
        std::string line = std::string("  ") + k + " " + ljust(NAMES.at(k), 9) + " " + (yes ? "yes" : "MISSING");
        if (yes) line += ": " + (cplen(v.s) <= 90 ? v.s : cpslice(v.s, 0, 87) + "...");
        out.push_back(line);
    }
    for (const Val& n : *get(res, "notes").l) out.push_back("  - " + n.s);
    return join(out, "\n");
}

// ---------------------------------------------------------------- the bank
// d[k] like Python: KeyError / TypeError
inline Val item(const Val& o, const std::string& k) {
    if (o.t == Val::NONE) throw PyErr("TypeError", "'NoneType' object is not subscriptable");
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* p = o.d->get(k);
    if (!p) throw PyErr("KeyError", str_repr(k));
    return *p;
}
inline std::string repr_v(const Val& v) { return v.t == Val::STR ? str_repr(v.s) : pystr(v); }
inline bool in_dict(const Val& d, const Val& k) { return d.t == Val::DICT && k.t == Val::STR && d.d->has(k.s); }

inline Val load_bank(const std::string& path = bank_path()) {
    const Val bank = loads(read_text(path));
    const Val srcs = dget(bank, "sources", Val::dict());
    for (const Val& q : iter(dget(bank, "questions", Val::list())))
        if (!in_dict(srcs, dget(q, "source")))
            throw StarError("question " + repr_v(dget(q, "id")) + " cites unknown source " + repr_v(dget(q, "source")));
    return bank;
}

inline std::string company_key(const std::string& name) {
    std::string o;
    for (const auto& ch : cps(name)) {
        const uint32_t c = cp_of(ch);
        if (c == 0x130) { o += 'i'; continue; }      // 'İ'.lower() is 'i' + a combining dot
        if (c == 0x212A) { o += 'k'; continue; }     // the Kelvin sign lower-cases to k
        const std::string l = lower(ch);
        for (char x : l) if (x >= 'a' && x <= 'z') o += x;
    }
    return o.empty() ? "any" : o;
}

inline List pick(const Val& bank, const std::string& company, const std::string& round_, long long count,
                 const std::vector<std::string>& exclude, bool include_any, std::optional<long long> seed) {
    const std::string key = company_key(company);
    List qs;
    for (const Val& q : iter(item(bank, "questions"))) {
        const std::string c = pystr(item(q, "company"));
        if (c == key || (include_any && c == "any")) qs.push_back(q);
    }
    if (qs.empty() && !in_dict(dget(bank, "companies", Val::dict()), Val::str(key)))
        for (const Val& q : iter(item(bank, "questions"))) if (pystr(item(q, "company")) == "any") qs.push_back(q);
    if (!round_.empty()) { List f; for (const Val& q : qs) if (pystr(item(q, "round")) == round_) f.push_back(q); qs = f; }
    { List f; for (const Val& q : qs) if (std::find(exclude.begin(), exclude.end(), pystr(item(q, "id"))) == exclude.end()) f.push_back(q); qs = f; }
    if (seed) {
        PyRandom rng(*seed);
        for (std::size_t i = qs.size(); i-- > 1;) std::swap(qs[i], qs[static_cast<std::size_t>(rng.randbelow(static_cast<long long>(i) + 1))]);
    }
    const long long n = static_cast<long long>(qs.size());
    long long c = count < 0 ? std::max(0LL, n + count) : std::min(count, n);
    return List(qs.begin(), qs.begin() + c);
}

inline std::string label(const Val& bank, const Val& q) {
    const Val src = item(item(bank, "sources"), pystr(item(q, "source")));
    if (pystr(item(q, "basis")) == "official")
        return "official: " + pystr(item(src, "publisher")) + " says " + pystr(item(q, "round")) + " rounds cover this (" + pystr(item(src, "url")) + ")";
    return "community-reported, not confirmed by the company";
}

// ---------------------------------------------------------------- sessions
// int(x) for ASCII text: surrounding whitespace, a sign, digits with single underscores between them
inline std::optional<long long> py_int(const std::string& s0) {
    const std::string s = strip(s0);
    std::size_t k = 0;
    bool neg = false;
    if (k < s.size() && (s[k] == '+' || s[k] == '-')) { neg = s[k] == '-'; k++; }
    if (k >= s.size()) return std::nullopt;
    long long v = 0; bool prev_digit = false;
    for (; k < s.size(); ++k) {
        const char c = s[k];
        if (c >= '0' && c <= '9') { if (v > 100000000000LL) return std::nullopt; v = v * 10 + (c - '0'); prev_digit = true; }
        else if (c == '_' && prev_digit && k + 1 < s.size() && s[k + 1] >= '0' && s[k + 1] <= '9') prev_digit = false;
        else return std::nullopt;
    }
    return neg ? -v : v;
}
inline bool leap(long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
// datetime.date(*[int(x) for x in arg.split("-")]).isoformat(); none = today
inline std::string today_from(const Val& arg) {
    if (!truthy(arg)) return today_iso();
    const std::string a = pystr(arg);
    const auto bad = [&]() { return StarError("--today must be YYYY-MM-DD, got " + str_repr(a)); };
    std::vector<long long> v;
    for (const auto& part : split(a, "-")) { const auto x = py_int(part); if (!x) throw bad(); v.push_back(*x); }
    if (v.size() != 3) throw bad();
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (v[0] < 1 || v[0] > 9999 || v[1] < 1 || v[1] > 12) throw bad();
    const long long days = dim[v[1] - 1] + (v[1] == 2 && leap(v[0]) ? 1 : 0);
    if (v[2] < 1 || v[2] > days) throw bad();
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", v[0], v[1], v[2]);
    return buf;
}

inline Val load_session(const std::string& path, const std::string& company = "", const std::string& today = "") {
    if (!path.empty() && exists(path)) return loads(read_text(path));
    Val s = Val::dict();
    s.d->set("company", company.empty() ? Val() : Val::str(company_key(company)));
    s.d->set("started", today.empty() ? Val() : Val::str(today));
    s.d->set("asked", Val::list());
    s.d->set("attempts", Val::list());
    return s;
}
inline void save_session(const std::string& path, const Val& s) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path + ".tmp", dumps(s, o) + "\n");
    os_replace(path + ".tmp", path);
}

inline Val review(const Val& session, const Val& bank) {
    const List atts = iter(dget(session, "attempts", Val::list()));
    Val out = Val::dict();
    if (atts.empty()) { out.d->set("attempts", Val::integer(0)); return out; }
    std::vector<std::pair<std::string, long long>> missing;   // insertion order, like the dict
    for (const Val& a : atts) {
        const Val f = item(item(a, "check"), "found");
        for (const char* k : {"S", "T", "A", "R", "N"}) {
            if (truthy(dget(f, k))) continue;
            auto it = std::find_if(missing.begin(), missing.end(), [&](const auto& kv) { return kv.first == k; });
            if (it == missing.end()) missing.emplace_back(k, 1); else it->second++;
        }
    }
    std::vector<std::pair<std::string, long long>> best;
    double total = 0;
    for (const Val& a : atts) {
        const std::string q = pystr(item(a, "question_id"));
        const Val sc = item(item(a, "check"), "score");
        const long long s = sc.t == Val::INT ? sc.i : static_cast<long long>(sc.f);
        total += sc.t == Val::INT ? static_cast<double>(sc.i) : sc.f;
        auto it = std::find_if(best.begin(), best.end(), [&](const auto& kv) { return kv.first == q; });
        if (it == best.end()) best.emplace_back(q, std::max(0LL, s)); else it->second = std::max(it->second, s);
    }
    std::stable_sort(missing.begin(), missing.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    List mm;
    for (const auto& [k, n] : missing) mm.push_back(Val::list({Val::str(k), Val::integer(n)}));
    std::vector<std::string> below;
    for (const auto& [q, s] : best) if (s < 4) below.push_back(q);
    std::sort(below.begin(), below.end());
    out.d->set("attempts", Val::integer(static_cast<long long>(atts.size())));
    out.d->set("questions", Val::integer(static_cast<long long>(best.size())));
    out.d->set("average", Val::flt(round_n(total / static_cast<double>(atts.size()), 2)));
    out.d->set("missing_most", Val::list(mm));
    out.d->set("below_4", strs(below));
    const Val company = dget(session, "company");
    if (truthy(bank) && truthy(company)) {
        std::vector<std::string> left;
        for (const Val& q : iter(item(bank, "questions"))) {
            const std::string id = pystr(item(q, "id"));
            const bool done = std::any_of(best.begin(), best.end(), [&](const auto& kv) { return kv.first == id; });
            if (company.t == Val::STR && pystr(item(q, "company")) == company.s && !done) left.push_back(id);
        }
        out.d->set("not_practiced", strs(left));
    }
    return out;
}

// ---------------------------------------------------------------- CLI
inline int run(const Val& a) {
    const std::string cmd = S(a, "cmd");
    const DumpOpts jo = [] { DumpOpts o; o.indent = 2; return o; }();
    if (cmd == "check") {
        const std::string text = truthy(get(a, "file")) ? read_text(S(a, "file")) : S(a, "text");
        if (text.empty()) throw StarError("give --text or --file");
        const Val res = check(text, B(a, "written") ? "written" : "spoken");
        print(B(a, "json") ? dumps(res, jo) : render_check(res));
        return 0;
    }
    const Val bank = load_bank();
    if (cmd == "loop") {
        const std::string key = company_key(S(a, "company"));
        const Val c = dget(item(bank, "companies"), key);
        if (!truthy(c)) {
            print("No sourced loop for " + S(a, "company") + ". Use /hunter-research for the company's own careers pages; general questions: star.py ask --company any");
            return 1;
        }
        print(pystr(item(c, "name")) + " interview loop (from questions.json, checked " + pystr(item(bank, "as_of")) + ")");
        for (const Val& r : iter(item(c, "loop"))) {
            const Val src = item(item(bank, "sources"), pystr(item(r, "source")));
            const std::string tag = pystr(item(r, "basis")) == "official" ? "official, " + pystr(item(src, "url")) : "community-reported";
            const Val note = dget(r, "note");
            print("  " + ljust(pystr(item(r, "round")), 22) + " " + pystr(item(r, "what")) + "\n  " + ljust("", 22) + " [" + tag + "]" +
                  (truthy(note) ? " Note: " + pystr(note) : std::string()));
        }
        print("Rule: " + pystr(item(c, "rules")));
        return 0;
    }
    if (cmd == "ask") {
        const std::string today = today_from(get(a, "today"));
        const bool has_sess = truthy(get(a, "session"));
        Val sess = has_sess ? load_session(S(a, "session"), S(a, "company"), today) : Val();
        std::vector<std::string> done;
        if (has_sess) for (const Val& x : iter(item(sess, "attempts"))) done.push_back(pystr(item(x, "question_id")));
        const Val seed = get(a, "seed");
        const List qs = pick(bank, S(a, "company"), S(a, "round"), get(a, "count").i, done, B(a, "include_any"),
                             seed.t == Val::INT ? std::optional<long long>(seed.i) : std::nullopt);
        if (qs.empty()) {
            print("No questions left for " + S(a, "company") + (truthy(get(a, "round")) ? " / " + S(a, "round") : std::string()) + ". star.py review shows how it went.");
            return 1;
        }
        if (B(a, "json")) {
            List o;
            for (const Val& q : qs) {
                Val d = Val::dict();
                for (const auto& [k, v] : q.d->items) d.d->set(k, v);
                d.d->set("label", Val::str(label(bank, q)));
                o.push_back(d);
            }
            print(dumps(Val::list(o), jo));
        } else {
            int i = 0;
            for (const Val& q : qs)
                print(std::to_string(++i) + ". [" + pystr(item(q, "id")) + "] " + pystr(item(q, "question")) + "\n   (" + pystr(item(q, "round")) + ", " +
                      pystr(item(q, "tests")) + "; " + label(bank, q) + ")");
        }
        if (has_sess) {
            Val asked = item(sess, "asked");
            if (asked.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(asked) + "' object has no attribute 'append'");
            for (const Val& q : qs) {
                const std::string id = pystr(item(q, "id"));
                bool seen = false;
                for (const Val& x : *asked.l) if (x.t == Val::STR && x.s == id) seen = true;
                if (!seen) asked.l->push_back(Val::str(id));
            }
            save_session(S(a, "session"), sess);
        }
        return 0;
    }
    if (cmd == "log") {
        const std::string today = today_from(get(a, "today"));
        const std::string text = truthy(get(a, "answer_file")) ? read_text(S(a, "answer_file")) : S(a, "text");
        if (text.empty()) throw StarError("give --text or --answer-file");
        Val q;
        for (const Val& x : iter(item(bank, "questions"))) if (pystr(item(x, "id")) == S(a, "question")) { q = x; break; }
        if (!truthy(q)) throw StarError("no question " + str_repr(S(a, "question")) + " in questions.json");
        const Val qc = item(q, "company");
        Val sess = load_session(S(a, "session"), truthy(qc) ? pystr(qc) : "", today);
        const Val res = check(text);
        if (!has(sess, "attempts")) sess.d->set("attempts", Val::list());
        Val atts = item(sess, "attempts");
        Val at = Val::dict();
        at.d->set("date", Val::str(today)); at.d->set("question_id", item(q, "id")); at.d->set("question", item(q, "question"));
        at.d->set("answer", Val::str(strip(text))); at.d->set("check", res);
        atts.l->push_back(at);
        save_session(S(a, "session"), sess);
        print(render_check(res));
        print("Logged attempt " + std::to_string(atts.l->size()) + " in " + S(a, "session"));
        return 0;
    }
    if (cmd == "review") {
        const Val sess = load_session(S(a, "session"));
        const Val r = review(sess, bank);
        if (B(a, "json")) print(dumps(r, jo));
        else if (get(r, "attempts").i == 0) print("No answers logged yet.");
        else {
            print(pystr(get(r, "attempts")) + " answers to " + pystr(get(r, "questions")) + " questions, average STAR " + fmt_f(get(r, "average").f, 2) + " / 5");
            const List mm = *get(r, "missing_most").l;
            if (!mm.empty()) {
                std::vector<std::string> p;
                for (const Val& kv : mm) p.push_back(NAMES.at((*kv.l)[0].s) + " (" + pystr((*kv.l)[1]) + ")");
                print("Most often missing: " + join(p, ", "));
            }
            std::vector<std::string> b;
            for (const Val& x : *get(r, "below_4").l) b.push_back(x.s);
            if (!b.empty()) print("Redo (best under 4/5): " + join(b, ", "));
            const Val np = get(r, "not_practiced");
            if (truthy(np)) print("Not practiced yet: " + std::to_string(np.l->size()) + " questions (star.py ask --session ...)");
        }
        return 0;
    }
    return 2;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("star", "Mock interviews from a sourced question bank, with a STAR check.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("loop"); s.help_line = "the company's interview rounds, with sources"; s.opt({"--company"}).required(); }
    { Parser& s = p.sub("ask"); s.help_line = "pick practice questions";
      s.opt({"--company"}).required(); s.opt({"--round"}); s.opt({"--count"}).integer().def(Val::integer(3));
      s.opt({"--include-any"}).store_true().help("also draw from the general questions");
      s.opt({"--seed"}).integer().help("shuffle with this seed (same seed, same questions)");
      s.opt({"--session"}).help("session file: skips questions already answered and records what was asked");
      s.opt({"--today"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("check"); s.help_line = "STAR check one answer";
      s.opt({"--text"}); s.opt({"--file"}); s.opt({"--written"}).store_true().help("a written answer (application form), not spoken");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("log"); s.help_line = "check an answer and add it to the session log";
      s.opt({"--session"}).required(); s.opt({"--question"}).required().help("question id from questions.json");
      s.opt({"--text"}); s.opt({"--answer-file"}); s.opt({"--today"}); }
    { Parser& s = p.sub("review"); s.help_line = "what the session shows: weak spots and what is left";
      s.opt({"--session"}).required(); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    if (S(a, "cmd").empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    try {
        return run(a);
    } catch (const StarError& e) {
        eprint(std::string("star: ") + e.what()); return 2;
    } catch (const PyErr& e) {   // OSError and ValueError (with JSONDecodeError and UnicodeDecodeError)
        if (e.type != "OSError" && e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError" && e.type != "JSONDecodeError" && e.type != "UnicodeDecodeError") throw;
        eprint(std::string("star: ") + e.what()); return 2;
    }
}
PACKTOOL("hunter-interview/star", main);
}  // namespace packtools::hunter_interview
