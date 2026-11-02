// hunter-linkedin/profile: LinkedIn profile checker (field limits + weighted job-keyword coverage), a C++ port of
// hunter-skill/skills/hunter-linkedin/profile.py (MIT, Jake Schincariol). Same subcommands, output, JSON and exit codes.
// It never touches LinkedIn: the user pastes the text into their own profile.
//
// Regex notes. The vocabulary patterns are matched by hand: Python compiles each as
//   (?<![a-z0-9])PAT(?![a-z0-9+#])  with re.I   (short capitalised patterns like "Go": case-sensitive, [A-Za-z0-9])
// Under re.I Python also folds U+0130/U+0131 to i, U+017F to s and U+212A to k, so fold() maps those four to ASCII
// before the byte search; every other non-ASCII byte is >= 0x80 and so never alnum, the same as the lookarounds.
// DEFAULT_HEADLINE (^[^|·,]{2,60}\s+at\s+[^|·,]{2,60}$, re.I) counts code points, so it is matched on code points too.
namespace packtools::hunter_linkedin {
using namespace pyrt;

const std::map<std::string, int> WEIGHTS = {{"must", 3}, {"body", 2}, {"nice", 1}};

const std::vector<std::pair<std::string, std::vector<std::string>>> VOCAB = {
    {"Go", {"Go", "golang"}}, {"Python", {"python"}}, {"Java", {"java"}}, {"C++", {"c++"}},
    {"Rust", {"rust"}}, {"JavaScript", {"javascript"}}, {"TypeScript", {"typescript"}},
    {"Kotlin", {"kotlin"}}, {"Swift", {"swift"}}, {"Scala", {"scala"}}, {"Ruby", {"ruby"}},
    {"SQL", {"sql"}},
    {"Distributed systems", {"distributed systems", "distributed system"}},
    {"System design", {"system design", "systems design"}},
    {"Data structures", {"data structures"}}, {"Algorithms", {"algorithms"}},
    {"APIs", {"apis", "api", "rest", "restful"}}, {"gRPC", {"grpc"}}, {"GraphQL", {"graphql"}},
    {"Microservices", {"microservices"}}, {"Backend", {"backend", "back-end"}},
    {"Frontend", {"frontend", "front-end"}}, {"Full-stack", {"full-stack", "full stack"}},
    {"Kubernetes", {"kubernetes", "k8s"}}, {"Docker", {"docker"}},
    {"Containers", {"containers", "containerization"}}, {"Terraform", {"terraform"}},
    {"Google Cloud", {"google cloud", "gcp"}}, {"AWS", {"aws", "amazon web services"}},
    {"Azure", {"azure"}}, {"Kafka", {"kafka"}}, {"PostgreSQL", {"postgresql", "postgres"}},
    {"MySQL", {"mysql"}}, {"Redis", {"redis"}}, {"Spark", {"spark"}}, {"BigQuery", {"bigquery"}},
    {"Machine learning", {"machine learning", "ML"}},
    {"ML infrastructure", {"ml infrastructure", "machine learning infrastructure", "ml infra"}},
    {"Deep learning", {"deep learning"}}, {"LLMs", {"llm", "llms", "large language models"}},
    {"CUDA", {"cuda"}}, {"PyTorch", {"pytorch"}}, {"TensorFlow", {"tensorflow"}},
    {"Observability", {"observability", "monitoring"}}, {"SRE", {"sre", "site reliability"}},
    {"Reliability", {"reliability", "reliable"}}, {"Scalability", {"scalability", "large-scale", "at scale"}},
    {"Performance", {"performance", "latency"}}, {"On-call", {"on-call", "on call"}},
    {"Design reviews", {"design reviews", "design review"}}, {"Code review", {"code review", "code reviews"}},
    {"CI/CD", {"ci/cd", "continuous integration"}}, {"Testing", {"testing", "unit tests"}},
    {"Linux", {"linux"}}, {"Networking", {"networking"}}, {"Security", {"security"}},
    {"Infrastructure", {"infrastructure"}}, {"Mentoring", {"mentoring", "mentorship"}},
    {"Technical leadership", {"technical leadership", "tech lead"}},
};

struct ProfileError : std::runtime_error { using std::runtime_error::runtime_error; };

inline std::string limits_default() { return skill_file("limits.json"); }

// d[k]: KeyError / TypeError like Python
inline Val item(const Val& o, const std::string& k) {
    if (o.t == Val::NONE) throw PyErr("TypeError", "'NoneType' object is not subscriptable");
    if (o.t != Val::DICT) throw PyErr("TypeError", "string indices must be integers");
    const Val* p = o.d->get(k);
    if (!p) throw PyErr("KeyError", str_repr(k));
    return *p;
}
inline Val load_json(const std::string& path) {
    if (!path.empty() && is_file(path)) return loads(read_text(path));
    return Val();
}
inline std::string read_text_(const Val& path) {
    if (!truthy(path) || !is_file(pystr(path))) throw ProfileError("no file " + pystr(path));
    return read_text(pystr(path));
}
// len() of a str (code points) or list
inline long long vlen(const Val& v) {
    if (v.t == Val::STR) return static_cast<long long>(cplen(v.s));
    if (v.t == Val::LIST || v.t == Val::DICT) return static_cast<long long>(v.size());
    throw PyErr("TypeError", std::string("object of type '") + type_name(v) + "' has no len()");
}
inline std::string as_text(const Val& v) {   // the value handed to re.search (a str in every real profile)
    if (v.t == Val::STR) return v.s;
    if (v.t == Val::NONE) return "";
    throw PyErr("TypeError", "expected string or bytes-like object, got '" + std::string(type_name(v)) + "'");
}
inline std::string join_strs(const List& items, const std::string& sep) {   // sep.join(items)
    std::vector<std::string> v;
    for (std::size_t k = 0; k < items.size(); ++k) {
        if (items[k].t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(k) + ": expected str instance, " + type_name(items[k]) + " found");
        v.push_back(items[k].s);
    }
    return join(v, sep);
}
inline bool has_digit(const std::string& s) {   // re.search(r"\d", s): Unicode decimal digits
    for (const auto& c : cps(s)) if (std::string(uni::category(cp_of(c))) == "Nd") return true;
    return false;
}
inline bool ascii_alnum(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// ASCII lower-case, plus the four non-ASCII letters Python's re.I folds onto ASCII ones
inline std::string fold(const std::string& s) {
    std::string o;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c >= 'A' && c <= 'Z') o += static_cast<char>(c + 32);
        else if (c == 0x130 || c == 0x131) o += 'i';
        else if (c == 0x17F) o += 's';
        else if (c == 0x212A) o += 'k';
        else o += ch;
    }
    return o;
}

