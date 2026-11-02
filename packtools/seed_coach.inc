// seed-coach/coach: drills the founder on the hard questions investors ask, tailored to their own numbers in seed/,
// grades written answers on five checks, and plans the first call. A C++ port of seed-skill/seed-coach/coach.py (MIT,
// Jake Schincariol); same output and exit codes: drill/agenda 0 done, 1 nothing to ask or no such investor; grade 0
// every check passed, 1 something to fix; 2 bad input.
namespace packtools::seed_coach {
using namespace pyrt;
using namespace packtools::seed;

struct CoachError : std::runtime_error {
    int code;
    explicit CoachError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {}
};

constexpr int WPM = 150;
inline const std::vector<std::string> HEDGES = {"i think", "i guess", "i believe", "we believe", "we think", "maybe", "hopefully", "kind of",
                                                "sort of", "probably", "basically", "might", "perhaps", "try to", "we hope", "fingers crossed"};
inline const std::vector<std::string> BUZZ = {"synergy", "disrupt", "revolutionize", "revolutionise", "game-changer", "game changer", "best-in-class",
                                              "world-class", "paradigm", "uber for", "next-gen", "cutting-edge", "huge market", "everyone needs"};
inline const std::vector<std::pair<std::string, int>> BLOCKS = {{"Hello and why them", 2}, {"The problem", 3}, {"The product", 4}, {"Market and model", 4},
                                                                {"Their questions", 4}, {"The ask", 2}, {"Next step", 1}};
inline const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
inline const char* const DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

inline std::string bank_default() { return abspath(packtools::skill_file("questions.json")); }

// ---------------------------------------------------------------- reading seed/
inline Val read_json(const std::string& path) {   // None when the file cannot be opened
    std::string text;
    try { text = read_text(path); } catch (const OSError&) { return Val(); }
    if (is_dir(path)) return Val();
    try {
        return loads(text);
    } catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw CoachError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline Val load_bank(const std::string& path) {
    const Val doc = read_json(path);
    if (!truthy(doc) || !truthy(get(doc, "questions"))) throw CoachError("no question bank at " + path);
    return doc;
}
inline Val item(const Val& o, const std::string& k) {   // o[k]
    if (!has(o, k)) throw PyErr("KeyError", str_repr(k));
    return get(o, k);
}
inline Val get_or(const Val& o, const std::string& k, const Val& dflt) { return has(o, k) ? get(o, k) : dflt; }   // dict.get(k, default)
inline Val or_dict(const Val& v) { return truthy(v) ? v : Val::dict(); }   // (x or {})

inline Val money(const Val& x) {   // 1500000 -> $1.5M, 17040000000 -> $17.04B, 3.75 -> $3.75, 2.225 -> $2.23 (half up)
    if (x.is_none()) return Val();
    const Decimal v = Decimal::parse(ascii_digits(pystr(x)));
    if (v.is_nan()) throw PyErr("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
    static const std::pair<long long, const char*> DIVS[] = {{1000000000LL, "B"}, {1000000LL, "M"}};
    for (const auto& [div, suf] : DIVS) {
        if (v >= Decimal(div)) {
            char buf[512]; std::snprintf(buf, sizeof buf, "%.2f", (v / Decimal(div)).to_double());
            return sv("$" + rstrip_chars(rstrip_chars(buf, "0"), ".") + suf);
        }
    }
    if (v == v.to_integral_value()) return sv("$" + commas(v.to_int()));
    return sv("$" + v.quantize(Decimal::parse("0.01"), Rounding::HALF_UP).format(","));
}
inline Val money_d(double d) { return money(Val::flt(d)); }

inline std::string int_str(double v) { return Decimal::from_double(v).to_int().str(); }   // str(int(v))
inline Val num(const Val& x) {
    if (x.is_none()) return Val();
    const double v = to_float(x);
    if (std::isinf(v)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    if (std::isnan(v)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (v == std::trunc(v)) return sv(int_str(v));
    char buf[512]; std::snprintf(buf, sizeof buf, "%.2f", v);
    return sv(rstrip_chars(rstrip_chars(buf, "0"), "."));
}

inline bool blank(const Val& v) { return v.is_none() || (v.t == Val::STR && v.s.empty()) || (v.t == Val::LIST && v.l->empty()); }   // in (None, "", [])
inline void put(Val& ctx, const std::string& key, const Val& value) { if (!blank(value)) ctx.d->set(key, value); }

inline Val find_investor(const std::string& sd, const std::string& ref) {
    const std::string path = join_path(sd, "investors.json");
    const Val book = read_json(path);
    if (!truthy(book)) throw CoachError("no " + path + ": rank investors with /seed-investors first", 1);
    for (const Val& x : items_of(get(book, "investors"))) {
        const Val id = get(x, "id"), name = get(x, "name");
        if ((id.t == Val::STR && id.s == ref) || lower(truthy(name) ? pystr(name) : "") == lower(ref)) return x;
    }
    throw CoachError("no investor " + str_repr(ref) + " in " + path, 1);
}

inline Val context(const std::string& sd, const std::string& investor = "", const std::string& customers = "", const std::string& provider = "") {
    Val ctx = Val::dict();   // everything a tailored question can use; a missing file just leaves its keys out
    const Val cfg = or_dict(read_json(join_path(sd, "config.json")));
    const Val st = or_dict(get(cfg, "startup"));
    put(ctx, "startup", get(st, "name"));
    const Val r = get(st, "raise");
    if (!(r.is_none() || (r.t == Val::STR && r.s.empty()))) {
        std::string u; for (char c : pystr(r)) if (c != '$' && c != ',') u += c;
        try { put(ctx, "raise", money_d(to_float(Val::str(u)))); }
        catch (const PyErr& e) { if (e.type != "ValueError") throw; put(ctx, "raise", sv(pystr(r))); }
    }
    put(ctx, "instrument", get(st, "instrument"));
    put(ctx, "one_liner", get(st, "one_liner"));
    ctx.d->set("customers", !customers.empty() ? sv(customers) : truthy(get(st, "customers")) ? get(st, "customers") : sv("users"));
    const Val prov = !provider.empty() ? sv(provider) : get(st, "provider");
    ctx.d->set("provider_pay", truthy(prov) ? sv(pystr(prov) + " pay") : sv("costs"));

    const Val comp = or_dict(read_json(join_path(sd, "competitors.json")));
    List apps = items_of(get(comp, "apps"));
    auto rating_key = [](const Val& a) { const Val r = get(a, "ratings"); return truthy(r) ? dec_of(r) : Decimal(0); };
    std::stable_sort(apps.begin(), apps.end(), [&](const Val& x, const Val& y) { return rating_key(x) > rating_key(y); });
    for (std::size_t i = 0; i < apps.size() && i < 2; ++i) {
        const Val& a = apps[i];
        const std::string p = "c" + std::to_string(i + 1) + "_";
        put(ctx, p + "name", get(a, "name"));
        if (!get(a, "ratings").is_none()) put(ctx, p + "ratings", sv(commas(int_of(get(a, "ratings")))));
        put(ctx, p + "rating", get(a, "rating"));
        put(ctx, p + "weak", get(a, "weak_point"));
        const Val key = get(a, "top_theme");
        Val cnt = get(a, "weak_point_count");
        if (cnt.is_none() && truthy(key)) cnt = key.t == Val::STR ? get(or_dict(get(a, "themes")), key.s) : Val();
        put(ctx, p + "count", cnt);
        put(ctx, p + "low", get(a, "low_reviews"));
    }

    const Val model = or_dict(read_json(join_path(sd, "model.json")));
    const Val s = or_dict(get(model, "summary")), ue = or_dict(get(model, "unit_economics")), asm_ = or_dict(get(model, "assumptions"));
    const Val take = get_or(ue, "take_rate_pct", get(asm_, "take_rate_pct"));
    if (!take.is_none()) ctx.d->set("take_rate", sv(num(take).s + "%"));
    const Val unit = get(ue, "unit");
    put(ctx, "unit", truthy(unit) ? unit : get(asm_, "unit"));
    put(ctx, "price", money(get_or(ue, "price", get(asm_, "price"))));
    for (const char* k : {"revenue_per_order", "contribution_per_order", "cost_per_order", "ltv"}) put(ctx, k, money(get(ue, k)));
    put(ctx, "cac", money(get_or(ue, "cac", get(asm_, "cac"))));
    if (!get(ue, "ltv_cac").is_none()) ctx.d->set("ltv_cac", sv(num(get(ue, "ltv_cac")).s + "x"));
    put(ctx, "cac_payback", num(get(ue, "cac_payback_months")));
    put(ctx, "breakeven_month", get(s, "breakeven_month"));
    put(ctx, "runway", get(s, "runway_months"));
    if (truthy(get(s, "peak_burn"))) put(ctx, "peak_burn", money(get(s, "peak_burn")));
    for (const char* k : {"revenue_y1", "revenue_y3"}) put(ctx, k, money(get(s, k)));
    if (truthy(get(s, "raise")) && !has(ctx, "raise")) put(ctx, "raise", money(get(s, "raise")));
    Val funds = get(s, "use_of_funds");
    if (!truthy(funds)) funds = get(asm_, "use_of_funds");
    if (truthy(funds)) {
        if (funds.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(funds) + "' object has no attribute 'items'");
        const std::pair<std::string, Val>* top = nullptr; double best = 0;
        for (const auto& kv : funds.d->items) { const double v = to_float(kv.second); if (!top || v > best) { top = &kv; best = v; } }
        ctx.d->set("top_fund", sv(top->first)); ctx.d->set("top_fund_amount", money(top->second));
    }

    const Val mk = or_dict(read_json(join_path(sd, "market.json")));
    for (const char* k : {"tam", "sam", "som"}) put(ctx, k, money(get(mk, k)));
    if (!get(mk, "som_share_pct").is_none()) ctx.d->set("som_share", sv(num(get(mk, "som_share_pct")).s + "%"));
    put(ctx, "som_years", get(mk, "som_years"));
    for (const Val& x : items_of(get(mk, "inputs"))) {
        const Val kind = get(x, "kind"), lv = get(x, "level");
        if (kind.t == Val::STR && kind.s == "assumption" && lv.t == Val::STR && (lv.s == "tam" || lv.s == "sam")) {
            const Val u = get(x, "unit");
            ctx.d->set("assumption", sv(pystr(item(x, "name")) + ": " + num(item(x, "value")).s + (u.t == Val::STR && u.s == "%" ? "%" : "")));
            break;
        }
    }

    if (!investor.empty()) {
        const Val inv = find_investor(sd, investor);
        put(ctx, "inv_name", get(inv, "name"));
        const Val nm = get(inv, "name");
        const std::string full = truthy(nm) ? pystr(nm) : "";
        put(ctx, "inv_first", sv(full.substr(0, full.find(' '))));
        put(ctx, "inv_firm", get(inv, "firm"));
        const Val deal = or_dict(get(inv, "last_relevant_deal"));
        put(ctx, "inv_deal", get(deal, "what"));
        if (!get(inv, "check_min").is_none()) {
            const Val lo = money(item(inv, "check_min")), hi = money(item(inv, "check_max"));
            put(ctx, "inv_check", sv((lo.is_none() ? "None" : lo.s) + "-" + (hi.is_none() ? "None" : hi.s)));
        }
    }
    if (!has(ctx, "startup")) ctx.d->set("startup", sv("your company"));
    return ctx;
}

// ---------------------------------------------------------------- drill
// string.Formatter().parse for the bank's plain {name} fields, with {{ and }} escapes
struct Piece { std::string lit; bool field = false; std::string name, conv, spec; };
inline std::vector<Piece> parse_format(const std::string& text) {
    std::vector<Piece> out; std::string lit;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') { lit += '{'; ++i; continue; }
        if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') { lit += '}'; ++i; continue; }
        if (c == '}') throw PyErr("ValueError", "Single '}' encountered in format string");
        if (c != '{') { lit += c; continue; }
        const std::size_t end = text.find('}', i);
        if (end == std::string::npos) throw PyErr("ValueError", "expected '}' before end of string");
        std::string body = text.substr(i + 1, end - i - 1);
        Piece p; p.lit = lit; p.field = true; lit.clear();
        const std::size_t colon = body.find(':');
        if (colon != std::string::npos) { p.spec = body.substr(colon + 1); body = body.substr(0, colon); }
        const std::size_t bang = body.find('!');
        if (bang != std::string::npos) { p.conv = body.substr(bang + 1); body = body.substr(0, bang); }
        p.name = body;
        out.push_back(p);
        i = end;
    }
    if (!lit.empty()) { Piece p; p.lit = lit; out.push_back(p); }
    return out;
}
inline std::vector<std::string> fields(const std::string& text) {
    std::vector<std::string> f; for (const Piece& p : parse_format(text)) if (p.field && !p.name.empty()) f.push_back(p.name);
    return f;
}
inline Val fill(const std::string& text, const Val& ctx) {   // the text with its placeholders filled, or None if one is missing
    for (const std::string& f : fields(text)) if (!has(ctx, f)) return Val();
    std::string o;
    for (const Piece& p : parse_format(text)) {
        o += p.lit;
        if (!p.field) continue;
        if (p.name.empty()) throw PyErr("IndexError", "Replacement index 0 out of range for positional args tuple");
        const Val v = get(ctx, p.name);
        std::string s = p.conv == "r" ? repr(v) : pystr(v);
        if (!p.conv.empty() && p.conv != "r" && p.conv != "s") throw PyErr("ValueError", "Unknown conversion specifier " + p.conv);
        if (!p.spec.empty()) throw PyErr("ValueError", "format specs are not used by the question bank: " + p.spec);
        o += s;
    }
    return sv(o);
}

struct Q { Val item; bool tailored; std::string key; };   // key: the item's JSON, for Python's `item not in out` (dict equality)
inline std::vector<Val> pick(const Val& bank, const Val& ctx, long n = 10, const std::string& category = "", const Val& seed = Val()) {
    std::vector<std::string> cats;
    for (const Val& c : items_of(get(bank, "categories"))) cats.push_back(pystr(c));
    if (!category.empty()) {
        if (std::find(cats.begin(), cats.end(), category) == cats.end()) throw CoachError("--category is one of: " + join(cats, ", "));
        cats = {category};
    }
    std::vector<std::vector<Q>> groups;
    for (const std::string& c : cats) {
        std::vector<Q> tailored, generic;
        const Val qrows = item(bank, "questions");
        for (const Val& q : items_of(qrows)) {
            const Val qc = get(q, "category");
            if (!(qc.t == Val::STR && qc.s == c)) continue;
            const std::string qt = pystr(item(q, "q"));
            const Val text = fill(qt, ctx);
            if (text.is_none()) continue;
            const bool tl = !fields(qt).empty();
            const Val hint = truthy(get(q, "hint")) ? fill(pystr(get(q, "hint")), ctx) : Val();
            const Val it = obj({{"id", item(q, "id")}, {"category", sv(c)}, {"q", text}, {"tests", get(q, "tests")}, {"strong", get(q, "strong")},
                                {"tailored", bv(tl)}, {"hint", hint}});
            DumpOpts o; o.compact = true;
            (tl ? tailored : generic).push_back({it, tl, dumps(it, o)});
        }
        if (!seed.is_none()) {
            PyRandom rnd(pystr(seed) + ":" + c);
            rnd.shuffle(tailored);
            rnd.shuffle(generic);
        }
        std::vector<Q> g = tailored; g.insert(g.end(), generic.begin(), generic.end());
        groups.push_back(g);
    }
    std::vector<Q> out;
    auto in_out = [&](const Q& q) { for (const Q& o : out) if (o.key == q.key) return true; return false; };
    for (bool want : {true, false}) {   // tailored first: one per category per round, then the rest round-robin
        bool more = true;
        while (more && static_cast<long>(out.size()) < n) {
            more = false;
            for (const auto& g : groups) {
                for (const Q& q : g) {
                    if (q.tailored == want && !in_out(q)) { out.push_back(q); more = true; break; }
                }
                if (static_cast<long>(out.size()) >= n) break;
            }
        }
    }
    std::vector<Val> res;
    for (std::size_t i = 0; i < out.size() && static_cast<long>(i) < n; ++i) res.push_back(out[i].item);
    return res;
}

inline void write_file(const std::string& path, const std::string& text) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    write_text(path, text);
}
inline std::string pad2(long long v) { char b[32]; std::snprintf(b, sizeof b, "%2lld", v); return b; }   // "%2d"

inline int cmd_drill(const Val& a) {
    const BigInt nb = int_of(get(a, "n"));
    if (cmp(nb, BigInt(1)) < 0 || cmp(nb, BigInt(50)) > 0) throw CoachError("--n is 1 to 50");
    const long n = static_cast<long>(nb.to_ll());
    const std::string sd = truthy(get(a, "dir")) ? S(a, "dir") : "seed";
    const Val ctx = context(sd, S(a, "investor"), S(a, "customers"), S(a, "provider"));
    const std::vector<Val> qs = pick(load_bank(S(a, "bank")), ctx, n, S(a, "category"), get(a, "seed"));
    if (qs.empty()) { print("No questions to ask."); return 1; }
    const bool js = truthy(get(a, "json"));
    if (js) {
        Val l = Val::list(); for (const Val& q : qs) l.l->push_back(q);
        DumpOpts o; o.indent = 2; o.ensure_ascii = false;
        print(dumps(obj({{"questions", l}}), o));
    } else {
        const std::string who = truthy(get(a, "investor")) ? " for " + pystr(item(ctx, "inv_name")) + " (" + (truthy(get(ctx, "inv_firm")) ? pystr(get(ctx, "inv_firm")) : "") + ")" : "";
        long tl = 0; for (const Val& q : qs) if (get(q, "tailored").b) ++tl;
        print("Pitch drill" + who + ": " + std::to_string(qs.size()) + " questions, " + std::to_string(tl) + " tailored to your numbers");
        for (std::size_t i = 0; i < qs.size(); ++i) {
            const Val& q = qs[i];
            print(pad2(static_cast<long long>(i + 1)) + ". [" + get(q, "category").s + "] " + get(q, "q").s);
            print("    They are checking: " + pystr(get(q, "tests")) + ". Strong: " + pystr(get(q, "strong")) + ".");
            if (truthy(get(q, "hint"))) print("    Your numbers: " + pystr(get(q, "hint")));
        }
        print("Answer out loud in under 60 seconds, then: coach.py grade --answer \"...\" --question ID");
    }
    if (truthy(get(a, "write"))) {
        const std::string path = join_path(join_path(sd, "coach"), "drill.md");
        std::vector<std::string> lines = {"# Pitch drill", ""};
        for (std::size_t i = 0; i < qs.size(); ++i) {
            const Val& q = qs[i];
            lines.push_back(std::to_string(i + 1) + ". **" + get(q, "q").s + "** (" + get(q, "category").s + ")");
            lines.push_back("   - Checking: " + pystr(get(q, "tests")));
            lines.push_back("   - Strong: " + pystr(get(q, "strong")));
            if (truthy(get(q, "hint"))) lines.push_back("   - Your numbers: " + pystr(get(q, "hint")));
        }
        write_file(path, join(lines, "\n") + "\n");
        if (!js) print("Wrote " + path + ".");
    }
    return 0;
}

// ---------------------------------------------------------------- grade
inline const std::string PUNCT = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";   // string.punctuation
inline Val grade(const std::string& text) {   // five checks, 20 points each
    const std::string t = squash(text);
    if (t.empty()) throw CoachError("grade needs an answer: --answer \"...\" or --file FILE");
    const long n = static_cast<long>(re::findall(R"([A-Za-z0-9$%][A-Za-z0-9$%'.,-]*)", t).size());
    const long secs = static_cast<long>(std::nearbyint(n * 60.0 / WPM));
    const std::string low = " " + lower(t) + " ";
    struct Check { std::string name; bool ok; std::string note; };
    std::vector<Check> checks;
    const bool len_ok = 15 <= n && n <= 150;
    checks.push_back({"length", len_ok, len_ok ? std::to_string(n) + " words, about " + std::to_string(secs) + " seconds spoken"
                                       : n < 15 ? std::to_string(n) + " words: too thin, give the reason and one number"
                                                : std::to_string(n) + " words, about " + std::to_string(secs) + " seconds: over a minute, cut it to 150 words"});
    const bool has_num = static_cast<bool>(re::search(R"(\d)", t));
    checks.push_back({"a number", has_num, has_num ? "it has a number" : "no number: add the one that proves it"});
    std::vector<std::string> hedges;
    for (const std::string& h : HEDGES) if (re::search("(?<![a-z])" + h + "(?![a-z])", low)) hedges.push_back(h);   // plain letters and spaces: no escaping needed
    checks.push_back({"no hedging", hedges.empty(), hedges.empty() ? "no hedging words" : "hedging: " + join(hedges, ", ") + ". Say it straight"});
    std::vector<std::string> sentences;
    for (const std::string& s : re::split(R"((?<=[.!?])\s+)", t)) if (!s.empty()) sentences.push_back(s);
    long specifics = static_cast<long>(re::findall(R"(\d[\d,.]*%?)", t).size());
    for (const std::string& s : sentences) {
        std::vector<std::string> toks; for (const std::string& w : split(s, " ")) if (!w.empty()) toks.push_back(w);
        for (std::size_t k = 1; k < toks.size(); ++k) {
            const auto c = cps(toks[k]);
            if (!c.empty() && isupper_str(c[0]) && strip_chars(toks[k], PUNCT) != "I") ++specifics;
        }
    }
    std::vector<std::string> buzz; for (const std::string& b : BUZZ) if (low.find(b) != std::string::npos) buzz.push_back(b);
    const bool spec_ok = specifics >= 2 && buzz.empty();
    checks.push_back({"specific", spec_ok, spec_ok ? std::to_string(specifics) + " specifics (numbers and names)"
                                          : !buzz.empty() ? "buzzwords: " + join(buzz, ", ")
                                                          : "only " + std::to_string(specifics) + " specifics: name the customer, the competitor, the number"});
    long first = 0;
    if (!sentences.empty()) for (const std::string& w : split(sentences[0], " ")) if (!w.empty()) ++first;
    checks.push_back({"answer first", first <= 25, first <= 25 ? "the first sentence is " + std::to_string(first) + " words"
                                                             : "the first sentence is " + std::to_string(first) + " words: lead with the answer in under 25"});
    long score = 0; for (const Check& c : checks) if (c.ok) score += 20;
    Val cl = Val::list();
    for (const Check& c : checks) cl.l->push_back(obj({{"check", sv(c.name)}, {"ok", bv(c.ok)}, {"note", sv(c.note)}}));
    return obj({{"score", iv(score)}, {"passed", bv(score == 100)}, {"words", iv(n)}, {"seconds", iv(secs)}, {"checks", cl}});
}

inline int cmd_grade(const Val& a) {
    std::string text;
    if (truthy(get(a, "file"))) {
        if (is_dir(S(a, "file"))) throw CoachError("no file " + S(a, "file"));
        try { text = read_text(S(a, "file")); }
        catch (const OSError&) { throw CoachError("no file " + S(a, "file")); }
    } else {
        text = S(a, "answer");
    }
    const Val g = grade(text);
    Val q;
    if (truthy(get(a, "question"))) {
        const Val rows = item(load_bank(S(a, "bank")), "questions");   // bound: never range-for over a temporary's list
        for (const Val& x : items_of(rows)) {
            const Val id = item(x, "id");
            if (q.is_none() && id.t == Val::STR && id.s == S(a, "question")) q = x;   // the list comprehension reads every id
        }
        if (q.is_none()) throw CoachError("no question " + str_repr(S(a, "question")) + " in the bank");
    }
    if (truthy(get(a, "json"))) {
        Val out = Val::dict(); for (const auto& kv : g.d->items) out.d->set(kv.first, kv.second);
        out.d->set("question", q.is_none() ? Val() : item(q, "id"));
        DumpOpts o; o.indent = 2;
        print(dumps(out, o));
    } else {
        if (!q.is_none()) {
            print("Question " + pystr(item(q, "id")) + ": " + pystr(item(q, "q")));
            print("  A strong answer has: " + pystr(item(q, "strong")) + ".");
        }
        print("Score " + std::to_string(get(g, "score").i) + "/100");
        for (const Val& c : *get(g, "checks").l) print(std::string("  [") + (get(c, "ok").b ? "x" : " ") + "] " + ljust(get(c, "check").s, 13) + " " + get(c, "note").s);
        print(get(g, "passed").b ? "Strong. Say it out loud twice more." : "Fix the unchecked lines and try again.");
    }
    return get(g, "passed").b ? 0 : 1;
}

// ---------------------------------------------------------------- agenda
inline std::vector<long> split_minutes(long total) {   // the seven blocks scaled to total minutes, largest remainder, summing exactly
    if (total < 10 || total > 60) throw CoachError("--minutes is 10 to 60");
    std::vector<double> raw; for (const auto& b : BLOCKS) raw.push_back(b.second * total / 20.0);
    std::vector<long> mins; for (double r : raw) mins.push_back(std::max(1L, static_cast<long>(r)));
    std::vector<std::size_t> order(raw.size()); for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
        const double fx = raw[x] - static_cast<long>(raw[x]), fy = raw[y] - static_cast<long>(raw[y]);
        return fx != fy ? fx > fy : x < y;
    });
    auto sum = [&] { long s = 0; for (long m : mins) s += m; return s; };
    for (std::size_t i = 0; sum() < total; ++i) mins[order[i % order.size()]] += 1;
    while (sum() > total) {
        std::size_t j = 0;
        for (std::size_t k = 1; k < mins.size(); ++k) if (mins[k] > mins[j]) j = k;   // max by (mins, -k): the first of the largest
        mins[j] -= 1;
    }
    return mins;
}

struct DT { long long day; int hour, minute; };   // days since 1970-01-01 and a wall-clock time
inline int weekday(long long day) { return static_cast<int>(((day + 3) % 7 + 7) % 7); }   // Monday = 0
inline long long nth_sunday(long long y, int m, int n) { const long long d = make_day(y, m, 1); return d + (6 - weekday(d)) % 7 + 7 * (n - 1); }
inline long long minutes_of(const DT& t) { return t.day * 1440 + t.hour * 60 + t.minute; }
inline DT from_minutes(long long v) { long long d = v / 1440, r = v % 1440; if (r < 0) { r += 1440; --d; } return {d, static_cast<int>(r / 60), static_cast<int>(r % 60)}; }
inline DT utc_to_eastern(const DT& utc) {   // US rule since 2007: daylight time from the 2nd Sunday of March to the 1st Sunday of November
    long long y; unsigned mo, dd; civil_from(utc.day, y, mo, dd);
    const long long start = nth_sunday(y, 3, 2) * 1440 + 7 * 60, end = nth_sunday(y, 11, 1) * 1440 + 6 * 60, u = minutes_of(utc);
    return from_minutes(u + (start <= u && u < end ? -4 : -5) * 60);
}
inline std::string clock(const DT& t) {
    long long y; unsigned mo, dd; civil_from(t.day, y, mo, dd);
    char b[128];
    std::snprintf(b, sizeof b, "%s %s %lld %lld, %d:%02d %s", DAYS[weekday(t.day)], MONTHS[mo - 1], static_cast<long long>(dd), y,
                  t.hour % 12 ? t.hour % 12 : 12, t.minute, t.hour < 12 ? "AM" : "PM");
    return b;
}

inline DT strptime_compact(const std::string& s) {   // datetime.strptime(s, "%Y%m%dT%H%M") for 8 digits, T, 4 digits
    const auto c = cps(s);
    const std::string bad = "time data " + str_repr(s) + " does not match format '%Y%m%dT%H%M'";
    if (c.size() != 13 || c[8] != "T") throw PyErr("ValueError", bad);
    std::string d; for (std::size_t k = 0; k < 13; ++k) if (k != 8) d += ascii_digits(c[k]);
    const int y = std::stoi(d.substr(0, 4)), mo = std::stoi(d.substr(4, 2)), dd = std::stoi(d.substr(6, 2)), h = std::stoi(d.substr(8, 2)), mi = std::stoi(d.substr(10, 2));
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31 || h > 23 || mi > 59) throw PyErr("ValueError", bad);
    if (y < 1 || !valid_date(y, mo, dd)) throw PyErr("ValueError", y < 1 ? "year 0 is out of range" : "day is out of range for month");
    return {make_day(y, mo, dd), h, mi};
}

