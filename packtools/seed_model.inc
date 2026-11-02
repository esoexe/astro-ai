// seed-model/model: the 36-month financial model behind the raise, in exact decimals. A C++ port of
// seed-skill/seed-model/model.py (MIT, Jake Schincariol); same numbers to the cent, same output and exit codes:
// 0 done, 1 refused (funds that do not add up, a file already there), 2 bad input.
#include <optional>
namespace packtools::seed_model {
using namespace pyrt;
using namespace packtools::seed;

struct ModelError : std::runtime_error {
    int code;
    explicit ModelError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {}
};

constexpr int MONTHS = 36;
inline const Decimal CENT = Decimal::parse("0.01");
inline const Decimal ONE = Decimal(1);
inline const char* const NOTE = "Projections from your own assumptions: a plan, not a forecast. Not investment advice.";
inline const char* const MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
inline const std::vector<std::string> CSV_FIELDS = {"month", "date", "new_customers", "churned", "customers", "orders", "gmv", "revenue",
                                                    "cost_of_revenue", "gross_profit", "payroll", "marketing", "other_opex", "opex", "net", "cash"};
inline bool csv_money(const std::string& k) { return std::find(CSV_FIELDS.begin() + 6, CSV_FIELDS.end(), k) != CSV_FIELDS.end(); }

// ---------------------------------------------------------------- numbers
inline Decimal D(const Val& x, const std::string& what = "a number") {
    if (x.t == Val::BOOL || x.is_none()) throw ModelError(what + " is not a number: " + repr(x));
    try {
        if (x.t == Val::FLOAT) return Decimal::parse(float_repr(x.f));   // Decimal(repr(x))
        std::string s = strip(pystr(x));
        const std::size_t k = s.find_first_not_of('$');
        s = k == std::string::npos ? "" : s.substr(k);
        std::string t; for (char c : s) if (c != ',') t += c;
        return Decimal::parse(ascii_digits(t));
    } catch (const DecimalError&) {
        throw ModelError(what + " is not a number: " + repr(x));
    }
}
inline Decimal dsum0() { return Decimal(0); }
inline Decimal cents(const Decimal& x) { return x.quantize(CENT, Rounding::HALF_UP); }
inline BigInt half_up(const Decimal& x) { return x.quantize(ONE, Rounding::HALF_UP).to_int(); }
inline std::string money(const Decimal& x, int dp = 0) {
    const Decimal q = x.quantize(dp ? CENT : ONE, Rounding::HALF_UP);
    return std::string(q < Decimal(0) ? "-" : "") + "$" + q.abs().format(",f");
}
inline std::string money(const Val& x, int dp = 0) { return money(D(x), dp); }
inline Val fnum(const Decimal& d) {   // a whole number as int, otherwise a float
    if (d == d.to_integral_value()) return int_val(d.to_int());
    return Val::flt(d.to_double());
}
inline long long small_int(const Decimal& d) {   // int(d) for a month number already range-checked (or about to be refused)
    const BigInt b = d.to_int();
    return b.fits_ll() ? b.to_ll() : 0;
}
inline std::pair<std::string, std::string> month_label(long long y, long long mo, long long m) {
    const long long idx = (mo - 1) + (m - 1), yy = y + idx / 12, mm = idx % 12 + 1;
    char a[64], b[64];
    std::snprintf(a, sizeof a, "%04lld-%02lld", yy, mm);
    std::snprintf(b, sizeof b, "%s %lld", MONTH_NAMES[mm - 1], yy);
    return {a, b};
}
inline std::string capitalize(const std::string& s) {   // str.capitalize(): the first character in title case, the rest lower
    std::string o; const auto v = ucase_detail::u32(s);
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == 0) ucase_detail::map_full(o, v[0], ucase_detail::TI);
        else ucase_detail::lower_at(o, v, i);
    }
    return o;
}
inline bool py_eq(const Val& a, const Val& b) {   // a == b for JSON values (1 == 1.0 == True)
    auto numeric = [](const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; };
    if (numeric(a) && numeric(b)) {
        if ((a.t == Val::FLOAT && !std::isfinite(a.f)) || (b.t == Val::FLOAT && !std::isfinite(b.f)))
            return a.t == Val::FLOAT && b.t == Val::FLOAT && a.f == b.f;
        return cmp(dec_of(a), dec_of(b)) == 0;
    }
    if (a.t != b.t) return false;
    switch (a.t) {
        case Val::NONE: return true;
        case Val::STR: return a.s == b.s;
        case Val::LIST:
            if (a.l->size() != b.l->size()) return false;
            for (std::size_t k = 0; k < a.l->size(); ++k) if (!py_eq((*a.l)[k], (*b.l)[k])) return false;
            return true;
        default:
            if (a.d->items.size() != b.d->items.size()) return false;
            for (const auto& [k, v] : a.d->items) { const Val* w = b.d->get(k); if (!w || !py_eq(v, *w)) return false; }
            return true;
    }
}
inline List py_iter(const Val& v) {   // for x in (v or []): a dict gives its keys, a str its characters
    if (!truthy(v)) return {};
    if (v.t == Val::LIST) return *v.l;
    List o;
    if (v.t == Val::DICT) { for (const auto& kv : v.d->items) o.push_back(sv(kv.first)); return o; }
    if (v.t == Val::STR) { for (const std::string& c : cps(v.s)) o.push_back(sv(c)); return o; }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
}
inline Val item(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) throw PyErr("TypeError", o.t == Val::LIST ? "list indices must be integers or slices, not str" : std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* v = o.d->get(k);
    if (!v) throw PyErr("KeyError", str_repr(k));
    return *v;
}