struct Pat { std::string p; bool cs; };
inline const std::vector<std::pair<std::string, std::vector<Pat>>>& compiled() {
    static const auto c = [] {
        std::vector<std::pair<std::string, std::vector<Pat>>> o;
        for (const auto& [name, pats] : VOCAB) {
            std::vector<Pat> v;
            for (const auto& p : pats) {
                const bool cs = cplen(p) <= 3 && !p.empty() && p[0] >= 'A' && p[0] <= 'Z';
                v.push_back({cs ? p : fold(p), cs});
            }
            o.emplace_back(name, v);
        }
        return o;
    }();
    return c;
}
inline bool pat_in(const std::string& raw, const std::string& folded, const Pat& pt) {
    const std::string& t = pt.cs ? raw : folded;
    for (std::size_t k = t.find(pt.p); k != std::string::npos; k = t.find(pt.p, k + 1)) {
        if (k > 0 && ascii_alnum(static_cast<unsigned char>(t[k - 1]))) continue;
        const std::size_t e = k + pt.p.size();
        if (e < t.size()) {
            const unsigned char n = static_cast<unsigned char>(t[e]);
            if (ascii_alnum(n) || n == '+' || n == '#') continue;
        }
        return true;
    }
    return false;
}
// vocabulary terms that appear in text
inline std::vector<std::string> terms_in(const std::string& text) {
    const std::string f = fold(text);
    std::vector<std::string> o;
    for (const auto& [name, pats] : compiled())
        for (const auto& pt : pats) if (pat_in(text, f, pt)) { o.push_back(name); break; }
    return o;
}
inline bool contains(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
inline std::size_t vocab_index(const std::string& t) {
    for (std::size_t k = 0; k < VOCAB.size(); ++k) if (VOCAB[k].first == t) return k;
    return VOCAB.size();
}
inline bool any_of(const std::string& folded, std::initializer_list<const char*> words) {
    for (const char* w : words) if (folded.find(w) != std::string::npos) return true;
    return false;
}
// str.strip(chars) for a set of code points
inline std::string strip_set(const std::string& s, const std::vector<uint32_t>& set) {
    const auto v = cps(s);
    std::size_t a = 0, b = v.size();
    const auto in = [&](const std::string& c) { return std::find(set.begin(), set.end(), cp_of(c)) != set.end(); };
    while (a < b && in(v[a])) a++;
    while (b > a && in(v[b - 1])) b--;
    std::string o;
    for (std::size_t k = a; k < b; ++k) o += v[k];
    return o;
}
// str.istitle(): upper/titlecase only after uncased, lowercase only after cased, at least one cased
inline bool istitle(const std::string& s) {
    bool cased = false, prev = false;
    for (const auto& ch : cps(s)) {
        const std::string cat = uni::category(cp_of(ch));
        if (cat == "Lu" || cat == "Lt") {
            if (prev) return false;
            prev = true;
            cased = true;
        } else if (cat == "Ll") {
            if (!prev) return false;
            prev = true;
            cased = true;
        } else prev = false;
    }
    return cased;
}

// split a job description into (zone, line) with zone in must, body, nice
inline std::vector<std::pair<std::string, std::string>> zones(const std::string& text) {
    std::string zone = "body";
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& line : splitlines(text)) {
        const std::string ls = strip(line);
        const std::string stripped = strip(strip_set(ls, {'-', '*', 0x2022, ' '}));
        const bool bullet = !ls.empty() && (ls[0] == '-' || ls[0] == '*' || ls.rfind("\xE2\x80\xA2", 0) == 0);
        const bool is_heading = !stripped.empty() && cplen(stripped) < 60 && !bullet &&
            (stripped.back() == ':' || istitle(stripped) || split_ws(stripped).size() <= 4);
        if (is_heading) {
            const std::string f = fold(stripped);
            if (any_of(f, {"preferred", "bonus", "nice to have", "nice-to-have", "pluses", "a plus"})) { zone = "nice"; continue; }
            if (any_of(f, {"minimum", "basic", "required", "requirements", "must have", "you should have", "what you'll need",
                           "what you\xE2\x80\x99ll need", "qualifications"})) { zone = "must"; continue; }
            if (any_of(f, {"responsibilit", "about", "what you'll do", "what you\xE2\x80\x99ll do", "the role", "the job"})) { zone = "body"; continue; }
        }
        out.emplace_back(zone, line);
    }
    return out;
}