inline Val read_call(const std::string& path) {   // seed/calls/<id>.ics -> {when, link} or None; a UTC time is shown in New York time
    std::string raw;
    try { if (is_dir(path)) return Val(); raw = read_text(path); } catch (const OSError&) { return Val(); }
    const std::string text = re::sub(R"(\r?\n[ \t])", "", raw);   // unfold long lines
    Val out = Val::dict();
    const re::Match m = re::search(R"(^DTSTART(;[^:\r\n]*)?:(\d{8}T\d{4,6})(Z?)\s*$)", text, re::M);
    if (m) {
        const auto c = cps(m.group(2));
        std::string first13; for (std::size_t k = 0; k < c.size() && k < 13; ++k) first13 += c[k];
        const DT dt = strptime_compact(first13);
        const re::Match tz = re::search(R"(TZID=([^;:]+))", m.has(1) ? m.group(1) : "");
        if (m.group(3) == "Z") out.d->set("when", sv(clock(utc_to_eastern(dt)) + " ET"));
        else out.d->set("when", sv(clock(dt) + (tz ? " (" + tz.group(1) + ")" : "")));
    }
    std::vector<std::string> keep;
    for (const std::string& l : splitlines(text)) {
        const std::string head = split(split(l, ":", 1)[0], ";")[0];
        if (head == "URL" || head == "LOCATION" || head == "DESCRIPTION") keep.push_back(l);
    }
    const std::vector<std::string> links = re::findall(R"(https?://[^\s\\,;]+)", join(keep, "\n"));
    if (!links.empty()) out.d->set("link", sv(links[0]));
    return out.d->items.empty() ? Val() : out;
}

