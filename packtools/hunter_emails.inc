// hunter-connector/emails - likely work-email addresses for one person, each with a confidence and the status "guessed",
// ranked by common corporate patterns and, much more strongly, by the pattern of addresses the user already knows at that
// company. Only an address the user found published (with its URL) is "published". No mail server is contacted.
// C++ port of hunter-skill/skills/hunter-connector/emails.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::hunter_emails {
using namespace pyrt;
struct EmailError : std::runtime_error { using std::runtime_error::runtime_error; };

// pattern -> prior weight (rough priors for common conventions, not measured rates)
inline const std::vector<std::pair<std::string, double>> PATTERNS = {
    {"first.last", 0.36}, {"first", 0.14}, {"flast", 0.13}, {"firstlast", 0.09}, {"first_last", 0.05}, {"firstl", 0.05},
    {"f.last", 0.04}, {"last", 0.04}, {"last.first", 0.03}, {"lastf", 0.03}, {"first-last", 0.02}, {"first.l", 0.02},
};
inline const std::set<std::string> NEEDS_LAST = {"first.last", "flast", "firstlast", "first_last", "f.last", "last", "last.first", "lastf", "first-last"};
inline const std::set<std::string> TITLES = {"dr", "mr", "mrs", "ms", "mx", "prof", "sir"};
inline const std::set<std::string> SUFFIXES = {"jr", "sr", "ii", "iii", "iv", "phd", "md", "mba", "esq", "cpa", "pe"};
inline const std::set<std::string> ROLE_ADDRESSES = {"careers", "jobs", "recruiting", "hr", "info", "hello", "contact", "talent", "press",
                                                     "support", "admin", "team", "noreply", "no-reply"};