// [{term, weight, zone, count}] from a job description, heaviest first
inline List keywords(const std::string& jd) {
    List found;
    const auto find = [&](const std::string& name) -> Val* {
        for (auto& k : found) if (get(k, "term").s == name) return &k;
        return nullptr;
    };
    for (const auto& [zone, line] : zones(jd)) {
        for (const auto& name : terms_in(line)) {
            Val* k = find(name);
            if (!k) {
                Val n = Val::dict();
                n.d->set("term", Val::str(name)); n.d->set("weight", Val::integer(0));
                n.d->set("zone", Val::str(zone)); n.d->set("count", Val::integer(0));
                found.push_back(n);
                k = &found.back();
            }
            k->d->set("count", Val::integer(get(*k, "count").i + 1));
            if (WEIGHTS.at(zone) > get(*k, "weight").i) {
                k->d->set("weight", Val::integer(WEIGHTS.at(zone)));
                k->d->set("zone", Val::str(zone));
            }
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Val& a, const Val& b) {
        const long long wa = get(a, "weight").i, wb = get(b, "weight").i, ca = get(a, "count").i, cb = get(b, "count").i;
        if (wa != wb) return wa > wb;
        if (ca != cb) return ca > cb;
        return vocab_index(get(a, "term").s) < vocab_index(get(b, "term").s);
    });
    return found;
}

