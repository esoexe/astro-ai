// seed-market/market: TAM, SAM and SOM with a source on every input. Exact arithmetic (Decimal), whole dollars, half
// up. Writes seed/market.json only with --write. A C++ port of seed-skill/seed-market/market.py (MIT, Jake
// Schincariol); same output and exit codes: 0 done, 1 refused, 2 bad input.
#include <set>
namespace packtools::seed_market {
using namespace pyrt;
using namespace packtools::seed;

struct MarketError : std::runtime_error {
    int code;
    explicit MarketError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {}
};

inline const char* NOTE = "Estimates from your inputs, not a forecast. Not investment advice.";
inline bool vague(const std::string& low) {
    // upstream's list, plus "astro" / "astro ai": the assistant is no more a source than "chatgpt" or "claude"
    static const std::set<std::string> V = {"", "?", "tbd", "todo", "n/a", "na", "none", "source", "google", "internet", "the internet", "web",
                                            "online", "chatgpt", "ai", "claude", "astro", "astro ai", "common knowledge", "estimate", "guess", "assumption", "me"};
    return V.count(low) > 0;
}
inline Decimal dec_text(const std::string& t) { return Decimal::parse(ascii_digits(t)); }

// ---------------------------------------------------------------- numbers
inline Decimal num(const Val& text, const std::string& what) {   // 71M, 400K, 1.5B, 2,400, $25 -> Decimal
    if (text.t == Val::BOOL || text.is_none()) throw MarketError(what + " is not a number: " + repr(text));
    if (text.t == Val::INT) return Decimal::from_int(big_of(text));
    if (text.t == Val::FLOAT) return dec_text(float_repr(text.f));   // Decimal(repr(x))
    std::string t = lower(strip(pystr(text)));
    std::string u; for (char c : t) if (c != ',' && c != '$' && c != ' ') u += c;
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)(k|m|mm|b|bn)?$)", u);
    if (!m || (!u.empty() && u.back() == '\n')) throw MarketError(what + " is not a number like 71M, 400K or 2400: " + repr(text));
    const std::string suf = m.has(2) ? m.group(2) : "";
    const long long mult = suf.empty() ? 1 : suf == "k" ? 1000 : (suf == "m" || suf == "mm") ? 1000000 : 1000000000;
    const Decimal v = dec_text(m.group(1)) * Decimal(mult);
    if (v <= Decimal(0)) throw MarketError(what + " must be above 0");
    return v;
}
inline Decimal num(const std::string& text, const std::string& what) { return num(Val::str(text), what); }

inline Decimal share(const std::string& text, const std::string& what = "--share") {   // 10% or 0.10 -> a fraction; a bare number above 1 is a percent
    const std::string t = strip(text);
    const std::string bad = what + " wants a percent like 10% or a fraction like 0.1, not " + str_repr(text);
    Decimal v;
    try {
        if (!t.empty() && t.back() == '%') {
            v = dec_text(strip(t.substr(0, t.size() - 1))) / Decimal(100);
        } else {
            v = dec_text(t);
            if (v.is_nan()) throw MarketError(bad);   // Decimal raises InvalidOperation comparing a NaN
            if (v > Decimal(1)) v = v / Decimal(100);
        }
    } catch (const DecimalError&) { throw MarketError(bad); }
    if (v.is_nan()) throw MarketError(bad);
    if (v <= Decimal(0) || v > Decimal(1)) throw MarketError(what + " must be above 0% and at most 100%, not " + str_repr(text));
    return v;
}

inline BigInt whole(const Decimal& x) { return x.quantize(Decimal(1), Rounding::HALF_UP).to_int(); }

inline std::string fmt_num(const Decimal& d) {
    if (d == d.to_integral_value()) return commas(d.to_int());
    return rstrip_chars(rstrip_chars(float_commas(d.to_double()), "0"), ".");
}
inline std::string fmt_num(const Val& x) { return fmt_num(dec_of(x)); }