inline Val build_agenda(const std::string& sd, const std::string& ref, long minutes, const std::string& customers, const std::string& provider) {
    const Val inv = find_investor(sd, ref);
    const std::string id = pystr(item(inv, "id"));
    const Val ctx = context(sd, id, customers, provider);
    const Val call = read_call(join_path(join_path(sd, "calls"), id + ".ics"));
    const Val pipe = or_dict(read_json(join_path(sd, "pipeline.json")));
    Val status;
    for (const Val& r : items_of(get(pipe, "investors"))) { const Val rid = get(r, "id"); if (rid.t == Val::STR && rid.s == id) status = get(r, "status"); }
    const std::vector<long> mins = split_minutes(minutes);
    std::vector<std::string> problem;
    for (int i : {1, 2}) {
        const std::string p = "c" + std::to_string(i) + "_";
        if (has(ctx, p + "name") && has(ctx, p + "count"))
            problem.push_back(pystr(item(ctx, p + "name")) + ": " + pystr(item(ctx, p + "count")) + " of " + pystr(item(ctx, p + "low")) + " recent 1-2 star reviews mention " + pystr(item(ctx, p + "weak")) + ".");
    }
    std::vector<std::string> mparts;
    for (const char* k : {"tam", "sam", "som"}) if (has(ctx, k)) mparts.push_back(upper(k) + " " + pystr(get(ctx, k)));
    const std::string market = join(mparts, ", ");
    std::vector<std::string> model;
    if (has(ctx, "take_rate")) model.push_back(pystr(get(ctx, "take_rate")) + " take rate");
    if (has(ctx, "contribution_per_order")) model.push_back(pystr(get(ctx, "contribution_per_order")) + " contribution a " + (has(ctx, "unit") ? pystr(get(ctx, "unit")) : "order"));
    if (has(ctx, "ltv_cac")) model.push_back("LTV:CAC " + pystr(get(ctx, "ltv_cac")));
    if (has(ctx, "breakeven_month")) model.push_back("breakeven in month " + pystr(get(ctx, "breakeven_month")));
    std::vector<std::string> likely; for (const Val& q : pick(load_bank(bank_default()), ctx, 3)) likely.push_back(get(q, "q").s);
    std::string ask = (has(ctx, "raise") ? pystr(get(ctx, "raise")) : "The raise") + " " + (truthy(get(ctx, "instrument")) ? pystr(get(ctx, "instrument")) : "round");
    if (has(ctx, "top_fund")) ask += "; the biggest use: " + pystr(get(ctx, "top_fund")) + " (" + pystr(item(ctx, "top_fund_amount")) + ")";
    const std::string mm = join(model, ", ");
    std::vector<std::string> mm2; for (const std::string& x : {market, mm}) if (!x.empty()) mm2.push_back(x);
    const std::string probs = join(problem, " ");
    const std::vector<std::string> texts = {
        "Thank them. " + (truthy(get(inv, "personal_line")) ? "Open with your line: \"" + pystr(get(inv, "personal_line")) + "\"" : std::string("Say why them.")) + " Ask what they are looking at right now.",
        probs.empty() ? "The problem in one sentence, with the evidence." : probs,
        pystr(item(ctx, "startup")) + ": " + (truthy(get(ctx, "one_liner")) ? pystr(get(ctx, "one_liner")) : "the product in one line") + ". Show it, do not describe it.",
        mm2.empty() ? "The market size and the model, two numbers each." : join(mm2, "; "),
        "Let them ask. Be ready for: " + join(likely, " / "),
        ask + ". Ask who else they would want in the round.",
        "Ask for the next step and a date: a partner meeting, then the deck and the data room.",
    };
    Val rows = Val::list(); long t = 0;
    for (std::size_t i = 0; i < BLOCKS.size(); ++i) {
        rows.l->push_back(obj({{"start", iv(t)}, {"end", iv(t + mins[i])}, {"title", sv(BLOCKS[i].first)}, {"minutes", iv(mins[i])}, {"plan", sv(texts[i])}}));
        t += mins[i];
    }
    Val why = Val::list();
    for (const Val& r : items_of(get(inv, "reasons"))) {
        const std::string s = pystr(r);
        if (!(s.size() >= 4 && s.compare(s.size() - 4, 4, "(+0)") == 0) && why.l->size() < 3) why.l->push_back(r);
    }
    return obj({{"investor", sv(id)}, {"name", item(inv, "name")}, {"firm", get(inv, "firm")}, {"role", get(inv, "role")}, {"minutes", iv(minutes)},
                {"call", call}, {"status", status}, {"why", why}, {"blocks", rows}});
}