// profile.get(k) or ""
inline Val or_empty(const Val& profile, const std::string& k) { return orv(dget(profile, k), Val::str("")); }
inline List or_list(const Val& profile, const std::string& k) { return iter(orv(dget(profile, k), Val::list())); }

inline std::vector<std::pair<std::string, std::string>> fields(const Val& profile) {
    std::vector<std::string> ex;
    for (const Val& e : or_list(profile, "experience"))
        ex.push_back(pystr(dget(e, "title", Val::str(""))) + "\n" + pystr(dget(e, "description", Val::str(""))));
    return {{"headline", as_text(or_empty(profile, "headline"))}, {"about", as_text(or_empty(profile, "about"))},
            {"skills", join_strs(or_list(profile, "skills"), "\n")}, {"experience", join(ex, "\n")}};
}
inline Val strs(const std::vector<std::string>& v) { List o; for (const auto& s : v) o.push_back(Val::str(s)); return Val::list(o); }

inline Val coverage(const Val& profile, const List& kws) {
    const auto f = fields(profile);
    std::vector<std::vector<std::string>> found;   // the terms of each field, once
    for (const auto& [name, text] : f) found.push_back(terms_in(text));
    List per;
    long long total = 0, got = 0;
    for (const Val& k : kws) {
        std::vector<std::string> where;
        for (std::size_t i = 0; i < f.size(); ++i) if (contains(found[i], get(k, "term").s)) where.push_back(f[i].first);
        Val d = Val::dict();
        for (const auto& [a, v] : k.d->items) d.d->set(a, v);
        d.d->set("where", strs(where));
        per.push_back(d);
        total += get(k, "weight").i;
        if (!where.empty()) got += get(k, "weight").i;
    }
    if (total == 0) total = 1;
    long long must_covered = 0, must_total = 0;
    std::vector<std::string> missing_must, missing_other, not_in_skills;
    for (const Val& k : per) {
        const std::string zone = get(k, "zone").s, term = get(k, "term").s;
        const List where = *get(k, "where").l;
        const bool on = !where.empty();
        bool in_skills = false;
        for (const Val& w : where) if (w.s == "skills") in_skills = true;
        if (zone == "must") {
            must_total++;
            if (on) must_covered++;
            else missing_must.push_back(term);
        } else if (!on) missing_other.push_back(term);
        if (zone != "nice" && on && !in_skills) not_in_skills.push_back(term);
    }
    Val o = Val::dict();
    o.d->set("score", Val::integer(static_cast<long long>(std::nearbyint(100.0 * static_cast<double>(got) / static_cast<double>(total)))));
    o.d->set("must_covered", Val::integer(must_covered));
    o.d->set("must_total", Val::integer(must_total));
    o.d->set("missing_must", strs(missing_must));
    o.d->set("missing_other", strs(missing_other));
    o.d->set("not_in_skills", strs(not_in_skills));
    o.d->set("terms", Val::list(per));
    return o;
}

inline double num_of(const Val& v) {
    if (v.t == Val::INT) return static_cast<double>(v.i);
    if (v.t == Val::FLOAT) return v.f;
    if (v.t == Val::BOOL) return v.b ? 1.0 : 0.0;
    throw PyErr("TypeError", std::string("'<=' not supported between instances of 'int' and '") + type_name(v) + "'");
}
inline List limit_checks(const Val& profile, const Val& limits) {
    const Val L = item(limits, "fields");
    List out;
    const auto add = [&](const std::string& field, long long n) {
        const Val lim = item(L, field);
        const Val mx = item(lim, "max");
        const bool ok = static_cast<double>(n) <= num_of(mx);
        const Val src = orv(dget(lim, "source"), Val::str("reported limit (the editor's counter is the authority)"));
        Val c = Val::dict();
        c.d->set("field", Val::str(field)); c.d->set("value", Val::integer(n)); c.d->set("max", mx);
        c.d->set("unit", item(lim, "unit")); c.d->set("ok", Val::boolean(ok)); c.d->set("source", src);
        out.push_back(c);
    };
    add("headline", vlen(or_empty(profile, "headline")));
    add("about", vlen(or_empty(profile, "about")));
    add("skills", vlen(orv(dget(profile, "skills"), Val::list())));
    for (const Val& e : or_list(profile, "experience")) {
        add("position_title", vlen(orv(dget(e, "title"), Val::str(""))));
        add("position_description", vlen(orv(dget(e, "description"), Val::str(""))));
    }
    return out;
}