inline std::string pct_text(const Decimal& frac) {
    const std::string s = (frac * Decimal(100)).normalize().format("f");
    return (s.find('.') != std::string::npos ? rstrip_chars(rstrip_chars(s, "0"), ".") : s) + "%";
}

inline std::string sign_of(const std::string& cur) { return cur == "USD" ? "$" : cur + " "; }
inline std::string big(const BigInt& n, const std::string& cur = "USD") {   // 17040000000 -> $17.04B
    static const std::pair<long long, const char*> DIVS[] = {{1000000000000LL, "T"}, {1000000000LL, "B"}, {1000000LL, "M"}, {1000LL, "K"}};
    for (const auto& [div, suf] : DIVS) {
        if (cmp(n, BigInt(div)) >= 0) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%.2f", n.to_double() / static_cast<double>(div));
            return sign_of(cur) + rstrip_chars(rstrip_chars(buf, "0"), ".") + suf;
        }
    }
    return sign_of(cur) + commas(n);
}
inline std::string big(const Val& n, const std::string& cur = "USD") { return big(int_of(n), cur); }
inline std::string big(const Decimal& n, const std::string& cur = "USD") { return big(n.to_int(), cur); }
inline std::string money_text(const BigInt& n, const std::string& cur = "USD") { return sign_of(cur) + commas(n); }
inline std::string money_text(const Val& n, const std::string& cur = "USD") { return money_text(int_of(n), cur); }

inline std::string show_value(const Val& x, const std::string& cur = "USD") {   // an input's value with its unit: 10%, $2,400, 400,000
    const Val uv = get(x, "unit");
    const std::string u = truthy(uv) ? pystr(uv) : "";
    const std::string v = fmt_num(get(x, "value"));
    if (starts(u, "%")) return v + u;
    if (u == cur || starts(u, cur + " ")) return sign_of(cur) + v + u.substr(cur.size());
    if (u == "count" || u.empty()) return v;
    return v + " " + u;
}

inline std::pair<std::string, std::string> source_kind(const Val& src, const std::string& what) {
    const std::string s = squash(truthy(src) ? pystr(src) : "");
    const std::string low = strip_chars(lower(s), " .");
    if (vague(low))
        throw MarketError(what + " needs a real source: a URL, a named publication, or \"assumption: <why>\" (got " + repr(truthy(src) ? src : Val::str("")) + ")", 1);
    if (starts(low, "assumption")) {
        const std::string why = re::sub(R"(^assumption\s*[:,-]?\s*)", "", s, 0, re::I);
        if (nwords(why) < 3) throw MarketError(what + ": an assumption needs its reason in a few words, like \"assumption: 1 in 10 dog homes pays for walks\"", 1);
        return {s, "assumption"};
    }
    if (re::search(R"(https?://)", s)) return {s, "url"};
    if (nwords(s) < 2) throw MarketError(what + ": " + str_repr(s) + " is too vague; name the publication or paste the URL", 1);
    return {s, "publication"};
}

inline Val inp(const std::string& name, const Decimal& value, const std::string& unit, const Val& src, const std::string& level, const std::string& what) {
    const auto [s, kind] = source_kind(src, what);
    return obj({{"name", sv(name)}, {"value", num_val(value)}, {"unit", sv(unit)}, {"source", sv(s)}, {"kind", sv(kind)}, {"level", sv(level)}});
}

// ---------------------------------------------------------------- the document
inline long long today_of(const Val& a) {   // datetime.strptime(text, "%Y-%m-%d"): one-digit months and days are fine
    const Val tv = get(a, "today");
    if (!truthy(tv)) return today_days();
    const std::string text = pystr(tv);
    const std::string bad = "--today wants YYYY-MM-DD, not " + str_repr(text);
    const re::Match m = re::match(R"(^(\d\d\d\d)-(1[0-2]|0[1-9]|[1-9])-(3[01]|[12]\d|0[1-9]|[1-9]| [1-9])$)", text, re::I);
    if (!m || text.back() == '\n') throw MarketError(bad);
    const long long y = to_ll(m.group(1));
    const int mo = static_cast<int>(to_ll(m.group(2))), d = static_cast<int>(to_ll(strip(m.group(3))));
    if (!valid_date(y, mo, d)) throw MarketError(bad);
    return make_day(y, mo, d);
}