inline std::vector<std::string> agenda_lines(const Val& ag) {
    std::vector<std::string> out = {"First call: " + pystr(get(ag, "name")) + (truthy(get(ag, "role")) ? ", " + pystr(get(ag, "role")) : "") +
                                    (truthy(get(ag, "firm")) ? ", " + pystr(get(ag, "firm")) : "") + " (" + std::to_string(get(ag, "minutes").i) + " minutes)"};
    const Val call = get(ag, "call");
    if (truthy(call)) {
        out.push_back("When: " + (has(call, "when") ? pystr(get(call, "when")) : "see the invite") + (truthy(get(call, "link")) ? ", " + pystr(get(call, "link")) : ""));
    } else if (!(get(ag, "status").t == Val::STR && get(ag, "status").s == "booked")) {
        out.push_back("Not booked yet: /seed-closer books the call and writes the invite.");
    }
    if (!get(ag, "why").l->empty()) {
        std::vector<std::string> w; for (const Val& x : *get(ag, "why").l) w.push_back(pystr(x));
        out.push_back("Why them: " + join(w, "; "));
    }
    for (const Val& b : *get(ag, "blocks").l) {
        char se[64]; std::snprintf(se, sizeof se, "%2lld-%-2lld", get(b, "start").i, get(b, "end").i);
        out.push_back("  " + std::string(se) + " min  " + ljust(get(b, "title").s, 18) + " " + get(b, "plan").s);
    }
    out.push_back("Before: coach.py drill --investor " + get(ag, "investor").s + ", answered out loud. After: log it in /seed-closer and follow up within 24 hours.");
    return out;
}