// ^[^|·,]{2,60}\s+at\s+[^|·,]{2,60}$ with re.I, on code points
inline bool default_headline(const std::string& head) {
    std::vector<uint32_t> h;
    for (const auto& c : cps(head)) h.push_back(cp_of(c));
    for (uint32_t c : h) if (c == '|' || c == 0xB7 || c == ',') return false;
    const long n = static_cast<long>(h.size());
    const auto sp = [&](long i) { return py_space(h[static_cast<std::size_t>(i)]); };
    for (long p = 1; p + 2 < n; ++p) {
        const uint32_t a = h[static_cast<std::size_t>(p)], t = h[static_cast<std::size_t>(p + 1)];
        if (!((a == 'a' || a == 'A') && (t == 't' || t == 'T'))) continue;
        if (!sp(p - 1) || !sp(p + 2)) continue;
        long s0 = p - 1;   // the whitespace before "at" can start anywhere in its run; X = h[0:s], 2..60 long
        while (s0 > 0 && sp(s0 - 1)) s0--;
        bool left = false;
        for (long s = s0; s <= p - 1; ++s) if (s >= 2 && s <= 60) left = true;
        if (!left) continue;
        long e1 = p + 3;   // Y = h[t:], t from p+3 to the end of the whitespace run after "at"
        while (e1 < n && sp(e1)) e1++;
        for (long tt = p + 3; tt <= e1; ++tt) if (n - tt >= 2 && n - tt <= 60) return true;
    }
    return false;
}

// plain-English warnings about the draft itself
inline std::vector<std::string> advice(const Val& profile, const Val& cov, const List& kws) {
    std::vector<std::string> tips;
    const std::string head = strip(as_text(or_empty(profile, "headline")));
    if (head.empty()) tips.push_back("Headline is empty.");
    else if (default_headline(head))
        tips.push_back("Headline reads like the default \"title at company\": add what you build and your strongest skills from the posting.");
    std::vector<std::string> top;
    for (const Val& k : kws) if (get(k, "zone").s == "must" && top.size() < 3) top.push_back(get(k, "term").s);
    const auto ht = terms_in(head);
    bool in_head = false;
    for (const auto& t : top) if (contains(ht, t)) in_head = true;
    if (!top.empty() && !in_head) tips.push_back("None of the top required terms (" + join(top, ", ") + ") is in the headline.");
    const std::string about = strip(as_text(or_empty(profile, "about")));
    if (about.empty()) tips.push_back("About is empty: write a short story of what you build, with one or two real results.");
    else if (cplen(about) < 600) tips.push_back("About is short (" + std::to_string(cplen(about)) + " characters): there is room for one more real result.");
    else if (!has_digit(about)) tips.push_back("About has no numbers: add one result you can prove.");
    const List skills = or_list(profile, "skills");
    if (skills.size() < 5) tips.push_back("Only " + std::to_string(skills.size()) + " skills listed.");
    std::vector<std::string> low;
    for (const Val& s : skills) {
        if (s.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(s) + "' object has no attribute 'lower'");
        low.push_back(lower(s.s));
    }
    std::vector<std::string> dups;
    for (const auto& s : low) if (std::count(low.begin(), low.end(), s) > 1 && !contains(dups, s)) dups.push_back(s);
    std::sort(dups.begin(), dups.end());
    if (!dups.empty()) tips.push_back("Duplicate skills: " + join(dups, ", ") + ".");
    for (const Val& e : or_list(profile, "experience")) {
        const Val d = dget(e, "description");
        if (truthy(d) && !has_digit(as_text(d)))
            tips.push_back(pystr(dget(e, "title")) + " at " + pystr(dget(e, "company")) + " has no numbers: add a result you can prove.");
    }
    std::vector<std::string> nis;
    for (const Val& t : *get(cov, "not_in_skills").l) if (nis.size() < 8) nis.push_back(t.s);
    if (!nis.empty()) tips.push_back("On the profile but not in Skills (add them if true): " + join(nis, ", ") + ".");
    std::vector<std::string> mm;
    for (const Val& t : *get(cov, "missing_must").l) mm.push_back(t.s);
    if (!mm.empty()) tips.push_back("Required by the posting and missing everywhere: " + join(mm, ", ") + ". Add only what the user really has.");
    return tips;
}