// ---------------------------------------------------------------- assumptions
inline Val read_json(const std::string& path, const std::string& what) {
    if (!exists(path)) throw ModelError("no " + what + " at " + path);
    try {
        return loads(read_text(path));
    } catch (const OSError&) {
        throw;
    } catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw ModelError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline Val clean(const Val& doc) {   // the assumptions without the _about style notes
    if (doc.t == Val::DICT) {
        Val o = Val::dict();
        for (const auto& [k, v] : doc.d->items) if (!starts(k, "_")) o.d->set(k, clean(v));
        return o;
    }
    if (doc.t == Val::LIST) { Val o = Val::list(); for (const Val& v : *doc.l) o.l->push_back(clean(v)); return o; }
    return doc;
}

struct Cost { std::string label; Decimal pct, fixed; };
struct Role { std::string role; Decimal monthly, count; long long start, end; };
struct Opex { std::string label; Decimal monthly; long long start; };
struct Once { std::string label; long long month; Decimal cost; };
struct A {
    std::string company, currency, unit, units;
    long long sy = 2027, smo = 1;
    Decimal price, take, cash_at_start, raise_, cac, overhead, starting, new1, churn, opc;
    long long raise_month = 1;
    std::vector<Decimal> growth;
    std::vector<Cost> costs; std::vector<Role> payroll; std::vector<Opex> opex; std::vector<Once> once;
    std::vector<std::pair<std::string, Decimal>> funds;
};
using OD = std::optional<Decimal>;

inline std::string str_or(const Val& v, const std::string& dflt) { return truthy(v) ? pystr(v) : dflt; }   // str(x or default)

inline A validate(const Val& doc) {   // every input as a Decimal; refuses listing every problem
    if (doc.t != Val::DICT) throw ModelError("the assumptions must be a JSON object");
    std::vector<std::string> errs;
    auto num = [&](const Val& obj, const std::string& key, const std::string& where, OD dflt = std::nullopt, OD lo = Decimal(0), OD hi = std::nullopt, bool required = true) -> OD {
        const Val* p = obj.d->get(key);
        if (!p || p->is_none()) {
            if (required && !dflt) { errs.push_back(where + key + " is missing"); return Decimal(0); }
            return dflt;
        }
        Decimal v;
        try { v = D(*p, where + key); } catch (const ModelError& e) { errs.push_back(e.what()); return Decimal(0); }
        if (lo && v < *lo) errs.push_back(where + key + " must be " + lo->str() + " or more (got " + pystr(*p) + ")");
        if (hi && v > *hi) errs.push_back(where + key + " must be " + hi->str() + " or less (got " + pystr(*p) + ")");
        return v;
    };
    A a;
    a.company = str_or(get(doc, "company"), "Your startup");
    a.currency = str_or(get(doc, "currency"), "USD");
    a.unit = str_or(get(doc, "unit"), "order");
    a.units = str_or(get(doc, "units"), a.unit + "s");
    const std::string sm = str_or(get(doc, "start_month"), "");
    const re::Match m = re::match(R"(^(\d{4})-(\d{2})$)", sm);
    if (!m || std::stoi(ascii_digits(m.group(2))) < 1 || std::stoi(ascii_digits(m.group(2))) > 12) {
        errs.push_back("start_month wants YYYY-MM, like 2027-01 (got " + repr(get(doc, "start_month")) + ")");
    } else {
        a.sy = std::stoll(ascii_digits(m.group(1))); a.smo = std::stoll(ascii_digits(m.group(2)));
    }
    const Decimal Z(0), HUNDRED(100), MAXM(MONTHS);
    a.price = *num(doc, "price", "", std::nullopt, std::nullopt);
    if (!get(doc, "price").is_none() && a.price <= Z) {
        bool named = false; for (const std::string& e : errs) if (starts(e, "price ")) named = true;
        if (!named) errs.push_back("price must be above 0 (got " + pystr(get(doc, "price")) + ")");
    }
    a.take = *num(doc, "take_rate_pct", "", std::nullopt, Z, HUNDRED);
    a.cash_at_start = *num(doc, "cash_at_start", "", Decimal(0), std::nullopt);
    a.raise_ = *num(doc, "raise", "");
    const OD rm = num(doc, "raise_month", "", Decimal(1), Decimal(1), MAXM);
    a.raise_month = rm ? small_int(*rm) : 1;
    a.cac = *num(doc, "cac", "");
    a.overhead = *num(doc, "payroll_overhead_pct", "", Decimal(0), Z, Decimal(200));
    Val cust = get(doc, "customers");
    if (cust.t != Val::DICT) {
        errs.push_back("customers is missing: {starting, new_month_1, new_growth_pct, churn_monthly_pct, orders_per_customer_month}");
        cust = obj({{"new_month_1", iv(0)}, {"churn_monthly_pct", iv(0)}, {"orders_per_customer_month", iv(0)}});
    }
    a.starting = *num(cust, "starting", "customers.", Decimal(0));
    a.new1 = *num(cust, "new_month_1", "customers.");
    a.churn = *num(cust, "churn_monthly_pct", "customers.");
    if (a.churn >= HUNDRED) errs.push_back("customers.churn_monthly_pct must be under 100 (got " + pystr(get(cust, "churn_monthly_pct")) + ")");
    a.opc = *num(cust, "orders_per_customer_month", "customers.");
    Val g = cust.d->get("new_growth_pct") ? *cust.d->get("new_growth_pct") : iv(0);
    List gl = g.t == Val::LIST ? *g.l : List{g};
    if (gl.empty() || gl.size() > 3) { errs.push_back("customers.new_growth_pct wants one number or one a year (up to 3)"); gl = {iv(0)}; }
    for (std::size_t i = 0; i < gl.size(); ++i) {
        const std::string w = "customers.new_growth_pct[" + std::to_string(i) + "]";
        try {
            const Decimal gv = D(gl[i], w);
            if (gv <= Decimal(-100)) errs.push_back(w + " must be above -100");
            a.growth.push_back(gv);
        } catch (const ModelError& e) { errs.push_back(e.what()); a.growth.push_back(Decimal(0)); }
    }
    while (a.growth.size() < 3) a.growth.push_back(a.growth.back());
    {
        const List rows = py_iter(get(doc, "cost_per_order"));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Val& it = rows[i]; const std::string n = std::to_string(i);
            if (it.t != Val::DICT) { errs.push_back("cost_per_order[" + n + "] must be an object"); continue; }
            const std::string w = "cost_per_order[" + n + "].";
            const Decimal pct = *num(it, "pct_of_price", w, Decimal(0), Z, HUNDRED);
            const Decimal fixed = *num(it, "fixed", w, Decimal(0));
            a.costs.push_back({str_or(get(it, "label"), "cost " + std::to_string(i + 1)), pct, fixed});
        }
    }
    {
        const List rows = py_iter(get(doc, "payroll"));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Val& it = rows[i]; const std::string n = std::to_string(i);
            if (it.t != Val::DICT) { errs.push_back("payroll[" + n + "] must be an object"); continue; }
            const std::string w = "payroll[" + n + "].";
            const Decimal start = *num(it, "start_month", w, Decimal(1), Decimal(1), MAXM);
            const OD end = num(it, "end_month", w, std::nullopt, Decimal(1), MAXM, false);
            Role r; r.role = str_or(get(it, "role"), "role " + std::to_string(i + 1));
            r.monthly = *num(it, "monthly", w);
            r.count = *num(it, "count", w, Decimal(1));
            r.start = small_int(start); r.end = end ? small_int(*end) : MONTHS;
            a.payroll.push_back(r);
        }
    }
    {
        const List rows = py_iter(get(doc, "opex"));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Val& it = rows[i]; const std::string n = std::to_string(i);
            if (it.t != Val::DICT) { errs.push_back("opex[" + n + "] must be an object"); continue; }
            const std::string w = "opex[" + n + "].";
            const Decimal start = *num(it, "start_month", w, Decimal(1), Decimal(1), MAXM);
            Opex o; o.label = str_or(get(it, "label"), "opex " + std::to_string(i + 1));
            o.monthly = *num(it, "monthly", w); o.start = small_int(start);
            a.opex.push_back(o);
        }
    }
    {
        const List rows = py_iter(get(doc, "one_off"));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Val& it = rows[i]; const std::string n = std::to_string(i);
            if (it.t != Val::DICT) { errs.push_back("one_off[" + n + "] must be an object"); continue; }
            const std::string w = "one_off[" + n + "].";
            const Decimal mo = *num(it, "month", w, std::nullopt, Decimal(1), MAXM);
            Once o; o.label = str_or(get(it, "label"), "one-off " + std::to_string(i + 1));
            o.month = small_int(mo); o.cost = *num(it, "cost", w);
            a.once.push_back(o);
        }
    }
    Val uof = get(doc, "use_of_funds");
    if (!truthy(uof)) uof = Val::dict();
    if (uof.t != Val::DICT) { errs.push_back("use_of_funds must be an object of {what: amount}"); uof = Val::dict(); }
    for (const auto& [k, v] : uof.d->items) {
        const std::string w = "use_of_funds[" + str_repr(k) + "]";
        try {
            const Decimal fv = D(v, w);
            if (fv <= Z) errs.push_back(w + " must be above 0");
            a.funds.push_back({k, fv});
        } catch (const ModelError& e) { errs.push_back(e.what()); }
    }
    if (!errs.empty()) throw ModelError("the assumptions need fixing:\n  - " + join(errs, "\n  - "));
    return a;
}