inline std::string path_of(const Val& a) { const std::string d = S(a, "dir"); return join_path(d.empty() ? "seed" : d, "market.json"); }

inline Val load(const std::string& path) {
    if (!is_file(path)) {
        return obj({{"created_by", sv("seed-market")}, {"generated", Val()}, {"method", Val()}, {"tam", Val()}, {"sam", Val()},
                    {"som", Val()}, {"currency", sv("USD")}, {"inputs", Val::list()}, {"notes", Val::list()}});
    }
    Val doc;
    try {
        doc = loads(read_text(path));
    } catch (const OSError&) {
        throw;
    } catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw MarketError(path + " is not valid JSON: " + e.what());
        throw;
    }
    if (doc.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(doc) + "' object has no attribute 'setdefault'");
    for (const char* k : {"inputs", "notes"}) if (!doc.d->has(k)) doc.d->set(k, Val::list());
    if (!doc.d->has("currency")) doc.d->set("currency", sv("USD"));
    return doc;
}

inline void save(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path, dumps(doc, o) + "\n");
}

inline std::string cur_of(const Val& doc) { const Val c = get(doc, "currency"); return doc.d->has("currency") ? pystr(c) : "USD"; }
inline Val sub_dict(Val& doc, const char* k) {   // doc.setdefault(k, {}), as a handle that edits doc
    if (!has(doc, k) || get(doc, k).t != Val::DICT) doc.d->set(k, Val::dict());
    return get(doc, k);
}

inline std::vector<std::string> order_problems(const Val& doc) {
    std::vector<std::string> out;
    const Val t = get(doc, "tam"), s = get(doc, "sam"), o = get(doc, "som");
    const std::string cur = cur_of(doc);
    if (!t.is_none() && !s.is_none() && num_cmp(s, t) > 0)
        out.push_back("SAM " + big(s, cur) + " is bigger than TAM " + big(t, cur) + ": the market you can serve cannot beat the whole market");
    if (!s.is_none() && !o.is_none() && num_cmp(o, s) > 0) out.push_back("SOM " + big(o, cur) + " is bigger than SAM " + big(s, cur));
    if (!t.is_none() && !o.is_none() && num_cmp(o, t) > 0) out.push_back("SOM " + big(o, cur) + " is bigger than TAM " + big(t, cur));
    return out;
}

inline Val method_of(const Val& doc) {
    std::vector<std::string> parts;
    for (const char* lv : {"tam", "sam"}) {
        const Val methods = get(doc, "methods");
        const Val m = truthy(methods) ? get(methods, lv) : Val();
        if (truthy(m)) parts.push_back(pystr(m) + " " + upper(lv));
    }
    if (!get(doc, "som").is_none() && !get(doc, "som_share_pct").is_none())
        parts.push_back("SOM " + pct_text(dec_text(pystr(get(doc, "som_share_pct"))) / Decimal(100)) + " of SAM in " + int_of(get(doc, "som_years")).str() + " years");
    if (parts.empty()) return Val();
    return sv(join(parts, ", "));
}