inline std::string all_fields(const Val& profile) {
    std::vector<std::string> v;
    for (const auto& [k, t] : fields(profile)) v.push_back(t);
    return join(v, "\n");
}
// terms the draft claims that no truth source (resume, profile skills, current profile) mentions
inline std::vector<std::string> unproven(const Val& draft, const std::string& truth, const Val& current) {
    const auto claimed = terms_in(all_fields(draft));
    auto known = terms_in(truth);
    if (truthy(current)) for (const auto& t : terms_in(all_fields(current))) if (!contains(known, t)) known.push_back(t);
    std::vector<std::string> o;
    for (const auto& t : claimed) if (!contains(known, t)) o.push_back(t);
    std::sort(o.begin(), o.end(), [](const std::string& a, const std::string& b) { return vocab_index(a) < vocab_index(b); });
    return o;
}

inline std::string truth_text(const std::string& profile_path, const std::string& resume_path) {
    List parts;
    const Val prof = orv(load_json(profile_path), Val::dict());
    for (const Val& s : iter(orv(dget(prof, "skills"), Val::list()))) parts.push_back(s);
    const Val cur = dget(prof, "current");
    if (cur.t == Val::DICT) parts.push_back(dget(cur, "title", Val::str("")));
    if (!resume_path.empty() && is_file(resume_path)) parts.push_back(Val::str(read_text_(Val::str(resume_path))));
    List keep;
    for (const Val& p : parts) if (truthy(p)) keep.push_back(p);
    return join_strs(keep, "\n");
}

inline Val report(const Val& profile, const std::string& jd, const Val& limits, const std::optional<std::string>& truth = std::nullopt, const Val& current = Val()) {
    const List kws = keywords(jd);
    const Val cov = coverage(profile, kws);
    const List checks = limit_checks(profile, limits);
    Val o = Val::dict();
    o.d->set("coverage", get(cov, "score"));
    o.d->set("must_covered", get(cov, "must_covered"));
    o.d->set("must_total", get(cov, "must_total"));
    o.d->set("missing_must", get(cov, "missing_must"));
    o.d->set("missing_other", get(cov, "missing_other"));
    o.d->set("limits", Val::list(checks));
    o.d->set("advice", strs(advice(profile, cov, kws)));
    Val len = Val::dict();
    len.d->set("headline", Val::integer(vlen(or_empty(profile, "headline"))));
    len.d->set("about", Val::integer(vlen(or_empty(profile, "about"))));
    len.d->set("skills", Val::integer(vlen(orv(dget(profile, "skills"), Val::list()))));
    o.d->set("lengths", len);
    o.d->set("terms", get(cov, "terms"));
    if (truth) o.d->set("confirm", strs(unproven(profile, *truth, current)));
    return o;
}

inline std::string comma_v(const Val& v) {   // "{:,}".format(v)
    if (v.t == Val::INT && !v.big()) return commas(std::to_string(v.i));
    if (v.t == Val::FLOAT) return commas(float_repr(v.f));
    return pystr(v);
}
inline std::string rjust_s(const std::string& s, std::size_t w) { return rjust(s, w); }

