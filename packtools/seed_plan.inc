// seed-plan/plan: the idea, turned into a business plan. init writes seed/config.json (shared by every seed skill) and the
// plan.json skeleton; build pulls the numbers from seed-model, seed-market and seed-scout's files into plan.json, writes
// seed/plan.md and runs the checks; check runs the checks only. A C++ port of seed-skill/seed-plan/plan.py (MIT, Jake
// Schincariol); same files, output and exit codes: init 0 / 1 refused, build 0, check 0 clean / 1 flags, 2 bad input.
#include <cmath>
#include <map>
#include <optional>
namespace packtools::seed_plan {
using namespace pyrt;
using namespace packtools::seed;
using seed_model::capitalize;
using seed_model::py_eq;

struct PlanError : std::runtime_error { int code; explicit PlanError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {} };

inline const std::vector<std::pair<std::string, std::string>> SECTIONS = {{"executive_summary", "Executive summary"}, {"problem", "Problem"},
    {"solution", "Solution"}, {"market", "Market"}, {"competition", "Competition"}, {"business_model", "Business model"},
    {"go_to_market", "Go-to-market"}, {"financial_plan", "Financial plan"}, {"use_of_funds", "Use of funds"}};
inline const char* const NOTE = "Projections come from your own assumptions in seed/model.json: a plan, not a promise. Raising money "
                                "depends on the business, and investors decide. Not investment advice.";
inline const char* const HEX = R"(^#[0-9A-Fa-f]{6}$)";

// ---------------------------------------------------------------- dynamic-typing helpers
inline Val pget(const Val& o, const std::string& k, const Val& dflt = Val()) {   // o.get(k[, dflt])
    if (o.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'get'");
    const Val* v = o.d->get(k);
    return v ? *v : dflt;
}
inline Val vor(const Val& v, const Val& dflt) { return truthy(v) ? v : dflt; }   // v or dflt
inline Val pgo(const Val& o, const std::string& k, const Val& dflt) { return vor(pget(o, k), dflt); }   // o.get(k) or dflt
inline std::string strip_str(const Val& v) {   // (v or "").strip()
    if (!truthy(v)) return "";
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'strip'");
    return strip(v.s);
}
inline std::string join_strs(const std::vector<Val>& v, const std::string& sep) {   // sep.join(v)
    std::vector<std::string> o;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i].t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(i) + ": expected str instance, " + type_name(v[i]) + " found");
        o.push_back(v[i].s);
    }
    return join(o, sep);
}
inline std::string str_of(const Val& v, const char* attr) {   // a value a str method is called on
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + attr + "'");
    return v.s;
}
inline std::string fmt_d(const Val& v) {   // "%d" % v
    if (v.t == Val::INT) return seed_scout::int_str(big_of(v));
    if (v.t == Val::BOOL) return v.b ? "1" : "0";
    if (v.t == Val::FLOAT) {
        if (std::isnan(v.f)) throw PyErr("ValueError", "cannot convert float NaN to integer");
        if (std::isinf(v.f)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
        return seed_scout::int_str(Decimal::from_double(std::trunc(v.f)).to_int());
    }
    throw PyErr("TypeError", std::string("%d format: a real number is required, not ") + type_name(v));
}
inline std::string group_float(const Val& v) {   // "{:,}".format(x) for an int or a float
    if (v.t == Val::INT) return pyrt::commas(seed_scout::int_str(big_of(v)));
    if (v.t == Val::BOOL) return v.b ? "1" : "0";
    if (v.t == Val::FLOAT) {
        const std::string r = float_repr(v.f);
        if (r.find_first_of("en") != std::string::npos) return r;   // 1e+16, inf, nan
        const std::size_t dot = r.find('.');
        const bool neg = r[0] == '-';
        const std::string ip = r.substr(neg ? 1 : 0, dot - (neg ? 1 : 0));
        return (neg ? "-" : "") + pyrt::commas(ip) + r.substr(dot);
    }
    throw PyErr("ValueError", std::string("Cannot specify ',' with 's'."));
}

// ---------------------------------------------------------------- numbers
inline Decimal D(const Val& x, const std::string& what = "a number") {
    if (x.t == Val::BOOL || x.is_none()) throw PlanError(what + " is not a number: " + repr(x));
    try {
        if (x.t == Val::FLOAT) return Decimal::parse(float_repr(x.f));   // Decimal(repr(x))
        if (x.t == Val::INT) return Decimal::from_int(big_of(x));
        std::string s = strip(pystr(x));
        const std::size_t k = s.find_first_not_of('$');
        s = k == std::string::npos ? "" : s.substr(k);
        return Decimal::parse(ascii_digits(replace_all(s, ",", "")));
    } catch (const DecimalError&) {
        throw PlanError(what + " is not a number: " + repr(x));
    }
}
inline Val fnum(const Decimal& d) {   // int(d) if whole, else float(d)
    if (d.is_nan()) return Val::flt(std::nan(""));
    if (d.is_infinite()) throw PyErr("OverflowError", "cannot convert Infinity to integer");
    if (d == d.to_integral_value()) return int_val(d.to_int());
    return Val::flt(d.to_double());
}
inline Val fnum(const Val& x) { return fnum(D(x)); }
inline Decimal rnd(const Decimal& d, long dp) { return seed_deck::rnd(d, dp); }
inline std::string money(const Decimal& d, int dp = -1) { return seed_deck::money(d, dp); }
inline std::string money(const Val& x, int dp = -1) { return money(D(x), dp); }
inline std::string short_money(const Val& x) { return seed_deck::short_money(D(x)); }
inline std::string trim_f(const Decimal& q) { return seed_deck::trim(q); }
inline std::string pct(const Decimal& d, long dp = 1) { return trim_f(rnd(d, dp)) + "%"; }
inline std::string pct(const Val& x, long dp = 1) { return pct(D(x), dp); }
inline std::string trim_pct(const Decimal& d) { return trim_f(d) + "%"; }

inline Decimal parse_money(const std::string& text) {   // 1500000, 1,500,000, $1.5M, 750k, 2B -> dollars
    std::string t = replace_all(strip(text), ",", "");
    const std::size_t k = t.find_first_not_of('$');
    t = strip(k == std::string::npos ? "" : t.substr(k));
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)\s*([kKmMbB])?$)", t);
    if (!m) throw PlanError("--raise wants an amount like 1500000, $1.5M or 750k (got " + str_repr(text) + ")");
    Decimal v = Decimal::parse(ascii_digits(m.group(1)));
    if (m.has(2) && !m.group(2).empty()) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(m.group(2)[0])));
        v = seed_deck::scaleb(v, c == 'k' ? 3 : c == 'm' ? 6 : 9);
    }
    return v;
}
inline Decimal parse_money(const Val& v) { return parse_money(pystr(v)); }

inline std::vector<Decimal> known_values(const std::vector<Val>& docs) {
    std::vector<Decimal> vals;
    for (const Val& d : docs) seed_deck::walk_known(d, vals);
    return vals;
}
inline std::vector<std::string> untraced(const std::string& text, const std::vector<Decimal>& known) {
    std::vector<std::string> o;
    for (const auto& t : seed_deck::tokens(text)) if (seed_deck::worth_checking(t) && !seed_deck::matches(t, known)) o.push_back(t.raw);
    return o;
}