inline std::vector<std::string> set_level(Val& doc, const std::string& level, const BigInt& value, const std::string& method, const std::string& formula,
                                          const std::vector<Val>& inputs, long long today) {
    Val keep = Val::list();
    const Val old = get(doc, "inputs");
    if (old.t == Val::LIST) for (const Val& x : *old.l) if (!(get(x, "level").t == Val::STR && get(x, "level").s == level)) keep.l->push_back(x);
    for (const Val& x : inputs) keep.l->push_back(x);
    doc.d->set("inputs", keep);
    doc.d->set(level, int_val(value));
    sub_dict(doc, "methods").d->set(level, sv(method));
    sub_dict(doc, "formulas").d->set(level, sv(formula));
    std::vector<std::string> notes;
    if (level == "sam" && !get(doc, "som_share_pct").is_none()) {
        const std::string cur = cur_of(doc);
        const Decimal frac = dec_text(pystr(get(doc, "som_share_pct"))) / Decimal(100);
        const BigInt som = whole(Decimal::from_int(value) * frac);
        doc.d->set("som", int_val(som));
        get(doc, "formulas").d->set("som", sv(pct_text(frac) + " of SAM " + money_text(value, cur) + " = " + money_text(som, cur)));
        notes.push_back("SOM recomputed from the new SAM: " + big(som, cur));
    }
    doc.d->set("method", method_of(doc));
    doc.d->set("generated", sv(iso_day(today)));
    doc.d->set("created_by", sv("seed-market"));
    return notes;
}

inline int finish(const Val& a, const Val& doc, const std::vector<std::string>& lines, const std::vector<std::string>& extra = {}) {
    const std::string path = path_of(a);
    const std::vector<std::string> bad = order_problems(doc);
    const bool write = truthy(get(a, "write"));
    if (truthy(get(a, "json"))) {
        DumpOpts o; o.indent = 2; o.ensure_ascii = false;
        print(dumps(obj({{"ok", bv(bad.empty())}, {"problems", strlist(bad)}, {"notes", strlist(extra)}, {"market", doc}}), o));
        if (bad.empty() && write) save(path, doc);
        return bad.empty() ? 0 : 1;
    }
    for (const std::string& l : lines) print(l);
    for (const std::string& n : extra) print("Note: " + n);
    std::vector<std::string> names;
    for (const Val& x : *get(doc, "inputs").l) {
        const Val k = get(x, "kind"), lv = get(x, "level");
        if (k.t == Val::STR && k.s == "assumption" && !(lv.t == Val::STR && lv.s == "context")) names.push_back(pystr(get(x, "name")));
    }
    if (!names.empty()) print("Assumptions in it (say so when you quote it): " + join(names, "; ") + ".");
    if (!bad.empty()) {
        for (const std::string& b : bad) eprint("Refused: " + b);
        eprint("Not saved. Fix the inputs: SOM <= SAM <= TAM.");
        return 1;
    }
    if (write) { save(path, doc); print("Wrote " + path + "."); }
    else print("Not saved: add --write.");
    print(NOTE);
    return 0;
}

inline std::vector<std::string> input_lines(const std::vector<Val>& ins, const std::string& cur) {
    std::vector<std::string> out;
    for (const Val& x : ins) out.push_back("  - " + get(x, "name").s + " = " + show_value(x, cur) + ": " + get(x, "source").s);
    return out;
}

// ---------------------------------------------------------------- commands
inline int cmd_top_down(const Val& a) {
    Val doc = load(path_of(a));
    const std::string cur = truthy(get(a, "currency")) ? S(a, "currency") : cur_of(doc);
    doc.d->set("currency", sv(cur));
    const std::string level = truthy(get(a, "level")) ? S(a, "level") : "tam";
    const Decimal pop = num(S(a, "population"), "--population");
    const Decimal sh = share(S(a, "share"));
    const Decimal sp = num(S(a, "spend"), "--spend");
    const BigInt value = whole(pop * sh * sp);
    const std::string pname = truthy(get(a, "population_name")) ? S(a, "population_name") : "people";
    const std::string shname = truthy(get(a, "share_name")) ? S(a, "share_name") : "share of " + pname + " that would buy";
    const std::string sname = truthy(get(a, "spend_name")) ? S(a, "spend_name") : "spend a year each";
    const std::vector<Val> ins = {inp(pname, pop, "count", get(a, "population_source"), level, "--population-source"),
                                  inp(shname, sh * Decimal(100), "%", get(a, "share_source"), level, "--share-source"),
                                  inp(sname, sp, cur + " a year", get(a, "spend_source"), level, "--spend-source")};
    const std::string formula = fmt_num(pop) + " " + pname + " x " + pct_text(sh) + " x " +
                                (sp == sp.to_integral_value() ? money_text(sp.to_int(), cur) : "$" + fmt_num(sp)) + " = " + money_text(value, cur);
    const std::vector<std::string> notes = set_level(doc, level, value, "top-down", formula, ins, today_of(a));
    std::vector<std::string> lines = {upper(level) + " (top-down): " + big(value, cur) + " a year", "  " + formula};
    for (const std::string& l : input_lines(ins, cur)) lines.push_back(l);
    return finish(a, doc, lines, notes);
}