inline std::vector<std::string> render_check(const std::string& name, const Val& rep) {
    std::vector<std::string> lines = {name + " profile vs the job description: keyword coverage " + pystr(get(rep, "coverage")) +
        "%, required terms " + pystr(get(rep, "must_covered")) + "/" + pystr(get(rep, "must_total")) + "."};
    for (const Val& c : *get(rep, "limits").l)
        lines.push_back("  " + ljust(truthy(get(c, "ok")) ? "ok" : "OVER", 4) + " " + ljust(get(c, "field").s, 21) + " " +
                        rjust_s(comma_v(get(c, "value")), 5) + " / " + comma_v(get(c, "max")) + " " + pystr(get(c, "unit")) +
                        "  (" + pystr(get(c, "source")) + ")");
    for (const Val& t : *get(rep, "advice").l) lines.push_back("  - " + t.s);
    const Val conf = get(rep, "confirm");
    if (truthy(conf)) {
        std::vector<std::string> v;
        for (const Val& t : *conf.l) v.push_back(t.s);
        lines.push_back("  - Confirm these are true before publishing (not in the resume, profile skills or current LinkedIn): " + join(v, ", ") + ".");
    }
    return lines;
}

inline std::vector<std::string> render_compare(const Val& before, const Val& after) {
    const Val bl = get(before, "lengths"), al = get(after, "lengths");
    const auto ratio = [](const Val& r) { return pystr(get(r, "must_covered")) + "/" + pystr(get(r, "must_total")); };
    const std::vector<std::array<std::string, 3>> rows = {
        {"Headline characters", comma_v(get(bl, "headline")), comma_v(get(al, "headline"))},
        {"About characters", comma_v(get(bl, "about")), comma_v(get(al, "about"))},
        {"Skills", comma_v(get(bl, "skills")), comma_v(get(al, "skills"))},
        {"Keyword coverage", pystr(get(before, "coverage")) + "%", pystr(get(after, "coverage")) + "%"},
        {"Required terms", ratio(before), ratio(after)}};
    std::vector<std::string> lines = {ljust("", 22) + " " + rjust_s("before", 10) + " " + rjust_s("after", 10)};
    for (const auto& r : rows) lines.push_back(ljust(r[0], 22) + " " + rjust_s(r[1], 10) + " " + rjust_s(r[2], 10));
    std::vector<std::string> had, has_, order;
    for (const Val& k : *get(before, "terms").l) if (truthy(get(k, "where"))) had.push_back(get(k, "term").s);
    for (const Val& k : *get(after, "terms").l) {
        if (truthy(get(k, "where"))) has_.push_back(get(k, "term").s);
        order.push_back(get(k, "term").s);
    }
    std::vector<std::string> gained, lost;
    for (const auto& t : order) {
        if (contains(has_, t) && !contains(had, t)) gained.push_back(t);
        if (contains(had, t) && !contains(has_, t)) lost.push_back(t);
    }
    lines.push_back("Gained: " + (gained.empty() ? std::string("nothing") : join(gained, ", ")));
    if (!lost.empty()) lines.push_back("Lost: " + join(lost, ", "));
    std::vector<std::string> miss;
    for (const Val& t : *get(after, "missing_must").l) miss.push_back(t.s);
    for (const Val& t : *get(after, "missing_other").l) miss.push_back(t.s);
    lines.push_back("Still missing: " + (miss.empty() ? std::string("nothing") : join(miss, ", ")));
    return lines;
}