// ---------------------------------------------------------------- the model
inline Val unit_economics(const A& a) {   // per order and per customer, exact
    const Decimal H(100);
    const Decimal rev = a.price * a.take / H;
    Decimal cost(0); for (const Cost& c : a.costs) cost = cost + (a.price * c.pct / H + c.fixed);
    const Decimal contrib = rev - cost, per_cust = contrib * a.opc, churn = a.churn / H;
    const OD ltv = churn > Decimal(0) ? OD(per_cust / churn) : std::nullopt;
    const OD ltv_cac = ltv && a.cac > Decimal(0) ? OD(*ltv / a.cac) : std::nullopt;
    const OD payback = per_cust > Decimal(0) ? OD(a.cac / per_cust) : std::nullopt;
    return obj({{"unit", sv(a.unit)}, {"price", fnum(a.price)}, {"take_rate_pct", fnum(a.take)}, {"revenue_per_order", fnum(rev)},
                {"cost_per_order", fnum(cost)}, {"contribution_per_order", fnum(contrib)}, {"orders_per_customer_month", fnum(a.opc)},
                {"contribution_per_customer_month", fnum(per_cust)}, {"churn_monthly_pct", fnum(a.churn)}, {"cac", fnum(a.cac)},
                {"ltv", ltv ? fnum(cents(*ltv)) : Val()}, {"ltv_cac", ltv_cac ? fnum(ltv_cac->quantize(CENT, Rounding::HALF_UP)) : Val()},
                {"cac_payback_months", payback ? fnum(payback->quantize(CENT, Rounding::HALF_UP)) : Val()}});
}