inline int cmd_bottom_up(const Val& a) {
    Val doc = load(path_of(a));
    const std::string cur = truthy(get(a, "currency")) ? S(a, "currency") : cur_of(doc);
    doc.d->set("currency", sv(cur));
    const std::string level = truthy(get(a, "level")) ? S(a, "level") : "sam";
    const Decimal cu = num(S(a, "customers"), "--customers");
    const Decimal fr = num(S(a, "frequency"), "--frequency");
    const Decimal pr = num(S(a, "price"), "--price");
    const BigInt value = whole(cu * fr * pr);
    const std::string cname = truthy(get(a, "customers_name")) ? S(a, "customers_name") : "customers you can reach";
    const std::string unit = truthy(get(a, "unit")) ? S(a, "unit") : "purchase";
    const std::vector<Val> ins = {inp(cname, cu, "count", get(a, "customers_source"), level, "--customers-source"),
                                  inp(unit + "s a year per customer", fr, "per year", get(a, "frequency_source"), level, "--frequency-source"),
                                  inp("price per " + unit, pr, cur, get(a, "price_source"), level, "--price-source")};
    const std::string formula = fmt_num(cu) + " " + cname + " x " + fmt_num(fr) + " " + unit + "s a year x " + sign_of(cur) + fmt_num(pr) + " = " + money_text(value, cur);
    const std::vector<std::string> notes = set_level(doc, level, value, "bottom-up", formula, ins, today_of(a));
    std::vector<std::string> lines = {upper(level) + " (bottom-up): " + big(value, cur) + " a year", "  " + formula};
    for (const std::string& l : input_lines(ins, cur)) lines.push_back(l);
    return finish(a, doc, lines, notes);
}

inline int cmd_som(const Val& a) {
    Val doc = load(path_of(a));
    const std::string cur = pystr(get(doc, "currency"));
    if (get(doc, "sam").is_none()) throw MarketError("SOM is a share of SAM: size SAM first (market.py bottom-up --write)", 1);
    const Decimal sh = share(S(a, "share"));
    const BigInt years = int_of(get(a, "years"));
    if (cmp(years, BigInt(1)) < 0 || cmp(years, BigInt(10)) > 0) throw MarketError("--years is 1 to 10");
    const std::string reason = squash(S(a, "reason"));
    if (nwords(reason) < 6) throw MarketError("--reason needs a real sentence on why you can win that share (who switches, why, how you reach them)", 1);
    const BigInt value = whole(dec_of(get(doc, "sam")) * sh);
    doc.d->set("som", int_val(value));
    doc.d->set("som_share_pct", Val::flt((sh * Decimal(100)).to_double()));
    doc.d->set("som_years", int_val(years));
    doc.d->set("som_reason", sv(reason));
    const std::string formula = pct_text(sh) + " of SAM " + money_text(get(doc, "sam"), cur) + " = " + money_text(value, cur);
    sub_dict(doc, "formulas").d->set("som", sv(formula));
    doc.d->set("method", method_of(doc));
    doc.d->set("generated", sv(iso_day(today_of(a))));
    std::vector<std::string> notes;
    if (sh > dec_text("0.1")) notes.push_back(pct_text(sh) + " of the SAM in " + years.str() + " years is a lot: be ready to defend it with a plan, customer by customer");
    const std::vector<std::string> lines = {"SOM: " + big(value, cur) + " a year within " + years.str() + " years (" + pct_text(sh) + " of SAM " + big(get(doc, "sam"), cur) + ")",
                                            "  " + formula, "  Why: " + reason};
    return finish(a, doc, lines, notes);
}

