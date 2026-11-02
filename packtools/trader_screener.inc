// trader-screener/screen - stock screener: checks every company in a metrics file against your rules, says exactly
// why each passes or fails, and ranks the ones that pass. No network. A screen is a filter, not a recommendation.
// C++ port of trader-skill/trader-screener/screen.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 when at least one company passes, 1 when none do, 2 on bad input.
namespace packtools::trader_screener {
using namespace pyrt;
struct ScreenError : std::runtime_error { using std::runtime_error::runtime_error; };
static const std::vector<std::string> OPS = {">", ">=", "<", "<=", "==", "!=", "between", "in", "not in"};

inline Val to_number(const Val& v) {
    if (v.t == Val::BOOL || v.t == Val::NONE || v.is_num()) return v;
    const std::string s = strip(pystr(v));
    const std::string l = lower(s);
    if (s.empty() || l == "na" || l == "n/a" || l == "none" || l == "null" || l == "-") return Val();
    const std::string t = replace_all(replace_all(replace_all(s, ",", ""), "$", ""), "%", "");
    if (auto f = parse_float(t)) return Val::flt(*f);
    return Val::str(s);
}
inline Val dict_copy(const Val& r) {   // dict(r)
    if (r.t == Val::DICT) { Val d = Val::dict(); d.d->items = r.d->items; return d; }
    if (r.t == Val::STR || r.t == Val::LIST) throw PyErr("ValueError", "dictionary update sequence element #0 has length 1; 2 is required");
    throw PyErr("TypeError", std::string("'") + type_name(r) + "' object is not iterable");
}
inline std::vector<Val> load_metrics(const std::string& path) {
    if (!is_file(path)) throw ScreenError("no such file: " + path);
    std::vector<Val> rows;
    if (ends(lower(path), ".csv")) {
        const CsvDict c = csv_dicts(read_text(path, true, true));
        for (const auto& r : c.rows) { Val d = Val::dict(); for (const auto& [k, v] : r) d.d->set(k ? strip(*k) : "", to_number(v)); rows.push_back(d); }
    } else {
        Val data;
        try { data = loads(read_text(path)); } catch (const JSONDecodeError& e) { throw ScreenError(path + " is not valid JSON: " + e.what()); }
        Val list = data;
        if (data.t == Val::DICT) { const Val alt = get(data, "rows"); list = has(data, "companies") ? get(data, "companies") : alt; }
        if (list.t != Val::LIST) throw ScreenError(path + ": expected a list of companies or {\"companies\": [...]}");
        for (const auto& r : *list.l) rows.push_back(dict_copy(r));
    }
    std::vector<Val> out; std::size_t i = 0;
    for (auto& r : rows) {
        ++i;
        Val t = get(r, "ticker"); if (!truthy(t)) t = get(r, "symbol"); if (!truthy(t)) t = Val::str("");
        const std::string tk = upper(strip(pystr(t)));
        if (tk.empty()) throw ScreenError("company #" + std::to_string(i) + " has no ticker");
        r.d->set("ticker", Val::str(tk)); out.push_back(r);
    }
    return out;
}
inline Val add_prices(std::vector<Val>& rows, const std::string& prices_path) {
    const Val data = loads(read_text(prices_path));   // a JSON error here is not caught, as in the original
    if (data.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(data) + "' object has no attribute 'get'");
    Val src = has(data, "prices") ? get(data, "prices") : data;
    if (!truthy(src)) src = Val::dict();
    if (src.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(src) + "' object has no attribute 'items'");
    Dict prices; for (const auto& [k, v] : src.d->items) prices.set(upper(k), v);
    for (auto& r : rows) {
        const Val p = to_number(prices.at(get(r, "ticker").s));
        if (!p.is_real() || p.num() <= 0) continue;
        r.d->set("price", p);
        const Val eps = to_number(get(r, "eps_ttm"));
        if (eps.is_real()) {
            r.d->set("earnings_yield", Val::flt(round_n(100.0 * eps.num() / p.num(), 2)));
            if (eps.num() > 0) r.d->set("pe", Val::flt(round_n(p.num() / eps.num(), 1)));
        }
    }
    return get(data, "as_of");
}
inline bool contains(const Val& c, const std::string& key) {   // `key in c`
    if (c.t == Val::DICT) return c.d->has(key);
    if (c.t == Val::STR) return c.s.find(key) != std::string::npos;
    if (c.t == Val::LIST) { for (const auto& x : *c.l) if (x.t == Val::STR && x.s == key) return true; return false; }
    throw PyErr("TypeError", std::string("argument of type '") + type_name(c) + "' is not iterable");
}
inline Val item(const Val& c, const std::string& key) {   // c[key]
    if (c.t == Val::DICT) { if (const Val* p = c.d->get(key)) return *p; throw PyErr("KeyError", str_repr(key)); }
    if (c.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
    if (c.t == Val::STR) throw PyErr("TypeError", "string indices must be integers, not 'str'");
    throw PyErr("TypeError", std::string("'") + type_name(c) + "' object is not subscriptable");
}
inline bool in_strs(const Val& v, std::initializer_list<const char*> opts) { if (v.t != Val::STR) return false; for (const char* o : opts) if (v.s == o) return true; return false; }
inline bool in_ops(const Val& v) { return v.t == Val::STR && std::find(OPS.begin(), OPS.end(), v.s) != OPS.end(); }
inline std::vector<Val> iterate(const Val& v) {   // for x in v
    std::vector<Val> o;
    if (v.t == Val::LIST) return *v.l;
    if (v.t == Val::DICT) { for (const auto& [k, x] : v.d->items) o.push_back(Val::str(k)); return o; }
    if (v.t == Val::STR) { for (const auto& c : cps(v.s)) o.push_back(Val::str(c)); return o; }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
}
inline void check_rules(const Val& spec) {
    if (spec.t != Val::DICT || get(spec, "rules", Val::list()).t != Val::LIST) throw ScreenError("rules must be {\"rules\": [...]}");
    std::size_t i = 0;
    for (const auto& r : *get(spec, "rules", Val::list()).l) {
        ++i; const std::string n = std::to_string(i);
        if (!contains(r, "metric") || !contains(r, "op") || !contains(r, "value")) throw ScreenError("rule #" + n + " needs metric, op and value: " + repr(r));
        const Val op = item(r, "op");
        if (!in_ops(op)) throw ScreenError("rule #" + n + ": unknown op " + repr(op) + " (use one of " + join(OPS, ", ") + ")");
        if (op.s == "between" && !(item(r, "value").t == Val::LIST && item(r, "value").size() == 2)) throw ScreenError("rule #" + n + ": between needs [low, high]");
        if ((op.s == "in" || op.s == "not in") && item(r, "value").t != Val::LIST) throw ScreenError("rule #" + n + ": " + op.s + " needs a list");
    }
    i = 0;
    for (const auto& k : iterate(get(spec, "rank", Val::list()))) {
        ++i;
        bool bad = !contains(k, "metric");
        if (!bad) { if (k.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(k) + "' object has no attribute 'get'"); bad = !in_strs(get(k, "order", Val::str("desc")), {"asc", "desc"}); }
        if (bad) throw ScreenError("rank #" + std::to_string(i) + " needs a metric and order asc or desc");
    }
    if (!in_strs(get(spec, "missing", Val::str("fail")), {"fail", "skip"})) throw ScreenError("missing must be \"fail\" or \"skip\"");
}
inline Val load_rules(const std::string& path) {
    const std::string text = read_text(path);
    Val spec;
    try { spec = loads(text); } catch (const JSONDecodeError& e) { throw ScreenError(path + " is not valid JSON: " + e.what()); }
    check_rules(spec);
    return spec;
}
inline std::string fmt(const Val& v) {   // _fmt
    if (v.t == Val::FLOAT) { std::string s = fmt_f(v.f, 2); while (!s.empty() && s.back() == '0') s.pop_back(); while (!s.empty() && s.back() == '.') s.pop_back(); return s; }
    if (v.t == Val::LIST) { std::vector<std::string> p; for (const auto& x : *v.l) p.push_back(fmt(x)); return "[" + join(p, ", ") + "]"; }
    return pystr(v);
}
[[noreturn]] inline void type_err(const char* op, const Val& a, const Val& b) { throw PyErr("TypeError", std::string("'") + op + "' not supported between instances of '" + type_name(a) + "' and '" + type_name(b) + "'"); }
inline bool cmp(const char* op, const Val& a, const Val& b) {   // a <op> b for real numbers (raises like Python otherwise)
    if (!a.is_real() || !b.is_real()) type_err(op, a, b);
    const double x = a.num(), y = b.num(); const std::string o = op;
    return o == ">" ? x > y : o == ">=" ? x >= y : o == "<" ? x < y : x <= y;
}
inline bool eq(const Val& a, const Val& b) {   // a == b
    if (a.is_real() && b.is_real()) return a.num() == b.num();
    if (a.t != b.t) return false;
    if (a.t == Val::STR) return a.s == b.s; if (a.t == Val::NONE) return true;
    return repr(a) == repr(b);
}
struct Check { std::optional<bool> ok; std::string why; };
inline Check check(const Val& company, const Val& rule) {
    const Val m = item(rule, "metric"), op = item(rule, "op"), want = item(rule, "value");
    const Val have = m.t == Val::STR ? get(company, m.s) : Val();
    const std::string ms = pystr(m), o = op.s;
    if (have.is_none() || (have.t == Val::STR && have.s.empty())) return {std::nullopt, ms + " is missing"};
    if (o == "in" || o == "not in") {
        std::vector<std::string> bag; for (const auto& x : *want.l) bag.push_back(upper(pystr(x)));
        const bool inside = std::find(bag.begin(), bag.end(), upper(pystr(have))) != bag.end();
        return {o == "in" ? inside : !inside, ms + " " + fmt(have) + " " + (inside ? "is in" : "is not in") + " " + fmt(want)};
    }
    if ((o == "==" || o == "!=") && !have.is_real()) return {(upper(pystr(have)) == upper(pystr(want))) == (o == "=="), ms + " " + fmt(have) + " " + o + " " + fmt(want)};
    if (!have.is_real()) return {false, ms + " is " + repr(have) + ", not a number"};
    if (o == "between") {
        const Val lo = (*want.l)[0], hi = (*want.l)[1];
        const bool ok = cmp("<=", lo, have) && cmp("<=", have, hi);
        return {ok, ms + " " + fmt(have) + " " + (ok ? "is within" : "is outside") + " " + fmt(lo) + " to " + fmt(hi)};
    }
    // the original builds all six comparisons before picking one, so a non-number threshold always raises
    const bool gt = cmp(">", have, want), ge = cmp(">=", have, want), lt = cmp("<", have, want), le = cmp("<=", have, want), e = eq(have, want);
    const bool ok = o == ">" ? gt : o == ">=" ? ge : o == "<" ? lt : o == "<=" ? le : o == "==" ? e : !e;
    if (ok) return {true, ms + " " + fmt(have) + " " + o + " " + fmt(want)};
    return {false, ms + " " + fmt(have) + ", needs " + o + " " + fmt(want)};
}
struct Res { std::string ticker; Val name; bool passed = false; std::vector<std::string> fails, passes, skipped; Val metrics; Val score; std::vector<std::pair<std::string, double>> parts; std::vector<std::string> gaps; bool has_gaps = false; };
inline void rank(std::vector<Res>& results, const Val& keys_v) {
    std::vector<Res*> winners; for (auto& r : results) if (r.passed) winners.push_back(&r);
    for (auto* r : winners) { r->score = Val::flt(0.0); r->parts.clear(); }
    const std::vector<Val> keys = iterate(keys_v);
    if (keys.empty() || winners.empty()) { for (auto* r : winners) r->score = Val(); return; }
    for (const auto& k : keys) {
        const Val mv = item(k, "metric"); const std::string m = pystr(mv);
        const bool desc = in_strs(get(k, "order", Val::str("desc")), {"desc"});
        std::vector<std::pair<Res*, double>> vals;
        for (auto* r : winners) { const Val v = mv.t == Val::STR ? get(r->metrics, mv.s) : Val(); if (v.is_num()) vals.emplace_back(r, v.num()); }
        if (vals.empty()) continue;
        std::vector<double> ordered; for (auto& [r, v] : vals) ordered.push_back(v);
        std::stable_sort(ordered.begin(), ordered.end());
        const std::size_t n = ordered.size();
        for (auto& [r, v] : vals) {
            long below = 0, equal = 0; for (double x : ordered) { if (x < v) below++; if (x == v) equal++; }
            const double pos = n > 1 ? (static_cast<double>(below) + static_cast<double>(equal - 1) / 2.0) / static_cast<double>(n - 1) : 1.0;
            const double val = round_n(desc ? pos : 1.0 - pos, 3);
            bool found = false; for (auto& pr : r->parts) if (pr.first == m) { pr.second = val; found = true; } if (!found) r->parts.emplace_back(m, val);
        }
    }
    std::vector<std::pair<std::string, double>> weights;
    for (const auto& k : keys) {
        const std::string m = pystr(item(k, "metric")); const double w = to_float(get(k, "weight", Val::integer(1)));
        bool found = false; for (auto& pr : weights) if (pr.first == m) { pr.second = w; found = true; } if (!found) weights.emplace_back(m, w);
    }
    auto weight = [&](const std::string& m) { for (auto& pr : weights) if (pr.first == m) return pr.second; return 0.0; };
    for (auto* r : winners) {
        std::vector<double> ws, prods; for (auto& [m, v] : r->parts) { ws.push_back(weight(m)); prods.push_back(v * weight(m)); }
        const double total = fsum(ws);
        r->score = total != 0 ? Val::flt(round_n(fsum(prods) / total, 3)) : Val();
        r->gaps.clear(); r->has_gaps = true;
        for (const auto& k : keys) { const std::string m = pystr(item(k, "metric")); bool in = false; for (auto& pr : r->parts) if (pr.first == m) in = true; if (!in) r->gaps.push_back(m); }
    }
}
inline std::vector<Res> screen(const std::vector<Val>& companies, const Val& spec) {
    const Val rules = get(spec, "rules", Val::list()); const Val missing = get(spec, "missing", Val::str("fail"));
    std::vector<Res> results;
    for (const auto& c : companies) {
        Res r; r.ticker = get(c, "ticker").s; r.name = get(c, "name"); r.metrics = c;
        for (const auto& rule : *rules.l) {
            const Check ck = check(c, rule);
            if (!ck.ok) (missing.s == "fail" ? r.fails : r.skipped).push_back(ck.why);
            else if (*ck.ok) r.passes.push_back(ck.why); else r.fails.push_back(ck.why);
        }
        r.passed = r.fails.empty(); results.push_back(std::move(r));
    }
    rank(results, get(spec, "rank", Val::list()));
    auto sc = [](const Res& r) { return truthy(r.score) ? -r.score.num() : -0.0; };
    std::stable_sort(results.begin(), results.end(), [&](const Res& a, const Res& b) {
        if (a.passed != b.passed) return a.passed;
        const double x = sc(a), y = sc(b); if (x != y) return x < y;
        if (a.fails.size() != b.fails.size()) return a.fails.size() < b.fails.size();
        return a.ticker < b.ticker;
    });
    return results;
}
// list[:top] with Python slice rules (top is an int or bool; anything else raises)
inline std::size_t slice_end(std::size_t n, const Val& top) {
    if (top.t != Val::INT && top.t != Val::BOOL) throw PyErr("TypeError", "slice indices must be integers or None or have an __index__ method");
    const long long t = top.t == Val::BOOL ? (top.b ? 1 : 0) : top.i; const long long len = static_cast<long long>(n);
    const long long e = t < 0 ? std::max(0LL, len + t) : std::min(len, t); return static_cast<std::size_t>(e);
}
inline std::string render(const std::vector<Res>& results, const Val& spec, const Val& top, const Val& prices_as_of) {
    Val nm = get(spec, "name"); const std::string name = truthy(nm) ? pystr(nm) : "screen";
    std::vector<const Res*> passed, failed; for (const auto& r : results) (r.passed ? passed : failed).push_back(&r);
    std::vector<std::string> out{name + ": " + std::to_string(passed.size()) + " of " + std::to_string(results.size()) + " companies pass."};
    if (truthy(prices_as_of)) out.push_back("Prices as of " + pystr(prices_as_of) + ".");
    out.emplace_back();
    const std::size_t shown = truthy(top) ? slice_end(passed.size(), top) : passed.size();
    for (std::size_t i = 0; i < shown; ++i) {
        const Res& r = *passed[i];
        const std::string score = r.score.is_none() ? "" : "  score " + fmt_f(r.score.num(), 2);
        std::string nmv;
        if (!truthy(r.name)) nmv = ""; else if (r.name.t == Val::STR) nmv = cpslice(r.name.s, 0, 32);
        else if (r.name.t == Val::LIST) { Val l = Val::list(List(r.name.l->begin(), r.name.l->begin() + static_cast<long>(std::min<std::size_t>(32, r.name.l->size())))); nmv = repr(l); }
        else throw PyErr("TypeError", std::string("'") + type_name(r.name) + "' object is not subscriptable");
        out.push_back(rjust(std::to_string(i + 1), 2) + ". PASS " + ljust(r.ticker, 6) + " " + nmv + score);
        for (const auto& w : r.passes) out.push_back("        ok    " + w);
        for (const auto& w : r.skipped) out.push_back("        skip  " + w);
        if (r.has_gaps && !r.gaps.empty()) out.push_back("        not ranked on: " + join(r.gaps, ", "));
    }
    if (truthy(top)) { const long long t = top.t == Val::BOOL ? 1 : top.i; if (static_cast<long long>(passed.size()) > t) out.push_back("    ... " + std::to_string(static_cast<long long>(passed.size()) - t) + " more pass"); }
    if (!failed.empty()) { out.emplace_back(); for (const auto* r : failed) out.push_back("    FAIL " + ljust(r->ticker, 6) + " " + join(r->fails, "; ")); }
    out.emplace_back();
    out.push_back("A screen is a filter on reported numbers, not a recommendation. Read the filings (/trader-analyst) before anything goes near /trader-budget.");
    return join(out, "\n") + "\n";
}
inline Val strs(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(Val::str(s)); return l; }
inline int main(const std::vector<std::string>& argv) {
    Parser p("screen", "Stock screener for trader-screener. Standard library only, no network.");
    p.pos("metrics").help("metrics JSON or CSV (trader/metrics.json)");
    p.opt({"--rules"}).def(Val::str(skill_file("rules.example.json")));
    p.opt({"--prices"}).help("trader/prices.json, adds price, pe and earnings_yield");
    p.opt({"--top"}).integer().help("show only the N best that pass");
    p.opt({"--out"}).help("write the shortlist JSON here (trader/shortlist.json)");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    Val spec, prices_as_of; std::vector<Val> companies;
    try {
        spec = load_rules(S(args, "rules"));
        companies = load_metrics(S(args, "metrics"));
        if (truthy(get(args, "prices"))) prices_as_of = add_prices(companies, S(args, "prices"));
    } catch (const ScreenError& e) { eprint(std::string("screen: ") + e.what()); return 2; }
    catch (const OSError& e) { eprint(std::string("screen: ") + e.what()); return 2; }
    const std::vector<Res> results = screen(companies, spec);
    const Val top = truthy(get(args, "top")) ? get(args, "top") : get(spec, "top");
    std::vector<const Res*> passed; for (const auto& r : results) if (r.passed) passed.push_back(&r);
    Val sl = Val::dict();
    sl.d->set("name", get(spec, "name")); sl.d->set("rules", get(spec, "rules", Val::list())); sl.d->set("rank", get(spec, "rank", Val::list()));
    sl.d->set("prices_as_of", prices_as_of);
    Val pl = Val::list(); const std::size_t upto = truthy(top) ? slice_end(passed.size(), top) : passed.size();
    for (std::size_t k = 0; k < upto; ++k) { const Res& r = *passed[k]; Val e = Val::dict(); e.d->set("ticker", Val::str(r.ticker)); e.d->set("name", r.name); e.d->set("score", r.score); e.d->set("why", strs(r.passes)); e.d->set("skipped", strs(r.skipped)); pl.l->push_back(e); }
    sl.d->set("passed", pl);
    Val fl = Val::list(); for (const auto& r : results) if (!r.passed) { Val e = Val::dict(); e.d->set("ticker", Val::str(r.ticker)); e.d->set("why", strs(r.fails)); fl.l->push_back(e); }
    sl.d->set("failed", fl);
    sl.d->set("note", Val::str("A filter on reported numbers, not a recommendation."));
    DumpOpts o; o.indent = 2;
    if (B(args, "json")) print(dumps(sl, o)); else out(render(results, spec, top, prices_as_of));
    if (truthy(get(args, "out"))) {
        const std::string path = S(args, "out"); const std::string folder = dirname(path);
        if (!folder.empty()) makedirs(folder);
        write_text(path, dumps(sl, o) + "\n");
        if (!B(args, "json")) print("wrote " + path);
    }
    return passed.empty() ? 1 : 0;
}
PACKTOOL("trader-screener/screen", main);
}  // namespace packtools::trader_screener