inline int cmd_agenda(const Val& a) {
    const std::string sd = truthy(get(a, "dir")) ? S(a, "dir") : "seed";
    const BigInt mb = int_of(get(a, "minutes"));
    const long minutes = mb.fits_ll() && cmp(mb, BigInt(1000)) < 0 && cmp(mb, BigInt(-1000)) > 0 ? static_cast<long>(mb.to_ll()) : 0;
    const Val ag = build_agenda(sd, S(a, "investor"), minutes, S(a, "customers"), S(a, "provider"));
    const std::vector<std::string> lines = agenda_lines(ag);
    if (truthy(get(a, "json"))) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(ag, o)); }
    else for (const std::string& l : lines) print(l);
    if (truthy(get(a, "write"))) {
        const std::string path = join_path(join_path(sd, "coach"), "agenda-" + get(ag, "investor").s + ".md");
        std::vector<std::string> rest; for (std::size_t i = 1; i < lines.size(); ++i) rest.push_back("- " + strip(lines[i]));
        write_file(path, "# " + lines[0] + "\n\n" + join(rest, "\n") + "\n");
        if (!truthy(get(a, "json"))) print("Wrote " + path + ".");
    }
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("coach.py", "Pitch Coach (seed-coach): hard questions, graded answers, the call plan.");
    p.subcommands("cmd", false);
    auto common = [](Parser& s) { s.opt({"--dir"}).help("the seed folder (default ./seed)"); s.opt({"--json"}).store_true(); };
    const std::string bank = bank_default();
    {
        Parser& s = p.sub("drill"); s.help_line = "the hard questions, tailored";
        s.opt({"--n"}).integer().def(10); s.opt({"--category"});
        s.opt({"--seed"}).integer().help("shuffle within each category, repeatably");
        s.opt({"--investor"}).help("an id or name in seed/investors.json");
        s.opt({"--customers"}).help("your customers in one word, e.g. \"owners\" (default \"users\")");
        s.opt({"--provider"}).help("the supply side, e.g. \"walker\" (default: \"costs\")");
        s.opt({"--bank"}).def(Val::str(bank)).hidden();
        s.opt({"--write"}).store_true().help("save seed/coach/drill.md");
        common(s);
    }
    {
        Parser& s = p.sub("grade"); s.help_line = "grade a written answer";
        s.opt({"--answer"}); s.opt({"--file"}); s.opt({"--question"}).help("a question id from the bank");
        s.opt({"--bank"}).def(Val::str(bank)).hidden();
        common(s);
    }
    {
        Parser& s = p.sub("agenda"); s.help_line = "a first-call plan for a booked investor";
        s.opt({"--investor"}).required(); s.opt({"--minutes"}).integer().def(20); s.opt({"--customers"}); s.opt({"--provider"});
        s.opt({"--write"}).store_true().help("save seed/coach/agenda-<id>.md");
        common(s);
    }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "drill") return cmd_drill(a);
        if (cmd == "grade") return cmd_grade(a);
        return cmd_agenda(a);
    } catch (const CoachError& e) { eprint(std::string("coach.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-coach/coach", main);
}  // namespace packtools::seed_coach