inline int cmd_cite(const Val& a) {
    Val doc = load(path_of(a));
    const Decimal v = num(S(a, "value"), "--value");
    const std::string cur = pystr(get(doc, "currency"));
    const Val x = inp(strip(S(a, "name")), v, truthy(get(a, "unit")) ? S(a, "unit") : cur, get(a, "source"), "context", "--source");
    Val keep = Val::list();
    for (const Val& i : *get(doc, "inputs").l) {
        const Val lv = get(i, "level");
        if (!(lv.t == Val::STR && lv.s == "context" && get(i, "name").t == Val::STR && get(i, "name").s == get(x, "name").s)) keep.l->push_back(i);
    }
    keep.l->push_back(x);
    doc.d->set("inputs", keep);
    doc.d->set("generated", sv(iso_day(today_of(a))));
    doc.d->set("created_by", sv("seed-market"));
    const std::string shown = get(x, "unit").s == cur ? big(v, cur) : show_value(x, cur);
    return finish(a, doc, {"Fact: " + get(x, "name").s + " = " + shown, "  " + get(x, "source").s,
                           "  (quoted by the plan and the deck; not part of the TAM, SAM, SOM math)"});
}

inline std::string ljust_cp(const std::string& s, std::size_t w) {   // "%-10s": pad by code points
    const std::size_t n = cps(s).size();
    return n >= w ? s : s + std::string(w - n, ' ');
}

inline std::vector<std::string> report(const Val& doc) {
    const std::string cur = cur_of(doc);
    const Val method = get(doc, "method");
    std::vector<std::string> lines = {"Market size (" + cur + " a year)" + (truthy(method) ? ", " + pystr(method) : "")};
    const Val formulas = get(doc, "formulas");
    for (const char* lv : {"tam", "sam", "som"}) {
        const Val v = get(doc, lv);
        const Val f = truthy(formulas) ? get(formulas, lv) : Val();
        lines.push_back("  " + upper(lv) + "  " + ljust_cp(v.is_none() ? "not sized" : big(v, cur), 10) + " " + (truthy(f) ? pystr(f) : ""));
    }
    if (truthy(get(doc, "som_reason"))) lines.push_back("  Why the SOM: " + pystr(get(doc, "som_reason")));
    const Val ins = get(doc, "inputs");
    if (ins.t == Val::LIST)
        for (const Val& x : *ins.l) {
            const Val lv = get(x, "level");
            if (!(lv.t == Val::STR && lv.s == "context")) continue;
            const Val u = get(x, "unit");
            lines.push_back("  Fact: " + pystr(get(x, "name")) + " = " + (u.t == Val::STR && u.s == cur ? big(get(x, "value"), cur) : show_value(x, cur)) +
                            " (" + pystr(get(x, "source")) + ")");
        }
    return lines;
}

inline std::vector<std::string> problems(const Val& doc) {
    std::vector<std::string> out = order_problems(doc);
    for (const char* lv : {"tam", "sam", "som"}) if (get(doc, lv).is_none()) out.push_back(upper(lv) + " is not sized yet");
    const Val ins = get(doc, "inputs");
    if (ins.t == Val::LIST)
        for (const Val& x : *ins.l) {
            try { source_kind(get(x, "source"), "input " + repr(get(x, "name"))); }
            catch (const MarketError& e) { out.push_back(e.what()); }
        }
    if (!get(doc, "som").is_none() && !truthy(get(doc, "som_reason"))) out.push_back("SOM has no stated reason");
    return out;
}