inline bool all_ok(const Val& rep) {
    for (const Val& c : *get(rep, "limits").l) if (!truthy(get(c, "ok"))) return false;
    return true;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("profile", "LinkedIn profile check: field limits and job keyword coverage.");
    p.subcommands("cmd", false);
    { Parser& k = p.sub("keywords"); k.help_line = "keywords of a job description, weighted";
      k.opt({"--job"}).required(); k.opt({"--top"}).integer().def(Val::integer(30)); k.opt({"--json"}).store_true(); }
    for (const std::string name : {"check", "compare"}) {
        Parser& s = p.sub(name); s.help_line = name == "check" ? "check the draft" : "before/after report";
        s.opt({"--linkedin"}).def(Val::str("hunter/linkedin.json"));
        s.opt({"--job"}).required().help("the job description as a text file");
        s.opt({"--profile"}).def(Val::str("hunter/profile.json")).help("truth source: your skills");
        s.opt({"--resume"}).help("truth source: your resume as text or markdown");
        s.opt({"--limits"}).def(Val::str(limits_default()));
        s.opt({"--json"}).store_true();
        if (name == "check") s.opt({"--which"}).def(Val::str("draft")).choices({"draft", "current"});
    }
    { Parser& l = p.sub("limits"); l.help_line = "LinkedIn field limits and where each comes from";
      l.opt({"--limits"}).def(Val::str(limits_default())); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }

    if (cmd == "limits") {
        const Val doc = load_json(S(a, "limits"));
        const Val fs = item(doc, "fields");
        if (fs.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(fs) + "' object has no attribute 'items'");
        for (const auto& [name, f] : fs.d->items)
            print(ljust(name, 21) + " " + rjust_s(comma_v(item(f, "max")), 6) + " " + ljust(pystr(item(f, "unit")), 10) + " " +
                  pystr(orv(dget(f, "source"), Val::str("reported: the editor's counter is the authority"))));
        const Val au = item(doc, "automation");
        print("Automation: " + pystr(item(au, "rule")) + " (" + pystr(item(au, "source")) + ")");
        return 0;
    }

    std::string jd;
    try {
        jd = read_text_(get(a, "job"));
    } catch (const ProfileError& e) {
        eprint(std::string("error: ") + e.what());
        return 2;
    }
    const DumpOpts jo = [] { DumpOpts o; o.indent = 2; return o; }();
    if (cmd == "keywords") {
        const List all = keywords(jd);
        const long long n = static_cast<long long>(all.size());
        long long top = get(a, "top").i;
        if (top < 0) top = std::max(0LL, n + top);
        top = std::min(top, n);
        const List kws(all.begin(), all.begin() + top);
        if (B(a, "json")) print(dumps(Val::list(kws), jo));
        else for (const Val& x : kws)
            print(ljust(get(x, "term").s, 22) + " weight " + pystr(get(x, "weight")) + "  (" + get(x, "zone").s + ", " + pystr(get(x, "count")) + "x)");
        return kws.empty() ? 1 : 0;
    }

    const Val doc = load_json(S(a, "linkedin"));
    if (!truthy(doc)) {
        eprint("error: no " + S(a, "linkedin") + ". Copy skills/hunter-linkedin/linkedin.example.json there and paste the user's real profile into \"current\".");
        return 2;
    }
    const Val limits = load_json(S(a, "limits"));
    const std::string truth = truth_text(S(a, "profile"), S(a, "resume"));
    const Val current = orv(dget(doc, "current"), Val::dict());
    if (cmd == "check") {
        const std::string which = S(a, "which");
        const Val prof = orv(dget(doc, which), Val::dict());
        const Val rep = report(prof, jd, limits, truth, which == "draft" ? current : Val());
        if (B(a, "json")) print(dumps(rep, jo));
        else {
            std::string cap = which;
            cap[0] = static_cast<char>(cap[0] - 32);
            print(join(render_check(cap, rep), "\n"));
        }
        return all_ok(rep) ? 0 : 1;
    }
    const Val before = report(current, jd, limits);
    const Val after = report(orv(dget(doc, "draft"), Val::dict()), jd, limits, truth, current);
    if (B(a, "json")) {
        Val o = Val::dict();
        o.d->set("before", before);
        o.d->set("after", after);
        print(dumps(o, jo));
    } else {
        print(join(render_compare(before, after), "\n"));
        print("");
        print(join(render_check("Draft", after), "\n"));
        print("Paste the draft into LinkedIn yourself. Nothing here logs in to LinkedIn or edits your profile.");
    }
    return all_ok(after) ? 0 : 1;
}
PACKTOOL("hunter-linkedin/profile", main);
}  // namespace packtools::hunter_linkedin