struct Row { long long month; std::string date; BigInt new_customers, churned, customers, orders; Decimal gmv, revenue, cor, gp, payroll, marketing, other, opex, net, cash; };

inline Val run_model(const Val& doc) {
    const A a = validate(doc);
    const Decimal H(100);
    std::vector<Row> rows;
    BigInt customers = half_up(a.starting);
    Decimal cash = a.cash_at_start, new_f = a.new1;
    for (long long m = 1; m <= MONTHS; ++m) {
        if (m > 1) new_f = new_f * (Decimal(1) + a.growth[static_cast<std::size_t>((m - 1) / 12)] / H);
        const BigInt nw = half_up(new_f);
        const BigInt churned = half_up(Decimal::from_int(customers) * a.churn / H);
        customers = customers - churned + nw;
        const BigInt orders = half_up(Decimal::from_int(customers) * a.opc);
        const Decimal od = Decimal::from_int(orders);
        const Decimal gmv = od * a.price;
        const Decimal revenue = cents(gmv * a.take / H);
        Decimal cs(0); for (const Cost& c : a.costs) cs = cs + od * (a.price * c.pct / H + c.fixed);
        const Decimal cor = cents(cs);
        const Decimal gp = revenue - cor;
        Decimal base(0); for (const Role& r : a.payroll) if (r.start <= m && m <= r.end) base = base + r.monthly * r.count;
        const Decimal payroll = cents(base * (Decimal(1) + a.overhead / H));
        const Decimal marketing = cents(Decimal::from_int(nw) * a.cac);
        Decimal ox(0); for (const Opex& o : a.opex) if (o.start <= m) ox = ox + o.monthly;
        Decimal oo(0); for (const Once& o : a.once) if (o.month == m) oo = oo + o.cost;
        const Decimal other = cents(ox + oo);
        const Decimal opex = payroll + marketing + other;
        const Decimal net = gp - opex;
        cash = cash + net + (m == a.raise_month ? a.raise_ : Decimal(0));
        rows.push_back({m, month_label(a.sy, a.smo, m).first, nw, churned, customers, orders, gmv, revenue, cor, gp, payroll, marketing, other, opex, net, cash});
    }
    auto year = [&](Decimal Row::*key, int y) { Decimal s(0); for (int i = (y - 1) * 12; i < y * 12; ++i) s = s + rows[static_cast<std::size_t>(i)].*key; return s; };
    long long breakeven = 0;
    for (int i = MONTHS - 1; i >= 0; --i) { if (rows[static_cast<std::size_t>(i)].net >= Decimal(0)) breakeven = i + 1; else break; }
    std::size_t worst = 0, low = 0;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].net < rows[worst].net) worst = i;
        if (rows[i].cash < rows[low].cash) low = i;
    }
    const Decimal peak_burn = rows[worst].net < Decimal(0) ? -rows[worst].net : Decimal(0);
    long long cash_out = 0; for (const Row& r : rows) if (r.cash < Decimal(0)) { cash_out = r.month; break; }
    Decimal funds_total(0); for (const auto& f : a.funds) funds_total = funds_total + f.second;
    Val uof = Val::dict(); for (const auto& [k, v] : a.funds) uof.d->set(k, fnum(v));
    const Val summary = obj({{"company", sv(a.company)}, {"currency", sv(a.currency)}, {"unit", sv(a.unit)}, {"months", iv(MONTHS)},
        {"start_month", sv(rows[0].date)}, {"end_month", sv(rows.back().date)},
        {"revenue_y1", fnum(year(&Row::revenue, 1))}, {"revenue_y2", fnum(year(&Row::revenue, 2))}, {"revenue_y3", fnum(year(&Row::revenue, 3))},
        {"gmv_y1", fnum(year(&Row::gmv, 1))}, {"gmv_y2", fnum(year(&Row::gmv, 2))}, {"gmv_y3", fnum(year(&Row::gmv, 3))},
        {"net_y1", fnum(year(&Row::net, 1))}, {"net_y2", fnum(year(&Row::net, 2))}, {"net_y3", fnum(year(&Row::net, 3))},
        {"breakeven_month", breakeven ? iv(breakeven) : Val()},
        {"breakeven_date", breakeven ? sv(rows[static_cast<std::size_t>(breakeven - 1)].date) : Val()},
        {"peak_burn", fnum(peak_burn)}, {"peak_burn_month", peak_burn > Decimal(0) ? iv(rows[worst].month) : Val()},
        {"runway_months", iv(cash_out ? cash_out - 1 : MONTHS)}, {"cash_out_month", cash_out ? iv(cash_out) : Val()},
        {"cash_low", fnum(rows[low].cash)}, {"cash_low_month", iv(rows[low].month)}, {"cash_end", fnum(rows.back().cash)},
        {"customers_end", int_val(rows.back().customers)},
        {"raise", fnum(a.raise_)}, {"raise_month", iv(a.raise_month)},
        {"use_of_funds", uof}, {"use_of_funds_total", fnum(funds_total)},
        {"funds_match", bv(!a.funds.empty() && funds_total == a.raise_)}});
    Val months = Val::list();
    for (const Row& r : rows)
        months.l->push_back(obj({{"month", iv(r.month)}, {"date", sv(r.date)}, {"new_customers", int_val(r.new_customers)}, {"churned", int_val(r.churned)},
                                 {"customers", int_val(r.customers)}, {"orders", int_val(r.orders)}, {"gmv", fnum(r.gmv)}, {"revenue", fnum(r.revenue)},
                                 {"cost_of_revenue", fnum(r.cor)}, {"gross_profit", fnum(r.gp)}, {"payroll", fnum(r.payroll)},
                                 {"marketing", fnum(r.marketing)}, {"other_opex", fnum(r.other)}, {"opex", fnum(r.opex)}, {"net", fnum(r.net)},
                                 {"cash", fnum(r.cash)}}));
    return obj({{"created_by", sv("seed-model")}, {"assumptions", clean(doc)}, {"months", months}, {"summary", summary},
                {"unit_economics", unit_economics(a)}, {"note", sv(NOTE)}});
}