// Python's re `$` also matches just before one trailing newline
inline bool is_email(const std::string& s) {
    static const std::regex EMAIL(R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}\n?$)");
    return std::regex_search(s, EMAIL);
}
inline std::string ascii_fold(const std::string& s) {   // NFKD, then drop the combining marks
    std::vector<uint32_t> o;
    for (uint32_t c : uni::to_u32(uni::normalize("NFKD", s))) if (!uni::combining(c)) o.push_back(c);
    return uni::from_u32(o);
}
inline std::string strip_dots(const std::string& s) { std::size_t a = 0, b = s.size(); while (a < b && s[a] == '.') ++a; while (b > a && s[b - 1] == '.') --b; return s.substr(a, b - a); }
inline std::string ascii_lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; }
struct Name { std::string first, last, last_initial; bool initial_only = false, hyphenated = false; };
inline Name parse_name(const std::string& name) {   // 'Dr. María García-López Jr.' -> first maria, last garcialopez
    const std::string raw = replace_all(replace_all(ascii_fold(name), "'", ""), "\xE2\x80\x99", "");
    std::vector<std::string> tokens; std::string cur;
    for (const auto& ch : cps(strip(raw))) {   // re.split(r"[\s,]+", ...) without empty pieces
        const uint32_t c = cp_of(ch);
        if (c == ',' || py_space(c)) { if (!cur.empty()) tokens.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) tokens.push_back(cur);
    const std::size_t n0 = tokens.size();
    std::vector<std::string> kept;
    for (const auto& t : tokens) if (!TITLES.count(strip_dots(lower(t))) || n0 <= 2) kept.push_back(t);
    tokens = kept;
    while (tokens.size() > 2 && SUFFIXES.count(strip_dots(lower(tokens.back())))) tokens.pop_back();
    if (tokens.empty()) throw EmailError("no name given");
    const auto clean = [](const std::string& t) { std::string o; for (char c : lower(t)) if (c >= 'a' && c <= 'z') o += c; return o; };
    Name n;
    n.first = clean(tokens.front());
    const std::string last_raw = tokens.size() > 1 ? tokens.back() : "";
    n.last = clean(last_raw);
    if (n.first.empty()) throw EmailError("could not read a first name from " + repr(Val::str(name)));
    n.initial_only = n.last.size() <= 1;
    n.last_initial = n.last.substr(0, 1);
    n.hyphenated = last_raw.find('-') != std::string::npos;
    return n;
}
inline std::string local_part(const std::string& p, const Name& n) {
    const std::string& f = n.first; const std::string& l = n.last; const std::string& li = n.last_initial;
    const std::string f1 = f.substr(0, 1);
    if (p == "first.last") return f + "." + l;
    if (p == "first") return f;
    if (p == "flast") return f1 + l;
    if (p == "firstlast") return f + l;
    if (p == "first_last") return f + "_" + l;
    if (p == "firstl") return f + li;
    if (p == "f.last") return f1 + "." + l;
    if (p == "last") return l;
    if (p == "last.first") return l + "." + f;
    if (p == "lastf") return l + f1;
    if (p == "first-last") return f + "-" + l;
    return f + "." + li;   // first.l
}
inline std::pair<std::string, std::string> parse_known(const std::string& text) {   // 'Jordan Reyes <jordan.reyes@example.com>'
    static const std::regex KNOWN(R"(^\s*(.+?)\s*<\s*([^>]+?)\s*>\s*\n?$)");
    std::smatch m;
    if (!std::regex_search(text, m, KNOWN)) throw EmailError("--known wants 'First Last <address>', got " + repr(Val::str(text)));
    const std::string email = lower(m[2].str());
    if (!is_email(email)) throw EmailError("not an email address: " + repr(Val::str(m[2].str())));
    return {m[1].str(), email};
}
inline std::string before_at(const std::string& e) { return e.substr(0, e.find('@')); }
inline std::string after_at(const std::string& e) { const auto k = e.find('@'); return k == std::string::npos ? "" : e.substr(k + 1); }
inline std::vector<std::string> patterns_for(const std::string& name, const std::string& email) {   // the patterns that make this address
    const Name n = parse_name(name);
    std::string local = lower(before_at(email));
    const auto plus = local.find('+'); if (plus != std::string::npos) local = local.substr(0, plus);
    std::vector<std::string> hits;
    for (const auto& [p, w] : PATTERNS) {
        (void)w;
        if (NEEDS_LAST.count(p) && n.initial_only) continue;
        if (local_part(p, n) == local) hits.push_back(p);
    }
    return hits;
}
using Counts = std::vector<std::pair<std::string, double>>;   // {pattern: count} in insertion order
inline double& at(Counts& c, const std::string& k) { for (auto& kv : c) if (kv.first == k) return kv.second; c.emplace_back(k, 0.0); return c.back().second; }
inline std::pair<Counts, std::vector<std::string>> learn(const std::vector<std::pair<std::string, std::string>>& known, const std::string& domain = "") {
    Counts counts; std::vector<std::string> notes;
    for (const auto& [name, email] : known) {
        const std::string dom = after_at(email);
        if (!domain.empty() && dom != lower(domain)) { notes.push_back(email + " is at " + dom + ", not " + domain + ": not used"); continue; }
        if (ROLE_ADDRESSES.count(before_at(email))) { notes.push_back(email + " is a shared inbox, not a person: not used"); continue; }
        const auto hits = patterns_for(name, email);
        if (hits.empty()) { notes.push_back(email + " fits none of the common patterns for " + name); continue; }
        for (const auto& p : hits) at(counts, p) += 1.0 / static_cast<double>(hits.size());
    }
    return {counts, notes};
}
inline std::string label(double conf) { return conf >= 0.7 ? "high" : conf >= 0.3 ? "medium" : "low"; }
struct Cand { std::string email, pattern; double confidence; std::string label, status, why; };
struct Result { std::string name, domain; std::vector<Cand> candidates; std::optional<std::vector<std::string>> learned; std::vector<std::string> notes; };
inline Result guess(const std::string& name, const std::string& domain0, const std::vector<std::pair<std::string, std::string>>& known,
                    const std::string& found = "", const std::string& found_source = "") {
    std::string domain = strip(lower(domain0));
    { std::size_t k = 0; while (k < domain.size() && domain[k] == '@') ++k; domain = domain.substr(k); }
    static const std::regex DOMAIN(R"(^[a-z0-9.\-]+\.[a-z]{2,}\n?$)");
    if (!std::regex_search(domain, DOMAIN)) throw EmailError("--domain wants the company's email domain, like example.com");
    const Name n = parse_name(name);
    auto [counts, notes] = learn(known, domain);
    std::map<std::string, double> weights; for (const auto& [p, w] : PATTERNS) weights[p] = w;
    std::optional<std::vector<std::string>> learned;
    if (!counts.empty()) {
        double top = counts.front().second; for (const auto& kv : counts) top = std::max(top, kv.second);
        std::vector<std::string> best; for (const auto& kv : counts) if (kv.second == top) best.push_back(kv.first);
        std::sort(best.begin(), best.end());
        std::vector<double> cv; for (const auto& kv : counts) cv.push_back(kv.second);
        const long long support = static_cast<long long>(std::nearbyint(fsum(cv)));
        const double nb = static_cast<double>(best.size());
        double share = support >= 2 && best.size() == 1 ? 0.90 : 0.80;
        if (best.size() > 1) share = 0.80 / nb * nb;
        const auto in_best = [&](const std::string& p) { return std::find(best.begin(), best.end(), p) != best.end(); };
        std::vector<double> rv; for (const auto& [p, w] : PATTERNS) if (!in_best(p)) rv.push_back(w);
        double rest = fsum(rv); if (rest == 0) rest = 1.0;
        for (const auto& [p, w] : PATTERNS) weights[p] = in_best(p) ? share / nb : (1.0 - share) * w / rest;
        learned = best;
    }
    std::vector<Cand> out;
    // no renormalising when only an initial is known: the patterns left keep their small absolute weights
    for (const auto& [p, w0] : PATTERNS) {
        (void)w0;
        if (NEEDS_LAST.count(p) && n.initial_only) continue;
        const double conf = round_n(weights[p], 2);
        std::string why = "common pattern " + p;
        if (learned && std::find(learned->begin(), learned->end(), p) != learned->end()) {
            const long long n_known = std::max(1LL, static_cast<long long>(std::nearbyint(at(counts, p))));
            why = "pattern " + p + " learned from " + std::to_string(n_known) + " known address" + (n_known == 1 ? "" : "es") + " at " + domain;
            if (learned->size() > 1) why += " (known addresses disagree)";
        }
        out.push_back({local_part(p, n) + "@" + domain, p, conf, label(conf), "guessed", why});
    }
    if (n.initial_only)
        notes.push_back("only a last initial is known: patterns that need the full last name (first.last, flast, ...) are left out. Get the full name from their public profile first.");
    if (!found.empty()) {
        const std::string f = strip(lower(found));
        if (!is_email(f) || !f.ends_with("@" + domain)) throw EmailError("--found must be an address at " + domain);
        static const std::regex HTTP(R"(^https?://)");
        if (found_source.empty() || !std::regex_search(found_source, HTTP)) throw EmailError("--found needs --found-source: the public URL where the address is published");
        std::vector<Cand> kept; for (const auto& c : out) if (c.email != f) kept.push_back(c);
        const auto pf = patterns_for(name, f);
        kept.insert(kept.begin(), Cand{f, pf.empty() ? "published" : pf.front(), 1.0, "published", "published", "published at " + found_source});
        out = kept;
    }
    std::stable_sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) { return a.confidence != b.confidence ? a.confidence > b.confidence : a.email < b.email; });
    std::vector<Cand> uniq; std::set<std::string> seen;
    for (const auto& c : out) if (seen.insert(c.email).second) uniq.push_back(c);
    return {name, domain, uniq, learned, notes};
}
inline Val to_val(const Result& r) {
    Val d = Val::dict(), cs = Val::list(), notes = Val::list();
    for (const auto& c : r.candidates) {
        Val x = Val::dict();
        x.d->set("email", Val::str(c.email)); x.d->set("pattern", Val::str(c.pattern)); x.d->set("confidence", Val::flt(c.confidence));
        x.d->set("label", Val::str(c.label)); x.d->set("status", Val::str(c.status)); x.d->set("why", Val::str(c.why));
        cs.l->push_back(x);
    }
    Val learned;
    if (r.learned) { learned = Val::list(); for (const auto& p : *r.learned) learned.l->push_back(Val::str(p)); }
    for (const auto& s : r.notes) notes.l->push_back(Val::str(s));
    d.d->set("name", Val::str(r.name)); d.d->set("domain", Val::str(r.domain)); d.d->set("candidates", cs);
    d.d->set("learned", learned); d.d->set("notes", notes);
    return d;
}
inline std::string render(const Result& r, long long top = 6) {
    std::string o = "Likely addresses for " + r.name + " at " + r.domain + " (guesses ranked by pattern; none is verified):\n";
    const long long n = static_cast<long long>(r.candidates.size());
    const long long lim = top < 0 ? std::max(0LL, n + top) : std::min(top, n);   // candidates[:top]
    for (long long k = 0; k < lim; ++k) {
        const Cand& c = r.candidates[static_cast<std::size_t>(k)];
        o += "  " + ljust(c.email, 34) + " " + rjust(fmt_f(c.confidence, 2), 4) + "  " + ljust(c.label, 9) + " " + c.why + "\n";
    }
    for (const auto& s : r.notes) o += "note: " + s + "\n";
    o += "A guess is a guess: it becomes certain only when the address is published or the person replies.\n";
    return o;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("emails", "Likely work-email addresses, each with a confidence. No mail server is contacted.");
    p.subcommands("cmd", false);
    { Parser& g = p.sub("guess"); g.help_line = "ranked candidates for one person";
      g.opt({"--name"}).required(); g.opt({"--domain"}).required();
      g.opt({"--known"}).append().def(Val::list()).help("'First Last <address>' you already know there");
      g.opt({"--found"}).help("an address you found published on a public page");
      g.opt({"--found-source"}).help("the URL where it is published");
      g.opt({"--top"}).integer().def(6); g.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("pattern"); s.help_line = "which pattern known addresses use";
      s.opt({"--known"}).append().required(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    Result res;
    try {
        std::vector<std::pair<std::string, std::string>> known;
        const Val kl = get(a, "known");
        if (kl.t == Val::LIST) for (const Val& k : *kl.l) known.push_back(parse_known(pystr(k)));
        if (cmd == "pattern") {
            auto [counts, notes] = learn(known);
            std::stable_sort(counts.begin(), counts.end(), [](const auto& x, const auto& y) { return -x.second < -y.second; });
            for (const auto& [pn, c] : counts) print(ljust(pn, 12) + " fits " + fmt_g(c) + " known address" + (c == 1 ? "" : "es"));
            for (const auto& s : notes) print("note: " + s);
            return counts.empty() ? 1 : 0;
        }
        res = guess(S(a, "name"), S(a, "domain"), known, S(a, "found"), S(a, "found_source"));
    } catch (const EmailError& e) {
        eprint(std::string("error: ") + e.what());
        return 2;
    }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(to_val(res), o)); }
    else out(render(res, get(a, "top").i));
    return 0;
}
PACKTOOL("hunter-connector/emails", main);
}  // namespace packtools::hunter_emails