inline int cmd_show(const Val& a) {
    const std::string path = path_of(a);
    if (!is_file(path)) { print("No " + path + " yet. Start with market.py top-down ... --write"); return 1; }
    const Val doc = load(path);
    if (truthy(get(a, "json"))) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(doc, o)); return 0; }
    for (const std::string& l : report(doc)) print(l);
    const std::string cur = cur_of(doc);
    for (const Val& x : *get(doc, "inputs").l) {
        const Val k = get(x, "kind");
        if (!(k.t == Val::STR && k.s == "assumption")) continue;
        const std::string lv = pystr(get(x, "level"));
        print("  Assumption (" + (lv != "context" ? upper(lv) : std::string("fact")) + "): " + pystr(get(x, "name")) + " = " + show_value(x, cur) + ". " + pystr(get(x, "source")));
    }
    for (const std::string& p : problems(doc)) print("Check: " + p);
    print(NOTE);
    return 0;
}

inline int cmd_check(const Val& a) {
    const std::string path = path_of(a);
    if (!is_file(path)) { print("No " + path + " yet."); return 1; }
    const std::vector<std::string> bad = problems(load(path));
    if (truthy(get(a, "json"))) { DumpOpts o; o.indent = 2; print(dumps(obj({{"ok", bv(bad.empty())}, {"problems", strlist(bad)}}), o)); }
    else if (!bad.empty()) for (const std::string& b : bad) print("Problem: " + b);
    else print("OK: SOM <= SAM <= TAM, every input has a source, the SOM has its reason.");
    return bad.empty() ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("market.py", "Market Sizer (seed-market): TAM, SAM, SOM with sources.");
    p.subcommands("cmd", false);
    auto common = [](Parser& s, bool writes) {
        s.opt({"--dir"}).help("the seed folder (default ./seed)");
        s.opt({"--today"}).help("YYYY-MM-DD for the date written (default: today)");
        s.opt({"--json"}).store_true();
        if (writes) s.opt({"--write"}).store_true().help("save seed/market.json");
    };
    {
        Parser& s = p.sub("top-down"); s.help_line = "population x share x spend";
        s.opt({"--population"}).required(); s.opt({"--population-name"}); s.opt({"--population-source"}).required();
        s.opt({"--share"}).required().help("10%% or 0.1"); s.opt({"--share-name"}); s.opt({"--share-source"}).required();
        s.opt({"--spend"}).required().help("spend a year each"); s.opt({"--spend-name"}); s.opt({"--spend-source"}).required();
        s.opt({"--level"}).choices({"tam", "sam"}); s.opt({"--currency"});
        common(s, true);
    }
    {
        Parser& s = p.sub("bottom-up"); s.help_line = "customers x purchases a year x price";
        s.opt({"--customers"}).required(); s.opt({"--customers-name"}); s.opt({"--customers-source"}).required();
        s.opt({"--frequency"}).required().help("purchases a year per customer"); s.opt({"--frequency-source"}).required();
        s.opt({"--price"}).required(); s.opt({"--price-source"}).required();
        s.opt({"--unit"}).help("what one purchase is, e.g. \"walk\" (default \"purchase\")");
        s.opt({"--level"}).choices({"sam", "tam"}); s.opt({"--currency"});
        common(s, true);
    }
    {
        Parser& s = p.sub("som"); s.help_line = "the share of SAM you can win in N years";
        s.opt({"--share"}).required(); s.opt({"--years"}).integer().required(); s.opt({"--reason"}).required();
        common(s, true);
    }
    {
        Parser& s = p.sub("cite"); s.help_line = "a sourced fact the plan quotes (not part of the math)";
        s.opt({"--name"}).required(); s.opt({"--value"}).required(); s.opt({"--unit"}); s.opt({"--source"}).required();
        common(s, true);
    }
    { Parser& s = p.sub("show"); s.help_line = "print seed/market.json with its checks"; common(s, false); }
    { Parser& s = p.sub("check"); s.help_line = "exit 1 if the numbers are out of order or unsourced"; common(s, false); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "top-down") return cmd_top_down(a);
        if (cmd == "bottom-up") return cmd_bottom_up(a);
        if (cmd == "som") return cmd_som(a);
        if (cmd == "cite") return cmd_cite(a);
        if (cmd == "show") return cmd_show(a);
        return cmd_check(a);
    } catch (const MarketError& e) { eprint(std::string("market.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-market/market", main);
}  // namespace packtools::seed_market