inline std::pair<bool, std::vector<std::string>> check_funds(const Val& doc) {   // the use of funds must add up to the raise, to the cent
    const A a = validate(doc);
    std::vector<std::string> lines = {"Use of funds for the " + money(a.raise_) + " raise"};
    if (a.funds.empty()) {
        lines.push_back("  (none yet) Add use_of_funds to the assumptions: {\"what\": amount}, adding up to the raise.");
        return {false, lines};
    }
    Decimal total(0); for (const auto& f : a.funds) total = total + f.second;
    std::size_t width = 5; for (const auto& f : a.funds) width = std::max(width, cplen(f.first));
    const bool has_raise = !a.raise_.is_zero();
    auto pct_of = [&](const Decimal& v) { return has_raise ? (v / a.raise_ * Decimal(100)).quantize(Decimal::parse("0.1"), Rounding::HALF_UP) : Decimal(0); };
    auto line = [&](const std::string& k, const Decimal& v) { return "  " + ljust(k, width) + "  " + rjust(money(v), 12) + "  " + rjust(pct_of(v).str(), 5) + "%"; };
    for (const auto& [k, v] : a.funds) lines.push_back(line(k, v));
    lines.push_back(line("Total", total));
    if (a.raise_ == Decimal(0)) { lines.push_back("The raise is $0: set raise in the assumptions."); return {false, lines}; }
    if (total == a.raise_) { lines.push_back("Adds up to the raise."); return {true, lines}; }
    const Decimal gap = a.raise_ - total;
    const int dp = (gap % Decimal(1)).is_zero() ? 0 : 2;
    if (gap > Decimal(0)) {
        lines.push_back("Short by " + money(gap, dp) + ": the use of funds adds up to " + money(total) + ", the raise is " + money(a.raise_) + ". Every dollar of the raise needs a job.");
        return {false, lines};
    }
    lines.push_back("Over by " + money(-gap, dp) + ": the use of funds adds up to " + money(total) + ", more than the " + money(a.raise_) + " raise.");
    return {false, lines};
}