// ---------------------------------------------------------------- files
inline std::optional<Val> read_json(const std::string& path, const std::string& what, bool required = true) {
    if (!exists(path)) {
        if (required) throw PlanError("no " + what + " at " + path);
        return std::nullopt;
    }
    try { return loads(read_text(path)); }
    catch (const OSError&) { throw; }
    catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw PlanError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline void write_file(const std::string& path, const std::string& text) {
    const std::string d = pyrt::dirname(path);
    if (!d.empty()) makedirs(d);
    write_text(path, text);
}
inline std::string dump2(const Val& v) { DumpOpts o; o.indent = 2; return dumps(v, o); }
inline void write_json(const std::string& path, const Val& doc) { write_file(path, dump2(doc) + "\n"); }
inline Val strip_notes(const Val& doc) {
    if (doc.t == Val::DICT) {
        Val o = Val::dict();
        for (const auto& [k, v] : doc.d->items) if (!(k.size() && k[0] == '_')) o.d->set(k, strip_notes(v));
        return o;
    }
    if (doc.t == Val::LIST) { Val o = Val::list(); for (const Val& v : *doc.l) o.l->push_back(strip_notes(v)); return o; }
    return doc;
}
// date.fromisoformat (3.11+): YYYY-MM-DD, YYYYMMDD and the ISO week forms YYYY-Www[-D], YYYYWww[D]; nullopt if invalid
inline std::optional<long long> date_fromiso(const std::string& s) {
    const auto dig = [&](std::size_t a, std::size_t n) { for (std::size_t k = a; k < a + n; ++k) if (k >= s.size() || s[k] < '0' || s[k] > '9') return false; return true; };
    const auto num = [&](std::size_t a, std::size_t n) { long long r = 0; for (std::size_t k = a; k < a + n; ++k) r = r * 10 + (s[k] - '0'); return r; };
    const auto leap = [](long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; };
    const auto wd = [](long long days) { return static_cast<int>(((days + 3) % 7 + 7) % 7); };
    if (s.size() < 7 || !dig(0, 4)) return std::nullopt;
    const long long y = num(0, 4);
    std::size_t k = 4; const bool dash = s[4] == '-'; if (dash) k = 5;
    if (k < s.size() && s[k] == 'W') {
        ++k; if (!dig(k, 2)) return std::nullopt; const long long w = num(k, 2); k += 2; long long d = 1;
        if (k < s.size()) { if (dash) { if (s[k] != '-') return std::nullopt; ++k; } if (!dig(k, 1) || k + 1 != s.size()) return std::nullopt; d = num(k, 1); }
        if (y < 1) return std::nullopt;
        const long long jan4 = make_day(y, 1, 4), wk1 = jan4 - wd(jan4);
        const int jan1 = wd(make_day(y, 1, 1));
        const int weeks = (jan1 == 3 || (jan1 == 2 && leap(y))) ? 53 : 52;
        if (w < 1 || w > weeks || d < 1 || d > 7) return std::nullopt;
        return wk1 + (w - 1) * 7 + (d - 1);
    }
    if (dash ? (s.size() != 10 || s[7] != '-' || !dig(5, 2) || !dig(8, 2)) : (s.size() != 8 || !dig(4, 4))) return std::nullopt;
    const long long m = dash ? num(5, 2) : num(4, 2), d = dash ? num(8, 2) : num(6, 2);
    if (!valid_date(y, static_cast<int>(m), static_cast<int>(d))) return std::nullopt;
    return make_day(y, static_cast<int>(m), static_cast<int>(d));
}
inline long long parse_today(const std::string& text) {
    if (text.empty()) return today_days();
    const auto d = date_fromiso(text);
    if (!d) throw PlanError("--today wants YYYY-MM-DD (got " + str_repr(text) + ")");
    return *d;
}
inline std::string built_date(long long day) {   // today.strftime("%b %d, %Y").replace(" 0", " ")
    static const char* const MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string iso = iso_day(day);   // YYYY-MM-DD
    const std::string s = std::string(MON[std::stoi(iso.substr(5, 2)) - 1]) + " " + iso.substr(8, 2) + ", " + iso.substr(0, 4);
    return replace_all(s, " 0", " ");
}

// ---------------------------------------------------------------- the numbers block
inline Val unit_economics(const Val& a) {
    const auto key = [&](const Val& o, const std::string& k) -> Val {   // o[k]; a missing key is the KeyError plan.py turns into advice
        if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
        const Val* v = o.d->get(k);
        if (!v) throw PlanError("model.json's assumptions have no " + str_repr(k) + ": run model.py run --write again");
        return *v;
    };
    const Decimal price = D(key(a, "price"), "price");
    const Decimal take = D(key(a, "take_rate_pct"), "take_rate_pct");
    const Val cust = pgo(a, "customers", Val::dict());
    const Decimal opc = D(key(cust, "orders_per_customer_month"), "orders_per_customer_month");
    const Decimal churn = D(key(cust, "churn_monthly_pct"), "churn_monthly_pct");
    const Decimal cac = D(key(a, "cac"), "cac");
    Val costs = Val::list();
    for (const Val& c : items_of(pgo(a, "cost_per_order", Val::list()))) {
        const Decimal one = price * D(pget(c, "pct_of_price", iv(0)), "pct_of_price") / Decimal(100) + D(pget(c, "fixed", iv(0)), "fixed");
        costs.l->push_back(obj({{"label", sv(pystr(pgo(c, "label", sv("cost"))))}, {"amount", fnum(one)}}));
    }
    const Decimal rev = price * take / Decimal(100);
    Decimal cost(0);
    for (const Val& c : *costs.l) cost = cost + D(*c.d->get("amount"));
    const Decimal contrib = rev - cost, per_cust = contrib * opc;
    std::optional<Decimal> ltv, ltv_cac, payback;
    if (churn > Decimal(0)) ltv = per_cust / (churn / Decimal(100));
    if (ltv && cac > Decimal(0)) ltv_cac = *ltv / cac;
    if (per_cust > Decimal(0)) payback = cac / per_cust;
    const Val unit_v = pget(a, "unit");
    std::string units;
    if (truthy(pget(a, "units"))) units = pystr(pget(a, "units"));
    else {
        const Val u = vor(unit_v, sv("order"));
        if (u.t != Val::STR) throw PyErr("TypeError", std::string("unsupported operand type(s) for +: '") + type_name(u) + "' and 'str'");
        units = u.s + "s";
    }
    return obj({{"unit", sv(pystr(vor(unit_v, sv("order"))))}, {"units", sv(units)}, {"price", fnum(price)}, {"take_rate_pct", fnum(take)},
                {"revenue_per_order", fnum(rev)}, {"cost_per_order", fnum(cost)}, {"costs", costs}, {"contribution_per_order", fnum(contrib)},
                {"orders_per_customer_month", fnum(opc)}, {"contribution_per_customer_month", fnum(per_cust)}, {"churn_monthly_pct", fnum(churn)},
                {"cac", fnum(cac)}, {"ltv", ltv ? fnum(rnd(*ltv, 2)) : Val()}, {"ltv_cac", ltv_cac ? fnum(rnd(*ltv_cac, 2)) : Val()},
                {"cac_payback_months", payback ? fnum(rnd(*payback, 2)) : Val()}});
}

inline std::vector<Val> month_rows(const Val& model) {   // model.get("months") or [] as a list (other types: Python's errors)
    const Val ms = pgo(model, "months", Val::list());
    if (ms.t == Val::LIST) return *ms.l;
    if (ms.t == Val::STR) { std::vector<Val> o; for (const auto& ch : cps(ms.s)) o.push_back(sv(ch)); return o; }
    if (ms.t == Val::DICT) throw PyErr("TypeError", "unhashable type: 'slice'");
    throw PyErr("TypeError", std::string("'") + type_name(ms) + "' object is not subscriptable");
}
inline Val years_of(const Val& model) {
    const std::vector<Val> months = month_rows(model);
    Val out = Val::list();
    for (int y = 0; y < 3; ++y) {
        if (months.size() < static_cast<std::size_t>((y + 1) * 12)) break;
        const std::vector<Val> rows(months.begin() + y * 12, months.begin() + (y + 1) * 12);
        const auto tot = [&](const std::string& k) { Decimal s(0); for (const Val& r : rows) s = s + D(pget(r, k, iv(0))); return fnum(s); };
        Val row = obj({{"year", iv(y + 1)}});
        for (const char* k : {"orders", "gmv", "revenue", "gross_profit", "opex", "net"}) row.d->set(k, tot(k));
        row.d->set("cash_end", fnum(D(pget(rows.back(), "cash", iv(0)))));
        row.d->set("customers_end", pget(rows.back(), "customers"));
        out.l->push_back(row);
    }
    return out;
}
inline std::optional<Decimal> num_or_value(Val x) {   // market.json tam/sam/som: a number, or {"value": n}
    if (x.t == Val::DICT) x = pget(x, "value");
    if (x.is_none() || x.t == Val::BOOL) return std::nullopt;
    try { return D(x); } catch (const PlanError&) { return std::nullopt; }
}
inline Val hkey_get(const Val& d, const Val& k) {   // d.get(k) for a JSON dict and any key
    if (k.t == Val::LIST || k.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(k) + "'");
    if (d.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(d) + "' object has no attribute 'get'");
    return k.t == Val::STR ? pget(d, k.s) : Val();
}
inline Decimal neg_num(const Val& v) {   // -v for the sort key
    if (v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL) return Decimal(0) - dec_of(v);
    throw PyErr("TypeError", std::string("bad operand type for unary -: '") + type_name(v) + "'");
}

inline Val gather(const Val& cfg, const std::optional<Val>& model, const std::optional<Val>& market, const std::optional<Val>& comps) {
    const Val st = pgo(cfg, "startup", Val::dict());
    Val n = obj({{"currency", sv("USD")}, {"raise", Val()}, {"instrument", pgo(st, "instrument", sv(""))}, {"stage", pgo(st, "stage", sv(""))}});
    const Val r = pget(st, "raise");
    if (!(r.is_none() || (r.t == Val::STR && r.s.empty()))) n.d->set("raise", fnum(parse_money(r)));
    if (model && truthy(*model)) {
        const Val s = pgo(*model, "summary", Val::dict());
        n.d->set("currency", vor(pget(s, "currency"), *n.d->get("currency")));
        const Val assumptions = pgo(*model, "assumptions", Val::dict());
        n.d->set("unit_economics", unit_economics(assumptions));
        Val fin = Val::dict();
        for (const char* k : {"months", "start_month", "end_month", "revenue_y1", "revenue_y2", "revenue_y3", "gmv_y1", "gmv_y2", "gmv_y3",
                              "breakeven_month", "breakeven_date", "peak_burn", "peak_burn_month", "runway_months", "cash_out_month", "cash_low",
                              "cash_low_month", "cash_end", "customers_end"}) fin.d->set(k, pget(s, k));
        fin.d->set("years", years_of(*model));
        n.d->set("financials", fin);
        n.d->set("model_raise", pget(s, "raise"));
        Val uof = pget(s, "use_of_funds");
        if (!truthy(uof)) uof = pgo(assumptions, "use_of_funds", Val::dict());
        n.d->set("use_of_funds", uof);
        Decimal base(0);
        if (truthy(*n.d->get("raise"))) base = D(*n.d->get("raise"));
        else {
            if (uof.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(uof) + "' object has no attribute 'values'");
            for (const auto& kv : uof.d->items) base = base + D(kv.second);
        }
        Val pcts = Val::dict();
        if (base != Decimal(0) || base.is_nan()) {
            if (uof.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(uof) + "' object has no attribute 'items'");
            for (const auto& [k, v] : uof.d->items) pcts.d->set(k, fnum(rnd(D(v) / base * Decimal(100), 1)));
        }
        n.d->set("use_of_funds_pct", pcts);
    }
    if (market && truthy(*market)) {
        const Val& m = *market;
        const auto nv = [&](const char* k) { const auto d = num_or_value(pget(m, k)); return d ? fnum(*d) : Val(); };
        Val mk = obj({{"method", pget(m, "method")}, {"currency", vor(pget(m, "currency"), *n.d->get("currency"))}});
        mk.d->set("tam", nv("tam")); mk.d->set("sam", nv("sam")); mk.d->set("som", nv("som"));
        mk.d->set("inputs", strip_notes(pgo(m, "inputs", Val::list())));
        mk.d->set("notes", pgo(m, "notes", sv("")));
        n.d->set("market", mk);
    }
    if (comps && truthy(*comps)) {
        const Val& c = *comps;
        Val rows = Val::list();
        const Val labels = pgo(c, "theme_labels", Val::dict());
        for (const Val& app : items_of(pgo(c, "apps", Val::list()))) {
            const Val themes = pgo(app, "themes", Val::dict());
            Val top = pget(app, "top_theme");
            if (!truthy(top) && truthy(themes)) {
                if (themes.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(themes) + "' object has no attribute 'items'");
                const std::pair<std::string, Val>* best = nullptr;
                for (const auto& kv : themes.d->items) {
                    if (!best) { (void)neg_num(kv.second); best = &kv; continue; }
                    const Decimal a = neg_num(kv.second), b = neg_num(best->second);
                    if (a < b || (a == b && kv.first < best->first)) best = &kv;
                }
                top = sv(best->first);
            }
            Val count = pget(app, "weak_point_count");
            if (count.is_none() && truthy(top)) count = hkey_get(themes, top);
            const Val wp = pget(app, "weak_point");
            const Val lab = hkey_get(labels, top);
            rows.l->push_back(obj({{"name", pget(app, "name")}, {"rating", pget(app, "rating")}, {"ratings", pget(app, "ratings")},
                                   {"reviews_read", pget(app, "reviews_read")}, {"low_reviews", pget(app, "low_reviews")}, {"top_theme", top},
                                   {"top_label", vor(vor(lab, wp), top)}, {"top_count", count}, {"weak_point", vor(vor(wp, lab), top)},
                                   {"source", pget(app, "source")}}));
        }
        n.d->set("competitors", rows);
        n.d->set("competitors_generated", pget(c, "generated"));
        n.d->set("low_stars", pgo(c, "low_stars", iv(2)));
    }
    return n;
}

// ---------------------------------------------------------------- checks
struct Sec { std::string key; Val title; std::string text, pitch; std::vector<Val> points; };
inline std::vector<Sec> section_list(const Val& plan) {   // the nine sections in order, filling in any the file is missing
    std::vector<std::pair<Val, Val>> have;
    for (const Val& s : items_of(pgo(plan, "sections", Val::list()))) {
        if (s.t != Val::DICT) continue;
        const Val k = pget(s, "key");
        if (k.t == Val::LIST || k.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(k) + "'");
        have.push_back({k, s});
    }
    std::vector<Sec> out;
    for (const auto& [key, title] : SECTIONS) {
        Val s = Val::dict();
        for (const auto& [k, v] : have) if (k.t == Val::STR && k.s == key) s = v;   // the last one wins, like a dict comprehension
        s = vor(s, Val::dict());
        Sec o{key, vor(pget(s, "title"), sv(title)), strip_str(pget(s, "text")), strip_str(pget(s, "pitch")), {}};
        for (const Val& p : items_of(pgo(s, "points", Val::list()))) if (truthy(p)) o.points.push_back(p);
        out.push_back(o);
    }
    return out;
}
inline Val date_of(const Val& model, const Val& m) {   // the month's "date", or None
    const std::vector<Val> rows = month_rows(model);
    if (m.t == Val::STR || m.t == Val::LIST || m.t == Val::DICT) throw PyErr("TypeError", std::string("'<' not supported between instances of 'int' and '") + type_name(m) + "'");
    const Decimal md = dec_of(m);
    if (!(Decimal(0) < md && md <= Decimal(static_cast<long long>(rows.size())))) return Val();
    if (m.t == Val::FLOAT) throw PyErr("TypeError", "list indices must be integers or slices, not float");
    return pget(rows[static_cast<std::size_t>(seed_model::small_int(md) - 1)], "date");
}
inline bool mue_has(const Val& mue, const std::string& k) {   // k in mue and mue[k] is not None
    if (mue.t == Val::DICT) { const Val* v = mue.d->get(k); return v && !v->is_none(); }
    if (mue.t == Val::LIST) {
        for (const Val& x : *mue.l) if (x.t == Val::STR && x.s == k) throw PyErr("TypeError", "list indices must be integers or slices, not str");
        return false;
    }
    if (mue.t == Val::STR) { if (mue.s.find(k) != std::string::npos) throw PyErr("TypeError", "string indices must be integers, not 'str'"); return false; }
    throw PyErr("TypeError", std::string("argument of type '") + type_name(mue) + "' is not iterable");
}
inline std::string runway_text(const Val& fin) {
    if (truthy(pget(fin, "cash_out_month")))
        return fmt_d(pget(fin, "runway_months")) + " months: the cash runs out in month " + fmt_d(pget(fin, "cash_out_month"));
    const std::string low = !pget(fin, "cash_low").is_none() ? " (low point " + money(pget(fin, "cash_low")) + " in month " + pystr(pget(fin, "cash_low_month")) + ")" : "";
    return "the cash lasts all " + pystr(pgo(fin, "months", iv(36))) + " months" + low;
}
inline std::string month_name(const Val& ym) { return seed_deck::month_name(ym); }

using Paths = std::map<std::string, std::string>;
inline std::pair<std::vector<std::string>, std::vector<std::string>> checks(const Val& cfg, const Val& plan, const std::optional<Val>& model,
                                                                            const std::optional<Val>& market, const std::optional<Val>& comps, const Val& n, const Paths& paths) {
    std::vector<std::string> flags, notes;
    const auto secs = section_list(plan);
    for (std::size_t i = 0; i < secs.size(); ++i) {
        if (secs[i].text.empty()) flags.push_back(pystr(secs[i].title) + " is empty: write it in seed/plan.json (sections[" + std::to_string(i) + "].text).");
        else if (secs[i].pitch.empty()) notes.push_back(pystr(secs[i].title) + " has no pitch line: the deck will use its first sentence.");
    }
    const std::pair<const char*, const char*> files[] = {{"model.json", "/seed-model"}, {"market.json", "/seed-market"}, {"competitors.json", "/seed-scout"}};
    const std::optional<Val>* docs[] = {&model, &market, &comps};
    for (int i = 0; i < 3; ++i) if (!*docs[i]) flags.push_back(std::string("No ") + paths.at(files[i].first) + ": run " + files[i].second + " first, then build again.");
    const Val st = pgo(cfg, "startup", Val::dict());
    for (const char* k : {"name", "one_liner"}) if (!truthy(pget(st, k))) flags.push_back(std::string("config.json has no startup.") + k + ".");
    const Val raise_v = pget(n, "raise");
    if (raise_v.is_none() || D(raise_v) <= Decimal(0)) flags.push_back("config.json has no startup.raise: how much are you raising?");
    const bool has_model = model && truthy(*model);
    if (has_model) {
        const Val mr = pget(n, "model_raise");
        if (!raise_v.is_none() && !mr.is_none() && D(mr) != D(raise_v))
            flags.push_back("The raise disagrees: config.json says " + money(raise_v) + ", model.json says " + money(mr) + ".");
        const Val funds = pgo(n, "use_of_funds", Val::dict());
        Decimal total(0);
        if (funds.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(funds) + "' object has no attribute 'values'");
        for (const auto& kv : funds.d->items) total = total + D(kv.second);
        std::optional<Decimal> target;
        if (!mr.is_none()) target = D(mr); else if (!raise_v.is_none()) target = D(raise_v);
        if (!truthy(funds)) flags.push_back("No use of funds in model.json: add use_of_funds to seed/assumptions.json.");
        else if (target && total != *target) flags.push_back("The use of funds adds up to " + money(total) + ", not the " + money(*target) + " raise.");
        const Val s = pgo(*model, "summary", Val::dict());
        const Val fin = *n.d->get("financials");
        long long y = 0;
        for (const Val& row : *fin.d->get("years")->l) {
            ++y;
            const Val said = pget(s, "revenue_y" + std::to_string(y));
            if (!said.is_none() && D(said) != D(*row.d->get("revenue")))
                flags.push_back("model.json says year " + std::to_string(y) + " revenue is " + money(said) + " but its months add up to " +
                                money(*row.d->get("revenue")) + ": run model.py run --write again.");
        }
        const Val mue = pgo(*model, "unit_economics", Val::dict());
        const Val ue = *n.d->get("unit_economics");
        std::vector<std::string> stale;
        for (const char* k : {"revenue_per_order", "contribution_per_order", "ltv", "ltv_cac", "cac_payback_months"})
            if (mue_has(mue, k) && !pget(ue, k).is_none() && D(*mue.d->get(k)) != D(pget(ue, k))) stale.push_back(k);
        if (!stale.empty()) flags.push_back("model.json's unit economics (" + join(stale, ", ") + ") no longer match its assumptions: run model.py run --write again.");
        const std::string unit = pystr(pget(ue, "unit"));
        if (D(pget(ue, "contribution_per_order")) <= Decimal(0)) {
            flags.push_back("Each " + unit + " loses money before opex: contribution is " + money(pget(ue, "contribution_per_order"), 2) +
                            ". Raise the price or the take rate, or cut the cost per " + unit + ".");
        } else {
            if (!pget(ue, "ltv_cac").is_none() && D(pget(ue, "ltv_cac")) < Decimal(3))
                notes.push_back("LTV:CAC is " + pystr(pget(ue, "ltv_cac")) + "x, under the 3x most seed investors look for.");
            if (!pget(ue, "cac_payback_months").is_none() && D(pget(ue, "cac_payback_months")) > Decimal(12))
                notes.push_back("CAC payback is " + pystr(pget(ue, "cac_payback_months")) + " months, over the 12 investors usually want.");
        }
        if (truthy(pget(fin, "cash_out_month"))) {
            const Val com = pget(fin, "cash_out_month");
            const std::string m1 = fmt_d(com), when = month_name(date_of(*model, com)), m2 = fmt_d(pget(fin, "runway_months"));
            notes.push_back("The cash runs out in month " + m1 + " (" + when + "): the raise covers " + m2 + " months. Say what it buys by then, or raise more.");
        }
        if (!truthy(pget(fin, "breakeven_month"))) notes.push_back("No breakeven inside the " + pystr(pgo(fin, "months", iv(36))) + " months of the model.");
    }
    if (market && truthy(*market)) {
        const Val mk = *n.d->get("market");
        const Val tam = pget(mk, "tam"), sam = pget(mk, "sam"), som = pget(mk, "som");
        if (tam.is_none() || sam.is_none() || som.is_none()) flags.push_back("market.json needs tam, sam and som: run /seed-market.");
        else {
            if (D(som) > D(sam)) flags.push_back("The SOM (" + short_money(som) + ") is bigger than the SAM (" + short_money(sam) + ").");
            if (D(sam) > D(tam)) flags.push_back("The SAM (" + short_money(sam) + ") is bigger than the TAM (" + short_money(tam) + ").");
            if (has_model) {
                const Val g3 = pget(*n.d->get("financials"), "gmv_y3");
                if (!g3.is_none() && D(g3) > D(som))
                    flags.push_back("Year 3 GMV (" + short_money(g3) + ") is above your SOM (" + short_money(som) +
                                    "): the model sells more than the share of the market you said you can win.");
            }
        }
        if (has_model && truthy(pget(mk, "currency")) && !py_eq(pget(mk, "currency"), *n.d->get("currency")))
            flags.push_back("market.json is in " + pystr(pget(mk, "currency")) + " but model.json is in " + pystr(*n.d->get("currency")) + ".");
    }
    std::vector<Val> docs_k = {strip_notes(cfg)};
    if (model && truthy(*model)) {
        if (model->t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(*model) + "' object has no attribute 'items'");
        Val m = Val::dict(); for (const auto& [k, v] : model->d->items) if (k != "months") m.d->set(k, v);
        docs_k.push_back(m);
    } else if (model) docs_k.push_back(*model);
    if (market) docs_k.push_back(*market);
    if (comps) docs_k.push_back(*comps);
    docs_k.push_back(n);
    const auto known = known_values(docs_k);
    for (const auto& s : secs) {
        const std::pair<const char*, std::string> parts[] = {{"text", s.text}, {"pitch", s.pitch}, {"points", join_strs(s.points, " ; ")}};
        for (const auto& [where, text] : parts) {
            const auto bad = untraced(text, known);
            if (!bad.empty())
                notes.push_back(pystr(s.title) + " " + where + ": " + join(bad, ", ") + " " + (bad.size() == 1 ? "is" : "are") + " in none of the seed/ files. Check " +
                                (bad.size() == 1 ? "it" : "them") + ".");
        }
    }
    if (strip_str(pget(plan, "tagline")).empty()) notes.push_back("No tagline in plan.json: the deck's title slide will use the one-liner.");
    bool team = false;
    for (const Val& t : items_of(pgo(plan, "team", Val::list()))) if (t.t == Val::DICT && truthy(pget(t, "name"))) { team = true; break; }
    if (!team) notes.push_back("No team in plan.json: the deck's team slide needs at least the founder.");
    return {flags, notes};
}

// ---------------------------------------------------------------- the plan
inline std::string md_escape(const std::string& text) { return replace_all(text, "|", "\\|"); }
inline std::string md_escape(const Val& v) { return md_escape(pystr(v)); }
inline std::string cap(const std::string& t) { return upper(cpslice(t, 0, 1)) + cpslice(t, 1, static_cast<long>(cplen(t))); }
inline std::string stars_text(const Val& n) {
    const Val low = pgo(n, "low_stars", iv(2));
    return py_eq(low, iv(1)) ? "1 star" : py_eq(low, iv(2)) ? "1 and 2 star" : "1 to " + pystr(low) + " star";
}
inline std::string input_text(const Val& it, const Val& cur) {   // a market input as people read it
    const Val v = pget(it, "value");
    const std::string unit = pystr(pgo(it, "unit", sv("")));
    Decimal d(0);
    try { d = D(v); } catch (const PlanError&) { return pystr(v); }
    std::string num;
    if (d == d.to_integral_value()) {
        if (d.is_infinite()) throw PyErr("OverflowError", "cannot convert Infinity to integer");
        num = pyrt::commas(seed_scout::int_str(d.to_int()));
    } else num = d.format(",f");
    const std::string lu = lower(unit);
    const auto cat = [&](const char* side) -> std::string {
        if (cur.t != Val::STR) throw PyErr("TypeError", std::string(side) + type_name(cur) + "' and 'str'");
        return cur.s + " ";
    };
    bool cur_match = lu == "usd" || lu == "$";
    if (!cur_match) cur_match = cur.t == Val::STR && unit == cur.s;
    if (!cur_match) cur_match = unit.rfind(cat("unsupported operand type(s) for +: '"), 0) == 0;
    if (cur_match) {
        const std::string pre = cat("unsupported operand type(s) for +: '");
        const std::string rest = unit.rfind(pre, 0) == 0 ? unit.substr(cur.s.size()) : "";
        return money(d) + rest;
    }
    if ((!unit.empty() && unit[0] == '%') || unit == "pct") return trim_pct(d) + (!unit.empty() && unit[0] == '%' ? unit.substr(1) : "");
    if (unit == "count" || unit.empty()) return num;
    return num + " " + unit;
}
inline Val S_(const Val& o, const char* k) { return o.d->get(k) ? *o.d->get(k) : Val(); }   // a key the numbers block always has

inline std::vector<std::string> section_data(const std::string& key, const Val& plan, const Val& n) {
    std::vector<std::string> L;
    const Val ue = pget(n, "unit_economics"), fin = pget(n, "financials"), mk = pget(n, "market");
    const std::vector<Val> comps = items_of(pgo(n, "competitors", Val::list()));
    if (key == "executive_summary") {
        std::vector<std::string> parts;
        for (const Val& t : items_of(pgo(plan, "team", Val::list()))) {
            if (t.t != Val::DICT || !truthy(pget(t, "name"))) continue;
            const Val line = pget(t, "line");
            parts.push_back(pystr(S_(t, "name")) + ", " + pystr(pgo(t, "role", sv("founder"))) + "." +
                            (truthy(line) ? " " + rstrip_chars(str_of(line, "rstrip"), ".") + "." : ""));
        }
        if (!parts.empty()) { L.push_back("**Team.** " + join(parts, " ")); L.push_back(""); }
    } else if (key == "problem" && !comps.empty()) {
        const Val when = pget(n, "competitors_generated");
        L.push_back("What the competitors' own reviews say (seed/competitors.json" + (truthy(when) ? ", " + pystr(when) : std::string()) + "):");
        L.push_back("");
        for (const Val& c : comps)
            if (!pget(c, "top_count").is_none() && truthy(pget(c, "low_reviews")))
                L.push_back("- " + pystr(S_(c, "name")) + ": " + pystr(S_(c, "top_count")) + " of " + pystr(S_(c, "low_reviews")) + " recent " + stars_text(n) +
                            " reviews mention " + pystr(S_(c, "top_label")) + ".");
        L.push_back("");
    } else if (key == "market" && truthy(mk)) {
        L.push_back("| | Size (" + pystr(pget(mk, "currency", sv("USD"))) + ") |"); L.push_back("| --- | ---: |");
        for (const auto& [k, label] : std::vector<std::pair<std::string, std::string>>{{"tam", "TAM, the whole market"}, {"sam", "SAM, the part you can serve"}, {"som", "SOM, the part you can win"}})
            if (!pget(mk, k).is_none()) L.push_back("| " + label + " | " + money(pget(mk, k)) + " (" + short_money(pget(mk, k)) + ") |");
        L.push_back("");
        if (truthy(pget(mk, "inputs"))) {
            L.push_back("Method: " + pystr(pgo(mk, "method", sv("not given"))) + ". The inputs and where each comes from:");
            L.push_back(""); L.push_back("| Input | Value | Source |"); L.push_back("| --- | ---: | --- |");
            for (const Val& it : items_of(pget(mk, "inputs"))) {
                const std::string shown = input_text(it, pgo(mk, "currency", sv("USD")));
                L.push_back("| " + md_escape(pget(it, "name", sv(""))) + " | " + md_escape(shown) + " | " + md_escape(pgo(it, "source", sv("MISSING: add a source"))) + " |");
            }
            L.push_back("");
        }
        const Val notes = pget(mk, "notes");
        if (notes.t == Val::LIST) {
            bool any = false;
            for (const Val& x : *notes.l) if (truthy(x)) { L.push_back("- " + pystr(x)); any = true; }
            if (any) L.push_back("");
        } else if (truthy(notes)) { L.push_back(pystr(notes)); L.push_back(""); }
    } else if (key == "competition" && !comps.empty()) {
        L.push_back("| App | Rating | Ratings | Low reviews | Weak point | Evidence |"); L.push_back("| --- | ---: | ---: | ---: | --- | --- |");
        for (const Val& c : comps) {
            const std::string ev = !pget(c, "top_count").is_none() && truthy(pget(c, "low_reviews"))
                ? pystr(S_(c, "top_count")) + " of " + pystr(S_(c, "low_reviews")) + " mention " + pystr(S_(c, "top_label")) : "";
            const std::string low = truthy(pget(c, "reviews_read")) ? pystr(pget(c, "low_reviews")) + " of " + pystr(pget(c, "reviews_read")) + " read" : "";
            const Val rt = pget(c, "rating"), rs = pget(c, "ratings");
            const std::string ratings = rs.t == Val::INT || rs.t == Val::BOOL ? group_float(rs) : "";
            L.push_back("| " + md_escape(pgo(c, "name", sv(""))) + " | " + (rt.is_none() ? "" : pystr(rt)) + " | " + ratings + " | " + low + " | " +
                        md_escape(cap(str_of(pgo(c, "weak_point", sv("")), "upper"))) + " | " + md_escape(ev) + " |");
        }
        L.push_back("");
        std::vector<Val> srcs;
        for (const Val& c : comps) if (truthy(pget(c, "source"))) srcs.push_back(S_(c, "source"));
        if (!srcs.empty()) { L.push_back("Sources: " + join_strs(srcs, ", ")); L.push_back(""); }
    } else if (key == "business_model" && truthy(ue)) {
        const std::string u = pystr(S_(ue, "unit"));
        std::vector<std::string> cs;
        for (const Val& c : *S_(ue, "costs").l) cs.push_back(pystr(S_(c, "label")) + " " + money(S_(c, "amount"), 2));
        const std::string costs = join(cs, ", ");
        L.push_back("| Per " + u + " | |"); L.push_back("| --- | ---: |");
        L.push_back("| Price | " + money(S_(ue, "price"), 2) + " |");
        L.push_back("| You keep (" + pct(S_(ue, "take_rate_pct")) + " take rate) | " + money(S_(ue, "revenue_per_order"), 2) + " |");
        L.push_back("| Costs of one " + u + " | " + money(S_(ue, "cost_per_order"), 2) + (costs.empty() ? "" : " (" + md_escape(costs) + ")") + " |");
        L.push_back("| **Contribution per " + u + "** | **" + money(S_(ue, "contribution_per_order"), 2) + "** |");
        L.push_back("| " + capitalize(pystr(S_(ue, "units"))) + " per customer a month | " + pystr(S_(ue, "orders_per_customer_month")) + " |");
        L.push_back("| Contribution per customer a month | " + money(S_(ue, "contribution_per_customer_month"), 2) + " |");
        L.push_back("| CAC (cost to win one customer) | " + money(S_(ue, "cac"), 2) + " |");
        if (!S_(ue, "cac_payback_months").is_none()) L.push_back("| CAC payback | " + pystr(S_(ue, "cac_payback_months")) + " months (CAC / contribution per customer a month) |");
        L.push_back("| Monthly churn | " + pct(S_(ue, "churn_monthly_pct")) + " |");
        if (!S_(ue, "ltv").is_none()) L.push_back("| LTV | " + money(S_(ue, "ltv"), 2) + " (contribution per customer a month / churn) |");
        if (!S_(ue, "ltv_cac").is_none()) L.push_back("| **LTV:CAC** | **" + pystr(S_(ue, "ltv_cac")) + "x** |");
        L.push_back("");
    } else if (key == "financial_plan" && truthy(fin)) {
        const std::vector<Val> yrs = items_of(pgo(fin, "years", Val::list()));
        if (!yrs.empty()) {
            const std::string units = truthy(ue) ? capitalize(pystr(S_(ue, "units"))) : "Orders";
            std::vector<std::string> h, o;
            for (const Val& y : yrs) { h.push_back("Year " + fmt_d(S_(y, "year"))); o.push_back(group_float(S_(y, "orders"))); }
            std::string dash = "| --- |"; for (std::size_t i = 0; i < yrs.size(); ++i) dash += " ---: |";
            L.push_back("| | " + join(h, " | ") + " |"); L.push_back(dash);
            L.push_back("| " + units + " | " + join(o, " | ") + " |");
            for (const auto& [k, label] : std::vector<std::pair<std::string, std::string>>{{"gmv", "GMV"}, {"revenue", "Revenue"}, {"gross_profit", "Gross profit"},
                                                                                         {"opex", "Opex"}, {"net", "Net"}, {"cash_end", "Cash at year end"}}) {
                std::vector<std::string> c; for (const Val& y : yrs) c.push_back(money(S_(y, k.c_str()), 0));
                L.push_back("| " + label + " | " + join(c, " | ") + " |");
            }
            L.push_back("");
        }
        L.push_back("- Breakeven: " + (truthy(pget(fin, "breakeven_month"))
                    ? "month " + fmt_d(S_(fin, "breakeven_month")) + " (" + month_name(S_(fin, "breakeven_date")) + "), and every month after it"
                    : std::string("not inside the model")) + ".");
        if (truthy(pget(fin, "peak_burn"))) L.push_back("- Peak burn: " + money(S_(fin, "peak_burn")) + " in month " + pystr(pget(fin, "peak_burn_month")) + ".");
        L.push_back("- Runway: " + runway_text(fin) + ".");
        L.push_back(""); L.push_back("_From seed/model.json, " + month_name(pget(fin, "start_month")) + " to " + month_name(pget(fin, "end_month")) +
                                     ": projections from your own assumptions, not a forecast._"); L.push_back("");
    } else if (key == "use_of_funds") {
        const Val funds = pgo(n, "use_of_funds", Val::dict());
        if (truthy(funds)) {
            if (funds.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(funds) + "' object has no attribute 'values'");
            Decimal total(0); for (const auto& kv : funds.d->items) total = total + D(kv.second);
            const Val r = pget(n, "raise");
            const Decimal base = truthy(r) ? D(r) : total;
            const bool b = base != Decimal(0) || base.is_nan();
            L.push_back("| Use | Amount | Share |"); L.push_back("| --- | ---: | ---: |");
            for (const auto& [k, v] : funds.d->items) L.push_back("| " + md_escape(k) + " | " + money(v) + " | " + (b ? pct(D(v) / base * Decimal(100)) : "") + " |");
            L.push_back("| **Total** | **" + money(total) + "** | **" + (b ? pct(total / base * Decimal(100)) : "") + "** |");
            L.push_back("");
            if (truthy(r) && total == D(r)) { L.push_back("Adds up to the " + money(r) + " raise."); L.push_back(""); }
        }
    }
    return L;
}

inline std::string render(const Val& cfg, const Val& plan, const Val& n, const std::vector<std::string>& flags, const std::vector<std::string>& notes, long long today) {
    const Val st = pgo(cfg, "startup", Val::dict());
    const std::string name = pystr(pgo(st, "name", sv("Your startup")));
    const auto secs = section_list(plan);
    const Val ue = pget(n, "unit_economics"), fin = pget(n, "financials"), mk = pget(n, "market");
    std::vector<std::string> L = {"# " + name + ": business plan", ""};
    if (truthy(pget(st, "one_liner"))) { L.push_back("**" + rstrip_chars(str_of(pget(st, "one_liner"), "rstrip"), ".") + ".**"); L.push_back(""); }
    const Val r = pget(n, "raise");
    std::string raise_line;
    if (truthy(r)) raise_line = replace_all("Raising a " + money(r) + " " + pystr(pgo(st, "stage", sv(""))) + " round", "  ", " ");
    else raise_line = "The raise is not set yet";
    if (truthy(pget(n, "instrument"))) raise_line += " on a " + pystr(pget(n, "instrument"));
    L.push_back(raise_line + ". Built " + built_date(today) + " from the files in seed/."); L.push_back("");

    std::vector<std::pair<std::string, std::string>> rows;
    if (truthy(r)) rows.push_back({"The raise", money(r) + " " + pystr(pgo(st, "stage", sv("")))});
    if (truthy(mk) && !pget(mk, "tam").is_none() && !pget(mk, "sam").is_none() && !pget(mk, "som").is_none())
        rows.push_back({"Market (TAM / SAM / SOM)", short_money(S_(mk, "tam")) + " / " + short_money(S_(mk, "sam")) + " / " + short_money(S_(mk, "som"))});
    if (truthy(ue)) {
        const std::string unit = pystr(S_(ue, "unit"));
        rows.push_back({"Take rate", pct(S_(ue, "take_rate_pct")) + " of a " + money(S_(ue, "price"), 2) + " " + unit});
        rows.push_back({"Contribution per " + unit, money(S_(ue, "contribution_per_order"), 2)});
        if (!S_(ue, "ltv_cac").is_none()) rows.push_back({"LTV:CAC", pystr(S_(ue, "ltv_cac")) + "x (LTV " + money(S_(ue, "ltv"), 2) + ", CAC " + money(S_(ue, "cac"), 2) + ")"});
        if (!S_(ue, "cac_payback_months").is_none()) rows.push_back({"CAC payback", pystr(S_(ue, "cac_payback_months")) + " months"});
    }
    if (truthy(fin)) {
        rows.push_back({"Revenue, years 1 to 3", money(S_(fin, "revenue_y1")) + " / " + money(S_(fin, "revenue_y2")) + " / " + money(S_(fin, "revenue_y3"))});
        rows.push_back({"Breakeven", truthy(pget(fin, "breakeven_month")) ? "month " + fmt_d(S_(fin, "breakeven_month")) + " (" + month_name(S_(fin, "breakeven_date")) + ")"
                                                                         : std::string("not inside the model")});
        if (truthy(pget(fin, "peak_burn"))) rows.push_back({"Peak burn", money(S_(fin, "peak_burn")) + " a month (month " + pystr(pget(fin, "peak_burn_month")) + ")"});
        rows.push_back({"Runway", runway_text(fin)});
    }
    if (!rows.empty()) {
        L.push_back("| Key number | |"); L.push_back("| --- | ---: |");
        for (const auto& [k, v] : rows) L.push_back("| " + md_escape(k) + " | " + md_escape(strip(v)) + " |");
        L.push_back("");
    }
    L.push_back("## Checks"); L.push_back("");
    if (flags.empty() && notes.empty()) { L.push_back("Every check passed: no empty sections, and the numbers agree between files."); L.push_back(""); }
    else {
        for (const auto& f : flags) L.push_back("- **Fix:** " + f);
        for (const auto& x : notes) L.push_back("- Note: " + x);
        L.push_back("");
    }
    for (std::size_t i = 0; i < secs.size(); ++i) {
        const Sec& s = secs[i];
        L.push_back("## " + std::to_string(i + 1) + ". " + pystr(s.title)); L.push_back("");
        L.push_back(!s.text.empty() ? s.text : "_Not written yet. Write it in seed/plan.json, then run plan.py build again._"); L.push_back("");
        if (!s.points.empty()) { for (const Val& p : s.points) L.push_back("- " + pystr(p)); L.push_back(""); }
        for (auto& x : section_data(s.key, plan, n)) L.push_back(std::move(x));
    }
    L.push_back("---"); L.push_back(""); L.push_back(std::string("_Every number in this plan comes from the files in seed/. ") + NOTE + "_"); L.push_back("");
    return join(L, "\n");
}

// ---------------------------------------------------------------- commands
inline Paths paths_for(const std::string& d) {
    Paths p;
    for (const char* k : {"config.json", "plan.json", "plan.md", "model.json", "market.json", "competitors.json"}) p[k] = join_path(d, k);
    return p;
}
inline Val str_list(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(sv(s)); return l; }

inline int cmd_init(const Val& a) {
    const Paths p = paths_for(S(a, "dir"));
    const bool force = B(a, "force");
    if (exists(p.at("plan.json")) && !force) {
        eprint("plan.py: " + p.at("plan.json") + " already exists. Edit it, or use --force to start again (that erases its sections).");
        return 1;
    }
    for (const auto& [flag, k] : std::vector<std::pair<std::string, std::string>>{{"--brand", "brand"}, {"--ink", "ink"}, {"--accent", "accent"}}) {
        const std::string v = S(a, k);
        if (!v.empty() && !re::match(HEX, v)) throw PlanError(flag + " wants a colour like #2F6FEB (got " + str_repr(v) + ")");
    }
    std::vector<std::string> wrote;
    Val cfg;
    bool kept;
    if (exists(p.at("config.json")) && !force) {
        cfg = *read_json(p.at("config.json"), "config");
        kept = true;
    } else {
        if (S(a, "name").empty() || S(a, "one_liner").empty()) throw PlanError("init needs --name and --one-liner (the startup's name and what it does, in one line)");
        const auto orq = [&](const char* k, const char* d) { const std::string v = S(a, k); return sv(v.empty() ? d : v); };
        Val st = obj({{"name", sv(S(a, "name"))}, {"one_liner", sv(S(a, "one_liner"))}, {"sector", orq("sector", "")}, {"stage", orq("stage", "seed")},
                      {"geography", orq("geography", "")}});
        st.d->set("raise", !S(a, "raise_").empty() ? fnum(parse_money(S(a, "raise_"))) : Val());
        st.d->set("instrument", orq("instrument", ""));
        const Val founder = obj({{"name", orq("founder", "")}, {"email", orq("email", "")}, {"company_address", orq("address", "")}});
        const bool ua = !S(a, "founder").empty() && !S(a, "email").empty();
        cfg = obj({{"startup", st}, {"founder", founder}, {"sec_user_agent", sv(ua ? S(a, "founder") + " " + S(a, "email") : "")},
                   {"outreach", obj({{"daily_cap", iv(20)}, {"followup_days", Val::list()}, {"max_touches", iv(3)}, {"business_days", Val::boolean(true)}})}});
        cfg.d->get("outreach")->d->get("followup_days")->l->push_back(iv(3));
        cfg.d->get("outreach")->d->get("followup_days")->l->push_back(iv(7));
        write_json(p.at("config.json"), cfg);
        wrote.push_back(p.at("config.json"));
        kept = false;
    }
    const Val founder = pgo(pgo(cfg, "founder", Val::dict()), "name", sv(""));
    const auto orq = [&](const char* k, const char* d) { const std::string v = S(a, k); return sv(v.empty() ? d : v); };
    Val secs = Val::list();
    for (const auto& [k, t] : SECTIONS) secs.l->push_back(obj({{"key", sv(k)}, {"title", sv(t)}, {"text", sv("")}, {"pitch", sv("")}, {"points", Val::list()}}));
    Val team = Val::list(); team.l->push_back(obj({{"name", founder}, {"role", sv("Founder")}, {"line", sv("")}}));
    const Val plan = obj({{"created_by", sv("seed-plan")}, {"tagline", orq("tagline", "")},
                          {"brand", obj({{"brand", orq("brand", "#2F6FEB")}, {"ink", orq("ink", "#0E1B33")}, {"accent", orq("accent", "#FFB020")}})},
                          {"sections", secs}, {"team", team}, {"numbers", Val::dict()}, {"flags", Val::list()}, {"notes", Val::list()}, {"built", Val()}});
    write_json(p.at("plan.json"), plan);
    wrote.push_back(p.at("plan.json"));
    for (const auto& w : wrote) print("Wrote " + w + ".");
    if (kept) print("Kept the existing " + p.at("config.json") + " (use --force to replace it).");
    if (!truthy(pget(pgo(cfg, "startup", Val::dict()), "raise"))) print("Note: config.json has no raise yet. Set startup.raise (a number in dollars).");
    print("Next: write the nine sections into " + p.at("plan.json") + " with the founder (text, a one-line pitch, up to 3 points), "
          "run /seed-scout, /seed-market and /seed-model, then plan.py build.");
    return 0;
}

struct Eval { Paths p; Val cfg, plan, n; std::vector<std::string> flags, notes; };
inline Eval evaluate(const Val& a) {
    const std::string d = S(a, "dir");
    const Paths p = paths_for(d);
    if (!is_dir(d)) throw PlanError("no " + d + "/ folder here: run plan.py init first");
    const Val cfg = *read_json(p.at("config.json"), "config (run plan.py init first)");
    const Val plan = *read_json(p.at("plan.json"), "plan (run plan.py init first)");
    const auto model = read_json(p.at("model.json"), "model", false);
    const auto market = read_json(p.at("market.json"), "market", false);
    const auto comps = read_json(p.at("competitors.json"), "competitors", false);
    const Val n = gather(cfg, model, market, comps);
    auto [flags, notes] = checks(cfg, plan, model, market, comps, n, p);
    return {p, cfg, plan, n, flags, notes};
}
inline void print_checks(const std::vector<std::string>& flags, const std::vector<std::string>& notes) {
    if (flags.empty() && notes.empty()) print("Every check passed: no empty sections, and the numbers agree between files.");
    for (const auto& f : flags) print("  FIX   " + f);
    for (const auto& x : notes) print("  note  " + x);
}
inline int cmd_build(const Val& a) {
    const long long today = parse_today(S(a, "today"));
    Eval e = evaluate(a);
    const std::string text = render(e.cfg, e.plan, e.n, e.flags, e.notes, today);
    write_file(e.p.at("plan.md"), text);
    const Val st = pgo(e.cfg, "startup", Val::dict()), fo = pgo(e.cfg, "founder", Val::dict());
    if (e.plan.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(e.plan) + "' object is not iterable");
    Val plan = Val::dict();
    for (const auto& [k, v] : e.plan.d->items) plan.d->set(k, v);
    Val stc = Val::dict();
    for (const char* k : {"name", "one_liner", "sector", "stage", "geography", "raise", "instrument"}) stc.d->set(k, pget(st, k));
    plan.d->set("startup", stc);
    plan.d->set("founder", obj({{"name", pgo(fo, "name", sv(""))}, {"email", pgo(fo, "email", sv(""))}}));
    plan.d->set("numbers", e.n);
    plan.d->set("flags", str_list(e.flags));
    plan.d->set("notes", str_list(e.notes));
    plan.d->set("built", sv(iso_day(today)));
    write_json(e.p.at("plan.json"), plan);
    long long filled = 0; for (const auto& s : section_list(plan)) if (!s.text.empty()) ++filled;
    if (B(a, "json")) {
        Val w = Val::list(); w.l->push_back(sv(e.p.at("plan.md"))); w.l->push_back(sv(e.p.at("plan.json")));
        print(dump2(obj({{"wrote", w}, {"sections_written", iv(filled)}, {"flags", str_list(e.flags)}, {"notes", str_list(e.notes)}, {"numbers", e.n}})));
        return 0;
    }
    print(pystr(pgo(st, "name", sv("Your startup"))) + ": business plan, " + std::to_string(filled) + " of 9 sections written.");
    const Val ue = pget(e.n, "unit_economics");
    if (truthy(ue)) {
        print("  Per " + ljust(pystr(S_(ue, "unit")), 8) + " price " + money(S_(ue, "price"), 2) + ", take " + pct(S_(ue, "take_rate_pct")) + " = " +
              money(S_(ue, "revenue_per_order"), 2) + ", contribution " + money(S_(ue, "contribution_per_order"), 2));
        if (!S_(ue, "ltv_cac").is_none()) print("  LTV:CAC      " + pystr(S_(ue, "ltv_cac")) + "x, CAC payback " + pystr(S_(ue, "cac_payback_months")) + " months");
    }
    print_checks(e.flags, e.notes);
    print("Wrote " + e.p.at("plan.md") + " and " + e.p.at("plan.json") + ".");
    print(e.flags.empty() ? "Next: /seed-deck turns plan.json into the 10-slide deck." : "Next: fix the items marked FIX, build again, then /seed-deck.");
    return 0;
}
inline int cmd_check(const Val& a) {
    const Eval e = evaluate(a);
    if (B(a, "json")) print(dump2(obj({{"ok", Val::boolean(e.flags.empty())}, {"flags", str_list(e.flags)}, {"notes", str_list(e.notes)}})));
    else print_checks(e.flags, e.notes);
    return e.flags.empty() ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("plan.py", "Business Planner (seed-plan): the idea, turned into a business plan.");
    p.opt({"--dir"}).def("seed").help("the seed/ folder (default ./seed)");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("init"); s.help_line = "write seed/config.json and the plan.json skeleton";
      s.opt({"--name"}); s.opt({"--one-liner"});
      s.opt({"--tagline"}).help("the short line for the deck's title slide");
      s.opt({"--sector"}); s.opt({"--stage"}).def("seed"); s.opt({"--geography"});
      s.opt({"--raise"}).dest("raise_").metavar("AMOUNT").help("1500000, $1.5M or 750k");
      s.opt({"--instrument"}).help("e.g. \"post-money SAFE\" or \"priced equity\"");
      s.opt({"--founder"}).help("the founder's name");
      s.opt({"--email"}).help("the founder's own work email (outreach goes from it)");
      s.opt({"--address"}).help("the company's mailing address (outreach emails carry it)");
      s.opt({"--brand"}).help("brand colour, #RRGGBB"); s.opt({"--ink"}).help("text colour, #RRGGBB"); s.opt({"--accent"}).help("accent colour, #RRGGBB");
      s.opt({"--force"}).store_true().help("replace an existing config.json and plan.json");
      s.opt({"--dir"}).no_default(); }
    { Parser& s = p.sub("build"); s.help_line = "write seed/plan.md and the numbers into plan.json";
      s.opt({"--today"}).help("the build date, YYYY-MM-DD (default today)"); s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); }
    { Parser& s = p.sub("check"); s.help_line = "the checks only"; s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "build") return cmd_build(a);
        return cmd_check(a);
    } catch (const PlanError& e) { eprint(std::string("plan.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-plan/plan", main);
}  // namespace packtools::seed_plan