// ---------------------------------------------------------------- output
inline std::string d_pct(long long v, int w) { char b[64]; std::snprintf(b, sizeof b, "%*lld", w, v); return b; }
inline std::vector<std::string> describe(const Val& model, bool show_months) {
    const Val s = get(model, "summary"), ue = get(model, "unit_economics"), a = get(model, "assumptions");
    const std::vector<std::string> sm = split(get(s, "start_month").s, "-");
    const long long sy = std::stoll(sm[0]), smo = std::stoll(sm[1]);
    const std::string unit = get(s, "unit").s;
    const Val uv = get(a, "units");
    if (truthy(uv) && uv.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(uv) + "' object has no attribute 'capitalize'");
    const std::string units = truthy(uv) ? uv.s : unit + "s";
    auto lab = [&](long long m) { return month_label(sy, smo, m).second; };
    const long long nm = get(s, "months").i;
    std::vector<std::string> out = {pystr(get(s, "company")) + ": " + std::to_string(nm) + "-month model, " + lab(1) + " to " + lab(nm) + " (" + pystr(get(s, "currency")) + ")"};
    const List& ms = *get(model, "months").l;
    auto yr = [&](int i) { return List(ms.begin() + i * 12, ms.begin() + (i + 1) * 12); };
    auto row = [](const std::string& name, const std::vector<std::string>& vals) { std::string o = "  " + ljust(name, 18); for (const auto& v : vals) o += rjust(v, 15); return o; };
    out.push_back(row("", {"Year 1", "Year 2", "Year 3"}));
    std::vector<std::string> v;
    for (int i = 0; i < 3; ++i) { BigInt t(0); for (const Val& r : yr(i)) t = t + big_of(get(r, "orders")); v.push_back(commas(t)); }
    out.push_back(row(capitalize(units), v));
    static const std::pair<const char*, const char*> KEYS[] = {{"gmv", "GMV"}, {"revenue", "Revenue"}, {"gross_profit", "Gross profit"}, {"opex", "Opex"}, {"net", "Net"}};
    for (const auto& [key, name] : KEYS) {
        v.clear();
        for (int i = 0; i < 3; ++i) { Decimal t(0); for (const Val& r : yr(i)) t = t + D(get(r, key)); v.push_back(money(t)); }
        out.push_back(row(name, v));
    }
    v.clear(); for (int i = 0; i < 3; ++i) v.push_back(money(get(yr(i).back(), "cash")));
    out.push_back(row("Cash at year end", v));
    v.clear(); for (int i = 0; i < 3; ++i) v.push_back(commas(big_of(get(yr(i).back(), "customers"))));
    out.push_back(row("Customers (end)", v));
    out.push_back("");
    out.push_back("  Per " + ljust(unit, 14) + " price " + money(get(ue, "price"), 2) + ", you keep " + money(get(ue, "revenue_per_order"), 2) + " (" +
                  pystr(get(ue, "take_rate_pct")) + "%), costs " + money(get(ue, "cost_per_order"), 2) + ", contribution " + money(get(ue, "contribution_per_order"), 2));
    if (!get(ue, "ltv_cac").is_none())
        out.push_back("  " + ljust("Per customer", 18) + " LTV " + money(get(ue, "ltv"), 2) + " on a " + money(get(ue, "cac"), 2) + " CAC: LTV:CAC " +
                      pystr(get(ue, "ltv_cac")) + "x, CAC paid back in " + pystr(get(ue, "cac_payback_months")) + " months");
    if (truthy(get(s, "breakeven_month"))) {
        const long long b = get(s, "breakeven_month").i;
        out.push_back("  " + ljust("Breakeven", 18) + " month " + std::to_string(b) + " (" + lab(b) + "), and every month after it");
    } else {
        out.push_back("  " + ljust("Breakeven", 18) + " not within " + std::to_string(nm) + " months");
    }
    if (truthy(get(s, "peak_burn_month"))) {
        const long long p = get(s, "peak_burn_month").i;
        out.push_back("  " + ljust("Peak burn", 18) + " " + money(get(s, "peak_burn")) + " in month " + std::to_string(p) + " (" + lab(p) + ")");
    } else {
        out.push_back("  " + ljust("Peak burn", 18) + " none: every month makes money");
    }
    if (truthy(get(s, "cash_out_month"))) {
        const long long c = get(s, "cash_out_month").i;
        out.push_back("  " + ljust("Runway", 18) + " " + std::to_string(get(s, "runway_months").i) + " months: the cash runs out in month " + std::to_string(c) + " (" + lab(c) + "). Raise more or cut the plan.");
    } else {
        out.push_back("  " + ljust("Runway", 18) + " the cash lasts all " + std::to_string(nm) + " months (low point " + money(get(s, "cash_low")) + " in month " + std::to_string(get(s, "cash_low_month").i) + ")");
    }
    if (truthy(get(s, "funds_match"))) out.push_back("  " + ljust("Use of funds", 18) + " adds up to the " + money(get(s, "raise")) + " raise");
    else if (truthy(get(s, "use_of_funds"))) out.push_back("  " + ljust("Use of funds", 18) + " adds up to " + money(get(s, "use_of_funds_total")) + ", not the " + money(get(s, "raise")) + " raise: run model.py funds");
    else out.push_back("  " + ljust("Use of funds", 18) + " not set yet: add use_of_funds, then run model.py funds");
    if (show_months) {
        out.push_back("");
        out.push_back("  " + rjust("month", 5) + " " + rjust("date", 8) + " " + rjust("customers", 9) + " " + rjust(cpslice(units, 0, 9), 9) + " " + rjust("GMV", 12) + " " +
                      rjust("revenue", 11) + " " + rjust("gross profit", 12) + " " + rjust("opex", 11) + " " + rjust("net", 11) + " " + rjust("cash", 13));
        for (const Val& r : ms)
            out.push_back("  " + d_pct(get(r, "month").i, 5) + " " + rjust(get(r, "date").s, 8) + " " + rjust(commas(big_of(get(r, "customers"))), 9) + " " +
                          rjust(commas(big_of(get(r, "orders"))), 9) + " " + rjust(money(get(r, "gmv")), 12) + " " + rjust(money(get(r, "revenue")), 11) + " " +
                          rjust(money(get(r, "gross_profit")), 12) + " " + rjust(money(get(r, "opex")), 11) + " " + rjust(money(get(r, "net")), 11) + " " +
                          rjust(money(get(r, "cash")), 13));
    }
    out.push_back(NOTE);
    return out;
}

inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2;
    write_text(path, dumps(doc, o) + "\n");
}
inline std::string csv_cell(const std::string& s) {   // csv.writer QUOTE_MINIMAL
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string o = "\""; for (char c : s) { if (c == '"') o += '"'; o += c; } return o + "\"";
}
inline std::string to_csv(const Val& model) {
    std::string out = join(CSV_FIELDS, ",") + "\n";
    const Val ms = get(model, "months");
    for (const Val& r : *ms.l) {
        std::vector<std::string> cells;
        for (const std::string& k : CSV_FIELDS) {
            const Val v = item(r, k);
            if (csv_money(k)) cells.push_back(cents(D(v)).format("f"));
            else cells.push_back(csv_cell(v.is_none() ? "" : pystr(v)));
        }
        out += join(cells, ",") + "\n";
    }
    return out;
}

// ---------------------------------------------------------------- commands
inline std::string assumptions_path(const Val& a) { return truthy(get(a, "assumptions")) ? S(a, "assumptions") : join_path(S(a, "dir"), "assumptions.json"); }

inline int cmd_init(const Val& a) {
    const std::string dest = assumptions_path(a);
    if (exists(dest) && !truthy(get(a, "force"))) {
        eprint("model.py: " + dest + " already exists. Edit it, or use --force to start again from the example.");
        return 1;
    }
    const Val doc = read_json(abspath(packtools::skill_file("assumptions.example.json")), "example assumptions");
    write_json(dest, doc);
    print("Wrote " + dest + " (the Leashly example: replace every number with yours).");
    print("Next: edit it with the founder, then model.py run --write.");
    return 0;
}
inline int cmd_run(const Val& a) {
    const Val model = run_model(read_json(assumptions_path(a), "assumptions (run model.py init first)"));
    const std::string out = join_path(S(a, "dir"), "model.json");
    if (truthy(get(a, "write"))) write_json(out, model);
    if (truthy(get(a, "json"))) {
        Val doc = model;
        if (!truthy(get(a, "months"))) { doc = Val::dict(); for (const auto& [k, v] : model.d->items) if (k != "months") doc.d->set(k, v); }
        DumpOpts o; o.indent = 2;
        print(dumps(doc, o));
        return 0;
    }
    for (const std::string& line : describe(model, truthy(get(a, "months")))) print(line);
    if (truthy(get(a, "write"))) {
        print("Wrote " + out + ".");
        print("Next: /seed-plan pulls these numbers into the business plan, then /seed-deck.");
    } else {
        print("Nothing written. Add --write to save " + out + ".");
    }
    return 0;
}
inline int cmd_funds(const Val& a) {
    const auto [ok, lines] = check_funds(read_json(assumptions_path(a), "assumptions (run model.py init first)"));
    if (truthy(get(a, "json"))) {
        Val l = Val::list(); for (const auto& s : lines) l.l->push_back(sv(s));
        DumpOpts o; o.indent = 2;
        print(dumps(obj({{"ok", bv(ok)}, {"lines", l}}), o));
    } else {
        for (const auto& s : lines) print(s);
    }
    return ok ? 0 : 1;
}
inline int cmd_csv(const Val& a) {
    const std::string src = join_path(S(a, "dir"), "model.json");
    const Val model = read_json(src, "model (run model.py run --write first)");
    if (model.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(model) + "' object has no attribute 'get'");
    const Val ms = get(model, "months");
    if (ms.t != Val::LIST || ms.l->size() != MONTHS) throw ModelError(src + " has no 36 months: run model.py run --write again");
    const std::string text = to_csv(model);
    const std::string ap = assumptions_path(a);
    bool stale = false;
    if (exists(ap)) {
        try { stale = !py_eq(clean(read_json(ap, "assumptions")), get(model, "assumptions")); }
        catch (const ModelError&) { stale = false; }
    }
    if (S(a, "out") == "-") { flush(); std::fwrite(text.data(), 1, text.size(), stdout); return 0; }
    const std::string out = truthy(get(a, "out")) ? S(a, "out") : join_path(S(a, "dir"), "model.csv");
    const std::string d = dirname(out);
    if (!d.empty()) makedirs(d);
    write_text(out, text);
    print("Wrote " + out + " (" + std::to_string(MONTHS) + " months, " + std::to_string(CSV_FIELDS.size()) + " columns).");
    if (stale) print("Note: " + ap + " changed since " + src + " was written. Run model.py run --write to refresh it.");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("model.py", "Financial Modeler (seed-model): the 36-month model behind the raise.");
    p.opt({"--dir"}).def("seed").help("the seed/ folder (default ./seed)");
    p.opt({"--assumptions"}).metavar("PATH").help("default <dir>/assumptions.json");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("init"); s.help_line = "copy the example assumptions into seed/assumptions.json"; s.opt({"--force"}).store_true().help("overwrite an existing assumptions.json");
      s.opt({"--dir"}).no_default(); s.opt({"--assumptions"}).metavar("PATH").no_default(); }
    { Parser& s = p.sub("run"); s.help_line = "run the 36 months and print the summary";
      s.opt({"--write"}).store_true().help("write <dir>/model.json"); s.opt({"--months"}).store_true().help("print every month too"); s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); s.opt({"--assumptions"}).metavar("PATH").no_default(); }
    { Parser& s = p.sub("funds"); s.help_line = "check the use of funds adds up to the raise"; s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); s.opt({"--assumptions"}).metavar("PATH").no_default(); }
    { Parser& s = p.sub("csv"); s.help_line = "export <dir>/model.json as a CSV"; s.opt({"--out"}).metavar("PATH").help("default <dir>/model.csv; - prints it");
      s.opt({"--dir"}).no_default(); s.opt({"--assumptions"}).metavar("PATH").no_default(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "run") return cmd_run(a);
        if (cmd == "funds") return cmd_funds(a);
        return cmd_csv(a);
    } catch (const ModelError& e) { eprint(std::string("model.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-model/model", main);
}  // namespace packtools::seed_model
