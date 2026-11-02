// social_founder.inc - the founder pack's helpers: C++ ports of founder-skill (Jake Schincariol, MIT):
//   founder-cfo/unit_economics.py, founder-pricing/van_westendorp.py, founder-plan/compile.py, founder-consumer/panel.py.
// They print what the originals print (the messages still name the .py helpers, as the other ports do).
#include <array>
#include <random>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace packtools::founder {
using namespace pyrt;

// ------------------------------------------------------------------ Python value semantics the four helpers lean on
[[noreturn]] inline void type_error(const std::string& m) { throw PyErr("TypeError", m); }
inline std::string tname(const Val& v) { return type_name(v); }
inline std::vector<Val> iter_of(const Val& v) {   // for x in v
    std::vector<Val> o;
    if (v.t == Val::LIST) return *v.l;
    if (v.t == Val::DICT) { for (const auto& kv : v.d->items) o.push_back(Val::str(kv.first)); return o; }
    if (v.t == Val::STR) { for (const auto& c : cps(v.s)) o.push_back(Val::str(c)); return o; }
    type_error("'" + tname(v) + "' object is not iterable");
}
inline std::size_t pylen(const Val& v) {
    if (v.t == Val::LIST || v.t == Val::DICT) return v.size();
    if (v.t == Val::STR) return cplen(v.s);
    type_error("object of type '" + tname(v) + "' has no len()");
}
inline bool contains(const Val& c, const std::string& k) {   // k in c
    if (c.t == Val::DICT) return c.d->has(k);
    if (c.t == Val::STR) return c.s.find(k) != std::string::npos;
    if (c.t == Val::LIST) { for (const auto& x : *c.l) if (x.t == Val::STR && x.s == k) return true; return false; }
    type_error("argument of type '" + tname(c) + "' is not iterable");
}
inline Val idx(const Val& c, const std::string& k) {   // c[k]
    if (c.t == Val::DICT) { const Val* p = c.d->get(k); if (!p) throw PyErr("KeyError", str_repr(k)); return *p; }
    if (c.t == Val::LIST) type_error("list indices must be integers or slices, not str");
    if (c.t == Val::STR) type_error("string indices must be integers, not 'str'");
    type_error("'" + tname(c) + "' object is not subscriptable");
}
inline Val idx_i(const Val& c, long long i) {   // c[i]
    if (c.t == Val::LIST) { const long long n = static_cast<long long>(c.l->size()); const long long j = i < 0 ? i + n : i; if (j < 0 || j >= n) throw PyErr("IndexError", "list index out of range"); return (*c.l)[static_cast<std::size_t>(j)]; }
    if (c.t == Val::STR) { const auto cs = cps(c.s); const long long n = static_cast<long long>(cs.size()); const long long j = i < 0 ? i + n : i; if (j < 0 || j >= n) throw PyErr("IndexError", "string index out of range"); return Val::str(cs[static_cast<std::size_t>(j)]); }
    if (c.t == Val::DICT) throw PyErr("KeyError", std::to_string(i));
    type_error("'" + tname(c) + "' object is not subscriptable");
}
inline Val fget(const Val& c, const std::string& k, Val d = Val()) {   // c.get(k, d)
    if (c.t != Val::DICT) throw PyErr("AttributeError", "'" + tname(c) + "' object has no attribute 'get'");
    return c.d->at(k, std::move(d));
}
inline Val deep(const Val& v) {   // a copy that shares nothing (json.loads(json.dumps(v)))
    if (v.t == Val::LIST) { Val o = Val::list(); for (const auto& x : *v.l) o.l->push_back(deep(x)); return o; }
    if (v.t == Val::DICT) { Val o = Val::dict(); for (const auto& [k, x] : v.d->items) o.d->items.emplace_back(k, deep(x)); return o; }
    return v;
}
inline Val shallow(const Val& v) { Val o = Val::dict(); o.d->items = v.d->items; return o; }
inline Val dict_of(const Val& v) {   // dict(v)
    if (v.t == Val::DICT) return shallow(v);
    Val o = Val::dict();
    const std::vector<Val> xs = iter_of(v);
    for (std::size_t k = 0; k < xs.size(); ++k) {
        const Val& e = xs[k];
        if (e.t != Val::LIST && e.t != Val::STR && e.t != Val::DICT) type_error("cannot convert dictionary update sequence element #" + std::to_string(k) + " to a sequence");
        const std::vector<Val> pr = iter_of(e);
        if (pr.size() != 2) throw PyErr("ValueError", "dictionary update sequence element #" + std::to_string(k) + " has length " + std::to_string(pr.size()) + "; 2 is required");
        if (pr[0].t != Val::STR) type_error("keys that are not strings are not supported here");
        o.d->set(pr[0].s, pr[1]);
    }
    return o;
}
[[noreturn]] inline void nan_int() { throw PyErr("ValueError", "cannot convert float NaN to integer"); }
[[noreturn]] inline void inf_int() { throw PyErr("OverflowError", "cannot convert float infinity to integer"); }
inline long long float_to_int(double x) {   // int(x)
    if (std::isnan(x)) nan_int();
    if (std::isinf(x)) inf_int();
    if (std::fabs(x) >= 9.2e18) throw PyErr("OverflowError", "int too large for this build");
    return static_cast<long long>(x);
}
inline long long py_int(const Val& v) {   // int(v)
    if (v.t == Val::BOOL) return v.b ? 1 : 0;
    if (v.t == Val::INT) { if (v.big()) throw PyErr("OverflowError", "int too large for this build"); return v.i; }
    if (v.t == Val::FLOAT) return float_to_int(v.f);
    if (v.t == Val::STR) { const auto r = parse_int(v.s); if (!r) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s)); return *r; }
    type_error("int() argument must be a string, a bytes-like object or a real number, not '" + tname(v) + "'");
}
inline std::string pct_d(const Val& v) {   // "%d" % v
    if (v.t == Val::BOOL) return v.b ? "1" : "0";
    if (v.t == Val::INT) return v.big() ? v.s : std::to_string(v.i);
    if (v.t == Val::FLOAT) { if (std::isnan(v.f)) nan_int(); if (std::isinf(v.f)) inf_int(); return fmt_f(std::trunc(v.f), 0); }
    type_error("%d format: a real number is required, not " + tname(v));
}
inline double real(const Val& v) {   // "%f" % v
    if (!v.is_real()) type_error("must be real number, not " + tname(v));
    return v.num();
}
inline std::string c0(double x) { return commas(fmt_f(x, 0)); }   // "{:,.0f}".format(x)
inline std::string fmt_comma(const Val& v) {   // "{:,}".format(v)
    if (v.t == Val::BOOL) return v.b ? "1" : "0";
    if (v.t == Val::INT) return commas(v.big() ? v.s : std::to_string(v.i));
    if (v.t == Val::FLOAT) { const std::string r = float_repr(v.f); return (r.find('e') != std::string::npos || !std::isfinite(v.f)) ? r : commas(r); }
    if (v.t == Val::STR) throw PyErr("ValueError", "Cannot specify ',' with 's'.");
    type_error("unsupported format string passed to " + tname(v) + ".__format__");
}
inline int kind(const Val& v) { return v.is_real() ? 1 : v.t == Val::STR ? 2 : 0; }
[[noreturn]] inline void cmp_error(const char* op, const Val& a, const Val& b) { type_error(std::string("'") + op + "' not supported between instances of '" + tname(a) + "' and '" + tname(b) + "'"); }
inline bool lt(const Val& a, const Val& b) {   // a < b
    if (kind(a) == 1 && kind(b) == 1) return a.num() < b.num();
    if (kind(a) == 2 && kind(b) == 2) return a.s < b.s;
    cmp_error("<", a, b);
}
inline bool ge(const Val& a, const Val& b) {   // a >= b
    if (kind(a) == 1 && kind(b) == 1) return a.num() >= b.num();
    if (kind(a) == 2 && kind(b) == 2) return a.s >= b.s;
    cmp_error(">=", a, b);
}
inline void hashable(const Val& v) { if (v.t == Val::LIST || v.t == Val::DICT) type_error("unhashable type: '" + tname(v) + "'"); }
inline bool key_eq(const Val& a, const Val& b) {   // the same dict key (1 == 1.0 == True)
    if (a.is_real() && b.is_real()) return a.num() == b.num();
    if (a.t == Val::STR && b.t == Val::STR) return a.s == b.s;
    return a.t == Val::NONE && b.t == Val::NONE;
}
inline std::string concat_str(const Val& v, const std::string& tail) {   // v + "tail"
    if (v.t != Val::STR) type_error("unsupported operand type(s) for +: '" + tname(v) + "' and 'str'");
    return v.s + tail;
}
inline std::string pct0(double x) { return fmt_f(x * 100, 0) + "%"; }   // "%.0f%%" % (x * 100)
// open(path, encoding="utf-8").read(): false when the file does not exist (FileNotFoundError)
inline bool read_if(const std::string& path, std::string& out) {
    try { out = read_text_utf8(path); return true; }
    catch (const OSError& e) { if (e.err == ENOENT) return false; throw; }
}
inline bool is_enoent(const PyErr& e) { const auto* o = dynamic_cast<const OSError*>(&e); return o && o->err == ENOENT; }
inline std::string abspath(const std::string& p) {   // os.path.abspath
    std::error_code ec;
    std::filesystem::path a = std::filesystem::absolute(fspath(p.empty() ? std::string(".") : p), ec);
    std::string s = a.lexically_normal().generic_string();
#if defined(_WIN32)
    s = replace_all(s, "/", "\\");
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
#else
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#endif
    return s;
}
inline DumpOpts indent1(bool ascii) { DumpOpts o; o.indent = 1; o.ensure_ascii = ascii; return o; }

// ------------------------------------------------------------------ random.Random (CPython's Mersenne Twister and its helpers)
struct PyRandom {
    static constexpr int N = 624, M = 397;
    uint32_t mt[N]; int mti = N + 1;
    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < N; mti++) mt[mti] = 1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<uint32_t>(mti);
    }
    explicit PyRandom(long long seed) {   // random.Random(int): abs(seed) as 32-bit words, little end first
        unsigned long long n = seed < 0 ? 0ULL - static_cast<unsigned long long>(seed) : static_cast<unsigned long long>(seed);
        std::vector<uint32_t> key;
        while (n) { key.push_back(static_cast<uint32_t>(n & 0xffffffffULL)); n >>= 32; }
        if (key.empty()) key.push_back(0);
        init_genrand(19650218U);
        int i = 1; std::size_t j = 0;
        for (int k = std::max<int>(N, static_cast<int>(key.size())); k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
            i++; j++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
            if (j >= key.size()) j = 0;
        }
        for (int k = N - 1; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
            i++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
        }
        mt[0] = 0x80000000U;
        mti = N;
    }
    uint32_t genrand() {
        static const uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
        uint32_t y;
        if (mti >= N) {
            int kk = 0;
            for (; kk < N - M; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            for (; kk < N - 1; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            y = (mt[N - 1] & 0x80000000U) | (mt[0] & 0x7fffffffU);
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11); y ^= (y << 7) & 0x9d2c5680U; y ^= (y << 15) & 0xefc60000U; y ^= (y >> 18);
        return y;
    }
    double random() { const uint32_t a = genrand() >> 5, b = genrand() >> 6; return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0); }
    double uniform(double a, double b) { return a + (b - a) * random(); }
    uint64_t getrandbits(int k) {   // k <= 64; the low word comes first
        if (k <= 0) return 0;
        if (k <= 32) return genrand() >> (32 - k);
        const uint64_t lo = genrand();
        const uint64_t hi = genrand() >> (64 - k);
        return lo | (hi << 32);
    }
    uint64_t randbelow(uint64_t n) {   // n >= 1
        int k = 0; for (uint64_t t = n; t; t >>= 1) k++;
        uint64_t r = getrandbits(k);
        while (r >= n) r = getrandbits(k);
        return r;
    }
    long long randrange(long long start, long long stop) {
        if (stop > start) return start + static_cast<long long>(randbelow(static_cast<uint64_t>(stop) - static_cast<uint64_t>(start)));
        throw PyErr("ValueError", "empty range in randrange(" + std::to_string(start) + ", " + std::to_string(stop) + ")");
    }
    long long randrange(long long stop) {
        if (stop > 0) return static_cast<long long>(randbelow(static_cast<uint64_t>(stop)));
        throw PyErr("ValueError", "empty range for randrange()");
    }
    long long randint(long long a, long long b) { return randrange(a, b + 1); }
    template <class T> void shuffle(std::vector<T>& x) {
        for (std::size_t i = x.size(); i-- > 1;) { const std::size_t j = static_cast<std::size_t>(randbelow(i + 1)); std::swap(x[i], x[j]); }
    }
};

// ================================================================== founder-cfo/unit_economics
namespace ue {
struct NumbersError : std::runtime_error { using std::runtime_error::runtime_error; };
inline std::string money(double v) { return std::string(v < 0 ? "-" : "") + "$" + commas(fmt_f(std::fabs(v), 2)); }
inline std::string whole(double v) {
    if (std::isnan(v)) nan_int();
    if (std::isinf(v)) inf_int();
    return std::string(v < 0 ? "-" : "") + "$" + commas(fmt_f(std::fabs(std::nearbyint(v)), 0));
}
inline std::string ceil0(double be) { if (std::isnan(be)) nan_int(); return c0(std::ceil(be)); }   // "{:,.0f}".format(math.ceil(be))
inline double sum_items(const Val& items, const std::string& field) {
    double total = 0.0;
    const std::vector<Val> xs = truthy(items) ? iter_of(items) : std::vector<Val>{};
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const Val& it = xs[i];
        if (it.t != Val::DICT || !it.d->has("cost")) throw NumbersError(field + "[" + std::to_string(i) + "] needs a 'cost'");
        const Val c = *it.d->get("cost");
        try { total += to_float(c); }
        catch (const PyErr& e) {
            if (e.type == "TypeError" || e.type == "ValueError") throw NumbersError(field + "[" + std::to_string(i) + "].cost is not a number: " + repr(c));
            throw;
        }
    }
    return total;
}
inline Val load(const std::string& path) {
    std::string text;
    if (!read_if(path, text)) throw NumbersError("no such file: " + path);
    try { return loads(text); }
    catch (const JSONDecodeError& e) { throw NumbersError(path + " is not valid JSON: " + e.what()); }
}
struct Month { long long month; double per_day, units, revenue, variable, fixed, profit, cumulative; };
struct A {
    Val business, unit, capacity, var_items, fixed_items, startup_items;
    double price = 0, var = 0, contribution = 0, contribution_pct = 0, fixed = 0, startup = 0, plan = 0, margin = 0, breakeven = 0;
    double year_revenue = 0, year_profit = 0, after_startup = 0, cash_needed = 0;
    long long payback = 0;   // 0 = None
    std::vector<Month> months; std::vector<std::string> flags;
};
inline A analyse(const Val& spec, bool has_price = false, double price = 0, double volume = 1.0) {
    if (!contains(spec, "price") && !has_price) throw NumbersError("'price' is missing");
    A a;
    const double p = has_price ? price : to_float(idx(spec, "price"));
    if (p <= 0) throw NumbersError("price must be above zero");
    const double days = to_float(fget(spec, "days_per_month", Val::integer(30)));
    const double var = sum_items(fget(spec, "variable"), "variable");
    const double fixed = sum_items(fget(spec, "fixed_monthly"), "fixed_monthly");
    const double startup = sum_items(fget(spec, "startup"), "startup");
    if (!contains(spec, "plan_per_day") && !contains(spec, "ramp_per_day")) throw NumbersError("give 'plan_per_day' or 'ramp_per_day' (12 numbers)");
    double plan = to_float(fget(spec, "plan_per_day", Val::integer(0))) * volume;
    const Val r = fget(spec, "ramp_per_day");
    std::vector<double> ramp;
    if (r.is_none()) ramp.assign(12, plan);
    else {
        const std::size_t n = pylen(r);
        if (n != 12) throw NumbersError("'ramp_per_day' needs 12 numbers, one per month (got " + std::to_string(n) + ")");
        for (const Val& v : iter_of(r)) ramp.push_back(to_float(v) * volume);
    }
    if (plan == 0) plan = ramp.back();
    const double contribution = p - var;
    double breakeven = INFINITY;
    if (!(contribution <= 0)) {
        const double den = contribution * days;
        if (den == 0) throw PyErr("ZeroDivisionError", "float division by zero");
        breakeven = fixed / den;
    }
    const double pum = plan * days;
    a.margin = (contribution - (pum != 0 ? fixed / pum : INFINITY)) / p;
    double cumulative = -startup;
    std::vector<double> profits, revenues;
    for (std::size_t m = 0; m < ramp.size(); ++m) {
        Month mo{};
        mo.month = static_cast<long long>(m) + 1; mo.per_day = ramp[m];
        mo.units = ramp[m] * days; mo.revenue = mo.units * p; mo.variable = mo.units * var; mo.fixed = fixed;
        mo.profit = mo.revenue - mo.variable - fixed;
        cumulative += mo.profit; mo.cumulative = cumulative;
        if (!a.payback && cumulative >= 0) a.payback = mo.month;
        profits.push_back(mo.profit); revenues.push_back(mo.revenue);
        a.months.push_back(mo);
    }
    a.year_profit = fsum(profits); a.year_revenue = fsum(revenues);
    double lowest = -startup;
    for (const auto& mo : a.months) if (mo.cumulative < lowest) lowest = mo.cumulative;
    a.cash_needed = -lowest;
    a.capacity = fget(spec, "capacity_per_day");
    if (contribution <= 0) a.flags.push_back("Each unit loses money before rent and salaries: the price is below the variable cost.");
    if (!a.capacity.is_none() && breakeven > to_float(a.capacity))
        a.flags.push_back("Break-even needs " + fmt_f(breakeven, 0) + " " + pystr(fget(spec, "unit", Val::str("unit"))) + "s a day but capacity is " + pystr(a.capacity) + ".");
    if (!a.capacity.is_none() && plan > to_float(a.capacity))
        a.flags.push_back("The plan (" + fmt_f(plan, 0) + " a day) is above capacity (" + pystr(a.capacity) + ").");
    if (a.year_profit < 0) a.flags.push_back("Year 1 loses money on operations (" + whole(a.year_profit) + ").");
    if (!a.payback) a.flags.push_back("The startup spend is not earned back within year 1.");
    a.business = fget(spec, "business", Val::str("")); a.unit = fget(spec, "unit", Val::str("unit"));
    a.price = p; a.var = var; a.contribution = contribution; a.contribution_pct = contribution / p; a.fixed = fixed; a.startup = startup;
    a.plan = plan; a.breakeven = breakeven; a.after_startup = a.year_profit - startup;
    a.var_items = fget(spec, "variable", Val::list()); a.fixed_items = fget(spec, "fixed_monthly", Val::list()); a.startup_items = fget(spec, "startup", Val::list());
    return a;
}
inline Val to_json(const A& a) {
    Val o = Val::dict(); auto& d = *o.d;
    d.set("business", a.business); d.set("unit", a.unit); d.set("price", Val::flt(a.price)); d.set("variable_per_unit", Val::flt(a.var));
    d.set("contribution", Val::flt(a.contribution)); d.set("contribution_pct", Val::flt(a.contribution_pct)); d.set("fixed_monthly", Val::flt(a.fixed));
    d.set("startup", Val::flt(a.startup)); d.set("plan_per_day", Val::flt(a.plan)); d.set("margin_at_plan", Val::flt(a.margin));
    d.set("breakeven_per_day", std::isinf(a.breakeven) ? Val() : Val::flt(a.breakeven)); d.set("capacity_per_day", a.capacity);
    d.set("year1_revenue", Val::flt(a.year_revenue)); d.set("year1_profit", Val::flt(a.year_profit)); d.set("year1_after_startup", Val::flt(a.after_startup));
    d.set("payback_month", a.payback ? Val::integer(a.payback) : Val()); d.set("cash_needed", Val::flt(a.cash_needed));
    Val ms = Val::list();
    for (const auto& m : a.months) {
        Val x = Val::dict(); auto& e = *x.d;
        e.set("month", Val::integer(m.month)); e.set("per_day", Val::flt(m.per_day)); e.set("units", Val::flt(m.units)); e.set("revenue", Val::flt(m.revenue));
        e.set("variable", Val::flt(m.variable)); e.set("fixed", Val::flt(m.fixed)); e.set("profit", Val::flt(m.profit)); e.set("cumulative", Val::flt(m.cumulative));
        ms.l->push_back(x);
    }
    d.set("months", ms);
    Val fl = Val::list(); for (const auto& f : a.flags) fl.l->push_back(Val::str(f));
    d.set("flags", fl);
    return o;
}
inline std::vector<std::pair<std::string, A>> sensitivity(const Val& spec) {
    std::vector<std::pair<std::string, A>> rows;
    const A base = analyse(spec);
    rows.emplace_back("Base plan", base);
    rows.emplace_back("Price -10%", analyse(spec, true, base.price * 0.9));
    rows.emplace_back("Volume -20%", analyse(spec, false, 0, 0.8));
    Val worse = deep(spec);
    for (Val it : iter_of(fget(worse, "variable", Val::list()))) {
        if (it.t != Val::DICT) type_error("'" + tname(it) + "' object does not support item assignment");
        const double c = to_float(idx(it, "cost")) * 1.15;
        it.d->set("cost", Val::flt(c));
    }
    rows.emplace_back("Unit costs +15%", analyse(worse));
    return rows;
}
inline std::string report(const Val& spec, const A& a) {
    const std::string u = pystr(a.unit);
    std::vector<std::string> L;
    L.push_back("# Unit economics: " + (truthy(a.business) ? pystr(a.business) : std::string("your business")));
    L.push_back("");
    L.push_back("Every number below comes from the input file. Nothing is looked up or guessed.");
    L.push_back("");
    L.push_back("## One " + u);
    L.push_back("");
    L.push_back("| line | per " + u + " |");
    L.push_back("| --- | ---: |");
    L.push_back("| Price | " + money(a.price) + " |");
    for (const Val& it : iter_of(a.var_items)) L.push_back("| " + pystr(fget(it, "label", Val::str("cost"))) + " | -" + money(to_float(idx(it, "cost"))) + " |");
    L.push_back("| **Contribution** (what each " + u + " leaves to pay the fixed costs) | **" + money(a.contribution) + "** (" + pct0(a.contribution_pct) + ") |");
    L.push_back("");
    L.push_back("## The margin that matters");
    L.push_back("");
    std::vector<std::string> fx;
    for (const Val& it : iter_of(a.fixed_items)) fx.push_back(pystr(fget(it, "label", Val::str("cost"))) + " " + whole(to_float(idx(it, "cost"))));
    const std::string fj = join(fx, ", ");
    L.push_back("Fixed costs: " + whole(a.fixed) + " a month (" + (fj.empty() ? std::string("none given") : fj) + ").");
    L.push_back("");
    L.push_back("- **Break-even: " + (std::isinf(a.breakeven) ? std::string("never (each unit loses money)") : ceil0(a.breakeven)) + " " + u + "s a day.** Below that you lose money every month.");
    L.push_back("- **Profit margin at your plan** (" + c0(a.plan) + " a day): **" + pct0(a.margin) + "** of every sale, after every cost.");
    if (!a.capacity.is_none()) L.push_back("- Capacity: " + pystr(a.capacity) + " a day.");
    L.push_back("");
    L.push_back("## Year 1, month by month");
    L.push_back("");
    L.push_back("| month | " + u + "s a day | revenue | profit | cumulative (after " + whole(a.startup) + " startup) |");
    L.push_back("| ---: | ---: | ---: | ---: | ---: |");
    for (const auto& m : a.months)
        L.push_back("| " + std::to_string(m.month) + " | " + c0(m.per_day) + " | " + whole(m.revenue) + " | " + whole(m.profit) + " | " + whole(m.cumulative) + " |");
    L.push_back("");
    L.push_back("- **Year 1 operating profit: " + whole(a.year_profit) + "** on " + whole(a.year_revenue) + " of revenue.");
    L.push_back("- After the " + whole(a.startup) + " startup spend: " + whole(a.after_startup) + ".");
    L.push_back("- Startup money earned back: " + (a.payback ? "month " + std::to_string(a.payback) : std::string("not within year 1")) + ".");
    L.push_back("- Cash you need before it pays for itself: **" + whole(a.cash_needed) + "**.");
    L.push_back("");
    L.push_back("## What if");
    L.push_back("");
    L.push_back("| scenario | margin at plan | break-even a day | year 1 profit |");
    L.push_back("| --- | ---: | ---: | ---: |");
    for (const auto& [name, s] : sensitivity(spec))
        L.push_back("| " + name + " | " + pct0(s.margin) + " | " + (std::isinf(s.breakeven) ? std::string("never") : ceil0(s.breakeven)) + " | " + whole(s.year_profit) + " |");
    L.push_back("");
    if (!a.flags.empty()) {
        L.push_back("## Red flags");
        L.push_back("");
        for (const auto& f : a.flags) L.push_back("- " + f);
    } else {
        L.push_back("No red flags in these numbers. They are only as good as the inputs: check every cost against a real quote.");
    }
    return join(L, "\n") + "\n";
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("unit_economics", "Unit economics for founder-cfo. Standard library only, no network.");
    p.pos("numbers").help("the business's numbers.json");
    p.opt({"--out"}).help("write the report here (markdown)");
    p.opt({"--json"}).store_true().help("print the numbers as JSON");
    p.opt({"--price"}).number().help("what-if price");
    p.opt({"--volume"}).number().def_float(1.0).help("what-if volume multiplier");
    const Val args = parse_args(p, argv);
    Val spec; A a;
    try {
        spec = load(S(args, "numbers"));
        const Val price = get(args, "price");
        if (!price.is_none()) { spec = dict_of(spec); spec.d->set("price", price); }
        const double vol = get(args, "volume").num();
        if (vol != 1.0) {
            spec = dict_of(spec);
            if (spec.d->has("plan_per_day")) spec.d->set("plan_per_day", Val::flt(to_float(*spec.d->get("plan_per_day")) * vol));
            if (spec.d->has("ramp_per_day")) {
                Val nr = Val::list();
                for (const Val& v : iter_of(*spec.d->get("ramp_per_day"))) nr.l->push_back(Val::flt(to_float(v) * vol));
                spec.d->set("ramp_per_day", nr);
            }
        }
        a = analyse(spec);
    } catch (const NumbersError& e) {
        eprint(std::string("error: ") + e.what());
        return 2;
    }
    if (B(args, "json")) { print(dumps(to_json(a), indent1(true))); return 0; }
    std::string text;
    try { text = report(spec, a); }
    catch (const NumbersError& e) { throw PyErr("NumbersError", e.what()); }
    const std::string out_path = S(args, "out");
    if (!out_path.empty()) { write_text(out_path, text); print("wrote " + out_path); }
    else out(text);
    return 0;
}
PACKTOOL("founder-cfo/unit_economics", main);
}  // namespace ue

// ================================================================== founder-pricing/van_westendorp
namespace vw {
struct PricingError : std::runtime_error { using std::runtime_error::runtime_error; };
inline const char* const KEYS[4] = {"too_cheap", "cheap", "expensive", "too_expensive"};
struct Opt { bool has = false; double v = 0; };
inline Opt num_of(const Val& v) {   // _f
    const std::string s = strip(replace_all(replace_all(pystr(v), "$", ""), ",", ""));
    const auto r = parse_float(s);
    if (!r || !(*r > 0)) return {};
    return {true, *r};
}
struct Row { double v[4]; };
inline bool json_name(const std::string& n) {   // glob "*.json": no dot files; Windows matches without case
    if (n.empty() || n[0] == '.' || n.size() < 5) return false;
    std::string tail = n.substr(n.size() - 5);
#ifdef _WIN32
    for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return tail == ".json";
}
inline std::vector<Row> read(const std::string& path, long long& dropped) {
    std::vector<std::pair<bool, std::array<Opt, 4>>> raw;
    if (is_dir(path)) {
        std::vector<std::string> names; std::error_code ec;
        for (std::filesystem::directory_iterator it(fspath(path), ec), end; !ec && it != end; it.increment(ec)) {
            const auto u = it->path().filename().u8string();
            const std::string n(u.begin(), u.end());
            if (json_name(n)) names.push_back(n);
        }
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            const std::string fp = path_join(path, n);
            Val a;
            try { a = loads(read_text_utf8(fp)); }
            catch (const JSONDecodeError&) { raw.push_back({false, {}}); continue; }
            catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") { raw.push_back({false, {}}); continue; } throw; }
            std::array<Opt, 4> r;
            for (int k = 0; k < 4; ++k) r[static_cast<std::size_t>(k)] = num_of(fget(a, std::string("price_") + KEYS[k]));
            raw.push_back({true, r});
        }
    } else if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".csv") == 0) {
        const CsvDict cd = csv_dicts(read_text_utf8(path, false, false, true));
        for (const auto& row : cd.rows) {
            std::array<Opt, 4> r;
            for (int k = 0; k < 4; ++k) {
                Val v;
                for (const auto& [key, val] : row) if (key && *key == KEYS[k]) v = val;
                r[static_cast<std::size_t>(k)] = num_of(v);
            }
            raw.push_back({true, r});
        }
    } else {
        throw PricingError("give the panel's answers folder or a .csv");
    }
    std::vector<Row> rows; dropped = 0;
    for (const auto& [ok, r] : raw) {
        bool good = ok;
        if (good) for (const auto& o : r) if (!o.has) good = false;
        if (good && !(r[0].v <= r[1].v && r[1].v <= r[2].v && r[2].v <= r[3].v)) good = false;
        if (!good) { dropped++; continue; }
        rows.push_back({{r[0].v, r[1].v, r[2].v, r[3].v}});
    }
    return rows;
}
inline Opt cross(const std::vector<double>& grid, const std::vector<double>& falling, const std::vector<double>& rising) {
    for (std::size_t i = 1; i < grid.size(); ++i) {
        const double d0 = rising[i - 1] - falling[i - 1], d1 = rising[i] - falling[i];
        if (d0 <= 0 && 0 <= d1 && d1 != d0) { const double t = -d0 / (d1 - d0); return {true, grid[i - 1] + t * (grid[i] - grid[i - 1])}; }
        if (d0 == 0) return {true, grid[i - 1]};
    }
    return {};
}
struct Res { std::size_t n = 0; Opt pmc, pme, opp, ipp; double median[4] = {0, 0, 0, 0}; };
inline Res analyse(const std::vector<Row>& rows) {
    if (rows.size() < 5) throw PricingError("need at least 5 complete answers (got " + std::to_string(rows.size()) + ")");
    double lo = rows[0].v[0], hi = rows[0].v[3];
    for (const auto& r : rows) { if (r.v[0] < lo) lo = r.v[0]; if (r.v[3] > hi) hi = r.v[3]; }
    const double s = round_n((hi - lo) / 400, 2);
    const double step = s > 0.01 ? s : 0.01;
    std::vector<double> grid;
    for (double p = lo; p <= hi + 1e-9; p += step) {
        grid.push_back(round_n(p, 4));
        if (std::isinf(p) || p + step == p) throw PyErr("MemoryError", "the price grid never ends (an answer is too large)");   /* Python loops until memory runs out */
    }
    const double n = static_cast<double>(rows.size());
    std::vector<double> tc, ch, ex, te, not_cheap, not_exp;
    for (double p : grid) {
        int a = 0, b = 0, c = 0, d = 0;
        for (const auto& r : rows) { if (r.v[0] >= p) a++; if (r.v[1] >= p) b++; if (r.v[2] <= p) c++; if (r.v[3] <= p) d++; }
        tc.push_back(a / n); ch.push_back(b / n); ex.push_back(c / n); te.push_back(d / n);
        not_cheap.push_back(1 - b / n); not_exp.push_back(1 - c / n);
    }
    Res res; res.n = rows.size();
    res.pmc = cross(grid, tc, not_cheap); res.pme = cross(grid, not_exp, te); res.opp = cross(grid, tc, te); res.ipp = cross(grid, ch, ex);
    for (int k = 0; k < 4; ++k) {
        std::vector<double> col; for (const auto& r : rows) col.push_back(r.v[k]);
        std::sort(col.begin(), col.end());
        res.median[k] = col[col.size() / 2];
    }
    return res;
}
inline Val jopt(const Opt& o) { return o.has ? Val::flt(o.v) : Val(); }
inline std::string m(const Opt& o) { return o.has ? "$" + fmt_f(o.v, 2) : "n/a"; }
inline std::string report(const Res& res, long long dropped, bool has_price, double price) {
    std::vector<std::string> L = {"# Price sensitivity", "",
        std::to_string(res.n) + " complete answers" + (dropped ? ", " + std::to_string(dropped) + " dropped (incomplete or out of order)" : std::string()) +
            ". Simulated buyers: use this to pick what to test, not as the final word.", "",
        "| point | price | what it means |", "| --- | ---: | --- |",
        "| PMC | " + m(res.pmc) + " | below this, too many think it is too cheap to be good |",
        "| OPP | " + m(res.opp) + " | as many say too cheap as too expensive: the least resistance |",
        "| IPP | " + m(res.ipp) + " | as many call it a bargain as call it expensive |",
        "| PME | " + m(res.pme) + " | above this, too many say it is too expensive to buy |", "",
        "**Acceptable range: " + m(res.pmc) + " to " + m(res.pme) + ".**", ""};
    L.push_back("Median answers: too cheap " + m({true, res.median[0]}) + ", a bargain " + m({true, res.median[1]}) + ", getting expensive " + m({true, res.median[2]}) +
                ", too expensive " + m({true, res.median[3]}) + ".");
    if (has_price && res.pmc.has && res.pme.has) {
        const std::string where = (res.pmc.v <= price && price <= res.pme.v) ? "inside" : (price < res.pmc.v ? "below" : "above");
        L.push_back("");
        L.push_back("Your current price, " + m({true, price}) + ", is **" + where + "** the acceptable range.");
    }
    return join(L, "\n") + "\n";
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("van_westendorp", "Price sensitivity (Van Westendorp) for founder-pricing. Standard library only.");
    p.pos("source").help("the panel's answers folder, or a CSV");
    p.opt({"--price"}).number().help("your current price, to place it on the range");
    p.opt({"--out"}).help("write the report here (markdown)");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    long long dropped = 0; Res res;
    try {
        const std::vector<Row> rows = read(S(a, "source"), dropped);
        res = analyse(rows);
    } catch (const PricingError& e) {
        eprint(std::string("error: ") + e.what()); return 2;
    } catch (const PyErr& e) {
        if (!is_enoent(e)) throw;
        eprint(std::string("error: ") + e.what()); return 2;
    }
    if (B(a, "json")) {
        Val o = Val::dict(); auto& d = *o.d;
        d.set("n", Val::integer(static_cast<long long>(res.n)));
        Val pts = Val::dict(); pts.d->set("PMC", jopt(res.pmc)); pts.d->set("PME", jopt(res.pme)); pts.d->set("OPP", jopt(res.opp)); pts.d->set("IPP", jopt(res.ipp));
        d.set("points", pts);
        d.set("range", Val::list({jopt(res.pmc), jopt(res.pme)}));
        Val md = Val::dict(); for (int k = 0; k < 4; ++k) md.d->set(KEYS[k], Val::flt(res.median[k]));
        d.set("median", md);
        d.set("dropped", Val::integer(dropped));
        print(dumps(o, indent1(true)));
        return 0;
    }
    const Val pr = get(a, "price");
    const std::string text = report(res, dropped, !pr.is_none(), pr.is_none() ? 0.0 : pr.num());
    const std::string out_path = S(a, "out");
    if (!out_path.empty()) { write_text(out_path, text); print("wrote " + out_path); }
    else out(text);
    return 0;
}
PACKTOOL("founder-pricing/van_westendorp", main);
}  // namespace vw

// ================================================================== founder-plan/compile
namespace plan {
struct Section { const char* file; const char* heading; const char* skill; };
inline const Section SECTIONS[] = {
    {"summary.md", "Summary", "founder-plan"},
    {"board.md", "What the board said", "founder-board"},
    {"competitors.md", "The competition", "founder-competitors"},
    {"panel/results.md", "The buyer panel", "founder-consumer"},
    {"pricing.md", "Pricing", "founder-pricing"},
    {"offer.md", "The offer", "founder-offer"},
    {"cfo.md", "The numbers", "founder-cfo"},
    {"marketing.md", "Marketing", "founder-marketing"},
    {"brand.md", "Brand", "founder-brand"},
    {"ops.md", "Operations", "founder-ops"},
    {"launch.md", "Launch plan", "founder-launch"},
};
inline bool read_strip(const std::string& path, std::string& out) {   // _read: None (false) when missing
    if (!read_if(path, out)) return false;
    out = strip(out);
    return true;
}
inline std::string strip_h1(const std::string& text) {
    std::vector<std::string> lines = splitlines(text);
    if (!lines.empty() && starts(lines[0], "# ")) lines.erase(lines.begin());
    for (auto& l : lines) if (starts(l, "#")) l = "#" + l;
    return strip(join(lines, "\n"));
}
struct Verdict { std::string v; std::vector<std::pair<bool, std::string>> checks; bool has_numbers = false; ue::A numbers; bool has_panel = false; Val panel; };
inline Verdict verdict(const std::string& d, double min_buy_rate) {
    Verdict o; std::string raw;
    if (read_strip(path_join(d, "numbers.json"), raw) && !raw.empty()) {
        const Val spec = loads(raw);
        try { o.numbers = ue::analyse(spec); }
        catch (const ue::NumbersError& e) { throw PyErr("founder_unit_economics.NumbersError", e.what()); }
        o.has_numbers = true;
        const ue::A& n = o.numbers;
        const std::string u = pystr(n.unit);
        const double c = n.contribution;
        o.checks.emplace_back(c > 0, c > 0 ? "Each " + u + " earns $" + fmt_f(c, 2) + " before fixed costs (" + pct0(n.contribution_pct) + " contribution)."
                                           : "Each " + u + " LOSES $" + fmt_f(-c, 2) + " before fixed costs.");
        const double y = n.year_profit;
        o.checks.emplace_back(y > 0, y > 0 ? "Year 1 operating profit: $" + c0(y) + "." : "Year 1 operating LOSS: $" + c0(-y) + ".");
        if (!n.capacity.is_none()) {
            const bool fits = !std::isinf(n.breakeven) && n.breakeven <= to_float(n.capacity);
            o.checks.emplace_back(fits, "Break-even is " + (std::isinf(n.breakeven) ? std::string("never reached") : ue::ceil0(n.breakeven)) + " " + u +
                                            "s a day against a capacity of " + pystr(n.capacity) + ".");
        }
    }
    if (read_strip(path_join(path_join(d, "panel"), "results.json"), raw) && !raw.empty()) {
        o.panel = loads(raw); o.has_panel = true;
        const Val r = idx(o.panel, "buy_rate");
        if (!r.is_real()) cmp_error(">=", r, Val::flt(min_buy_rate));
        const bool ok = r.num() >= min_buy_rate;
        const std::string buys = pct_d(idx(o.panel, "buys")), answered = pct_d(idx(o.panel, "answered"));
        o.checks.emplace_back(ok, buys + " of " + answered + " simulated buyers buy (" + pct0(r.num()) + ", the bar is " + pct0(min_buy_rate) + ").");
    }
    if (!o.has_numbers) o.v = "Incomplete";
    else {
        bool all = true; for (const auto& c : o.checks) if (!c.first) all = false;
        o.v = all ? "Profitable" : "Not yet";
    }
    return o;
}
inline const char* mark(bool ok) { return ok ? "\xE2\x9C\x93" : "\xE2\x9C\x97"; }   // check mark / ballot x
inline std::pair<std::string, std::string> compile_plan(const std::string& d, double min_buy_rate) {
    std::string idea; const bool has_idea = read_strip(path_join(d, "idea.md"), idea) && !idea.empty();
    std::string name;
    if (has_idea) { const std::string first = splitlines(idea)[0]; if (starts(first, "# ")) name = strip(first.substr(2)); }
    const Verdict vd = verdict(d, min_buy_rate);
    std::string title = name;
    if (title.empty()) title = (vd.has_numbers && truthy(vd.numbers.business)) ? pystr(vd.numbers.business) : std::string("Your business");
    std::vector<std::string> L = {"# " + title + " \xC2\xB7 Business plan", ""};
    L.push_back("**Verdict: " + vd.v + "**");
    L.push_back("");
    for (const auto& [ok, text] : vd.checks) L.push_back(std::string("- ") + mark(ok) + " " + text);
    if (vd.v == "Incomplete") L.push_back("- No numbers yet. Run /founder-cfo, then compile again.");
    L.push_back("");
    if (vd.has_numbers) {
        const ue::A& n = vd.numbers; const std::string u = pystr(n.unit);
        L.push_back("| key number | |"); L.push_back("| --- | ---: |");
        L.push_back("| Price | $" + fmt_f(n.price, 2) + " a " + u + " |");
        L.push_back("| Profit margin at plan | " + pct0(n.margin) + " per " + u + " |");
        L.push_back("| Break-even | " + (std::isinf(n.breakeven) ? std::string("never") : ue::ceil0(n.breakeven)) + " " + u + "s a day |");
        L.push_back("| Year 1 operating profit | $" + c0(n.year_profit) + " |");
        L.push_back("| Startup spend | $" + c0(n.startup) + " |");
        L.push_back("| Cash needed before it pays for itself | $" + c0(n.cash_needed) + " |");
        L.push_back("| Startup money earned back | " + (n.payback ? "month " + std::to_string(n.payback) : std::string("not in year 1")) + " |");
        if (vd.has_panel && truthy(vd.panel)) L.push_back("| Buyer panel | " + pct_d(idx(vd.panel, "buys")) + " buy \xC2\xB7 " + pct_d(idx(vd.panel, "passes")) + " pass |");
        L.push_back("");
    }
    if (has_idea) { L.push_back("## The idea"); L.push_back(""); L.push_back(strip_h1(idea)); L.push_back(""); }
    std::vector<const Section*> missing;
    for (const auto& s : SECTIONS) {
        std::string text;
        if (read_strip(path_join(d, s.file), text) && !text.empty()) { L.push_back(std::string("## ") + s.heading); L.push_back(""); L.push_back(strip_h1(text)); L.push_back(""); }
        else missing.push_back(&s);
    }
    if (!missing.empty()) {
        L.push_back("## Not done yet"); L.push_back("");
        for (const auto* s : missing) L.push_back(std::string("- ") + s->heading + ": run /" + s->skill);
        L.push_back("");
    }
    L.push_back("_The panel is simulated buyers and the numbers are projections from your inputs. Confirm demand with real"
                " customers and costs with real quotes before you spend. Not financial, legal or tax advice._");
    return {vd.v, join(L, "\n") + "\n"};
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("compile", "Compiles the business plan for founder-plan. Standard library only, no network.");
    p.opt({"--dir"}).def("founder");
    p.opt({"--min-buy-rate"}).number().def_float(0.25);
    p.opt({"--check"}).store_true().help("print the verdict only; exit 0 if Profitable");
    p.opt({"--out"}).help("default: <dir>/business-plan.md");
    const Val a = parse_args(p, argv);
    const std::string d = S(a, "dir");
    const double rate = get(a, "min_buy_rate").num();
    if (!is_dir(d)) { eprint("error: no " + d + "/ folder here. Run the other founder skills first."); return 2; }
    const auto [v, text] = compile_plan(d, rate);
    if (B(a, "check")) {
        const Verdict vd = verdict(d, rate);
        print("Verdict: " + v);
        for (const auto& [ok, t] : vd.checks) print(std::string("  ") + mark(ok) + " " + t);
        return v == "Profitable" ? 0 : 1;
    }
    std::string out_path = S(a, "out");
    if (out_path.empty()) out_path = path_join(d, "business-plan.md");
    write_text(out_path, text);
    print("Verdict: " + v + ". Wrote " + out_path);
    return 0;
}
PACKTOOL("founder-plan/compile", main);
}  // namespace plan

// ================================================================== founder-consumer/panel
namespace panel {
struct PanelError : std::runtime_error { using std::runtime_error::runtime_error; };
inline const std::vector<std::string>& first_names() {
    static const std::vector<std::string> v = {
        "Maya", "Jordan", "Priya", "Marcus", "Sofia", "Ethan", "Aisha", "Liam", "Chloe", "Daniel", "Nadia", "Owen",
        "Grace", "Ravi", "Emma", "Kenji", "Zoe", "Luca", "Hana", "Noah", "Fatima", "Caleb", "Isla", "Mateo",
        "Leah", "Omar", "Ava", "Theo", "Mei", "Jonah", "Sara", "Diego", "Ruby", "Kofi", "Nora", "Felix",
        "Amara", "Ben", "Lina", "Sam", "Aria", "Julian", "Keisha", "Marco", "Ines", "Tariq", "Elena", "Hugo",
        "Yara", "Callum", "Rosa", "Idris", "Freya", "Andre", "Leila", "Mason", "Talia", "Rohan", "Esme", "Victor",
        "Anya", "Kai", "Beatriz", "Declan", "Sana", "Jasper", "Min", "Gabriel", "Olivia", "Arjun", "Clara", "Malik",
        "Iris", "Wes", "Noor", "Rafael", "Ivy", "Dev", "Maeve", "Tomas", "Celine", "Adrian", "Lucia", "Hamza",
        "Poppy", "Ezra", "Sienna", "Darius", "Quinn", "Selin", "Rhys", "Nia", "Oscar", "Tess", "Bilal", "Jade",
        "Luis", "Harper", "Emil", "Kira", "Pablo", "Alma", "Seth", "Zara", "Nico", "Farah", "Levi", "June",
        "Eli", "Ana", "Yusuf", "Lena", "Cole", "Mira", "Raj", "Elle", "Finn", "Dana", "Sol", "Ada",
    };
    return v;
}
inline const std::vector<std::string> REASONS = {"price", "habit", "need", "trust", "quality", "convenience", "timing", "values", "other"};
inline const char* const BRIEF = R"BRIEF(You are a simulated buyer on a consumer panel. A founder wants to know, before spending any money, whether real people like you would buy. Your answer is only useful if it is honest: most people do not buy most things, and a polite yes is worse than a clear no.

=== WHO YOU ARE ===
{name}, {age}. {segment}. You earn about ${income:,} a year.
How you buy: {behaviour}. {behaviour_how}
{habit_line}The first thing that comes to mind when you hear an offer like this: "{objection}"
You are one of these people: {target}.
=== END ===

=== WHAT IS BEING OFFERED (you see it for the first time, the way a passer-by or a scroller would) ===
{pitch}
=== END ===

Decide as {name} would, with {name}'s money, habits and doubts, in a normal week. Do not be agreeable. Do not invent facts about the business beyond what is written above.

Create the file {out} with your file-writing tool (do not just print the answer in your reply) containing ONLY this JSON, no markdown fence, nothing else:
{{
  "id": "{id}",
  "buys": true or false,
  "purchases_first_month": how many times you would buy in the first month (0 if you would not buy),
  "reason_code": one of {reasons},
  "reason": "why, in one or two sentences, in your own voice",
  "objection": "the doubt that weighs most for you, in your own words",
  "would_change_my_mind": "the one thing that would flip your answer, or empty",
  "price_too_cheap": the price at which it would seem too cheap to be any good (a number),
  "price_cheap": the price at which it would feel like a bargain (a number),
  "price_expensive": the price at which it starts to feel expensive but you might still buy (a number),
  "price_too_expensive": the price at which you would not buy it at all (a number)
}}
)BRIEF";
inline constexpr long long WAVE = 10;

// ---- dealing
inline std::vector<long long> quota(const Val& items, long long n, const std::string& key = "share") {
    const std::vector<Val> xs = iter_of(items);
    std::vector<double> shares;
    for (const Val& it : xs) shares.push_back(to_float(fget(it, key, Val::integer(1))));
    double total = fsum(shares);
    if (total == 0) total = 1.0;
    std::vector<double> raw; std::vector<long long> counts;
    for (std::size_t i = 0; i < xs.size(); ++i) { raw.push_back(static_cast<double>(n) * to_float(fget(xs[i], key, Val::integer(1))) / total); counts.push_back(float_to_int(raw.back())); }
    std::vector<std::size_t> order(xs.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return raw[x] - static_cast<double>(counts[x]) > raw[y] - static_cast<double>(counts[y]); });
    long long sum = 0; for (long long c : counts) sum += c;
    long long k = n - sum; const long long len = static_cast<long long>(order.size());
    if (k < 0) k = std::max<long long>(0, len + k);
    if (k > len) k = len;
    for (long long j = 0; j < k; ++j) counts[order[static_cast<std::size_t>(j)]] += 1;
    return counts;
}
inline bool empty_like(const Val& v) { return v.is_none() || (v.t == Val::STR && v.s.empty()) || (v.t == Val::LIST && v.l->empty()); }   // v in ("", [], None)
inline void validate_customer(const Val& c) {
    for (const char* k : {"business", "target", "segments", "behaviours", "objections"})
        if (!contains(c, k) || empty_like(idx(c, k))) throw PanelError(std::string("customer.json needs '") + k + "'");
    for (const Val& s : iter_of(idx(c, "segments")))
        if (!contains(s, "name") || !contains(s, "income") || pylen(idx(s, "income")) != 2) throw PanelError("every segment needs a name and an income [low, high]");
    for (const Val& b : iter_of(idx(c, "behaviours")))
        if (!contains(b, "name")) throw PanelError("every behaviour needs a name");
}
inline double round_thousands(double x) {   // round(x, -3): half to even on the exact value
    if (!std::isfinite(x)) return x;
    const double r = std::fmod(x, 1000.0);
    double base = x - r;
    const double ar = std::fabs(r);
    if (ar > 500 || (ar == 500 && std::fmod(base / 1000.0, 2.0) != 0)) base += std::copysign(1000.0, x);
    return base;
}
inline Val deal(const Val& customer, long long n, long long seed) {
    validate_customer(customer);
    if (n < 1) throw PanelError("--n must be at least 1");
    PyRandom rng(seed);
    const Val age = fget(customer, "age", Val::list({Val::integer(25), Val::integer(45)}));
    std::vector<Val> segs, behs;
    {
        const Val sv = idx(customer, "segments"); const std::vector<Val> xs = iter_of(sv); const auto q = quota(sv, n);
        for (std::size_t i = 0; i < xs.size() && i < q.size(); ++i) for (long long k = 0; k < q[i]; ++k) segs.push_back(xs[i]);
    }
    {
        const Val bv = idx(customer, "behaviours"); const std::vector<Val> xs = iter_of(bv); const auto q = quota(bv, n);
        for (std::size_t i = 0; i < xs.size() && i < q.size(); ++i) for (long long k = 0; k < q[i]; ++k) behs.push_back(xs[i]);
    }
    rng.shuffle(segs);
    rng.shuffle(behs);
    std::vector<std::string> names = first_names();
    rng.shuffle(names);
    const std::size_t base = first_names().size();
    while (static_cast<long long>(names.size()) < n) {
        const std::size_t batch = names.size() / base + 1;
        for (const auto& x : first_names()) names.push_back(x + " " + std::to_string(batch));
    }
    const Val objections = idx(customer, "objections");
    Val habits = fget(customer, "habits");
    if (!truthy(habits)) habits = Val::list();
    Val cards = Val::list();
    for (long long i = 0; i < n; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        if (ui >= segs.size() || ui >= behs.size()) throw PyErr("IndexError", "list index out of range");
        const Val& s = segs[ui]; const Val& b = behs[ui];
        const Val inc = idx(s, "income");
        const double lo = to_float(idx_i(inc, 0)), hi = to_float(idx_i(inc, 1));
        Val c = Val::dict(); auto& d = *c.d;
        char id[32]; std::snprintf(id, sizeof id, "P%03lld", i + 1);
        d.set("id", Val::str(id));
        d.set("name", Val::str(names[ui]));
        const long long a0 = py_int(idx_i(age, 0)), a1 = py_int(idx_i(age, 1));
        d.set("age", Val::integer(rng.randint(a0, a1)));
        d.set("segment", idx(s, "name"));
        d.set("income", Val::integer(float_to_int(round_thousands(rng.uniform(lo, hi)))));
        d.set("behaviour", idx(b, "name"));
        d.set("behaviour_how", fget(b, "how", Val::str("")));
        { const long long len = static_cast<long long>(pylen(objections)); d.set("objection", idx_i(objections, rng.randrange(len))); }
        if (truthy(habits)) { const long long len = static_cast<long long>(pylen(habits)); d.set("habit", idx_i(habits, rng.randrange(len))); }
        else d.set("habit", Val::str(""));
        cards.l->push_back(c);
    }
    return cards;
}

// ---- state
inline std::string pth(const std::string& d, const std::string& a) { return path_join(d, a); }
inline std::string pth(const std::string& d, const std::string& a, const std::string& b) { return path_join(path_join(d, a), b); }
inline Val read_json(const std::string& path) { return loads(read_text_utf8(path)); }
inline void write_json(const std::string& path, const Val& obj) {
    const std::string dir = dirname(path);
    makedirs(dir.empty() ? "." : dir);
    write_text(path, dumps(obj, indent1(false)) + "\n");
}
inline Val init(const std::string& d, const std::string& customer_path, const std::string& pitch_path, long long n, bool has_seed, long long seed) {
    Val customer;
    try { customer = read_json(customer_path); }
    catch (const PyErr& e) { if (is_enoent(e)) throw PanelError("no such file: " + customer_path); throw; }
    std::string pitch;
    if (!read_if(pitch_path, pitch)) throw PanelError("no such file: " + pitch_path + " (write the pitch first: what you sell, the price, where)");
    pitch = strip(pitch);
    if (pitch.empty()) throw PanelError("the pitch is empty");
    if (!has_seed) { std::random_device rd; seed = std::uniform_int_distribution<long long>(1, 999999)(rd); }
    const Val cards = deal(customer, n, seed);
    Val st = Val::dict();
    st.d->set("n", Val::integer(n)); st.d->set("seed", Val::integer(seed)); st.d->set("customer", customer); st.d->set("pitch", Val::str(pitch));
    write_json(pth(d, "panel.json"), st);
    write_json(pth(d, "personas.json"), cards);
    makedirs(pth(d, "briefs"));
    makedirs(pth(d, "answers"));
    return cards;
}
inline std::pair<Val, Val> load_state(const std::string& d) {
    try { Val a = read_json(pth(d, "panel.json")); Val b = read_json(pth(d, "personas.json")); return {a, b}; }
    catch (const PyErr& e) { if (is_enoent(e)) throw PanelError("no panel in " + d + ": run init first"); throw; }
}
inline std::string answer_problem(const std::string& path, const std::string& pid) {   // "" when the answer file is good
    if (!exists(path)) return "missing";
    Val a;
    try { a = read_json(path); }
    catch (const JSONDecodeError&) { return "not valid JSON"; }
    catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") return "not valid JSON"; throw; }
    std::vector<std::string> have;
    if (a.t == Val::DICT) { for (const auto& kv : a.d->items) have.push_back(kv.first); }
    else if (a.t == Val::LIST) { for (const auto& x : *a.l) { hashable(x); if (x.t == Val::STR) have.push_back(x.s); } }
    else for (const Val& x : iter_of(a)) have.push_back(x.s);
    std::vector<std::string> missing;
    for (const char* f : {"buys", "id", "reason", "reason_code"}) if (std::find(have.begin(), have.end(), f) == have.end()) missing.push_back(f);
    if (!missing.empty()) return "missing " + join(missing, ", ");
    const Val id = fget(a, "id");
    if (!(id.t == Val::STR && id.s == pid)) return "id is " + repr(id) + ", expected " + pid;
    if (fget(a, "buys").t != Val::BOOL) return "'buys' must be true or false";
    const Val rc = fget(a, "reason_code");
    if (!(rc.t == Val::STR && std::find(REASONS.begin(), REASONS.end(), rc.s) != REASONS.end())) return "reason_code " + repr(rc) + " is not one of " + join(REASONS, ", ");
    return "";
}
inline std::string card_id(const Val& c) { const Val id = idx(c, "id"); return concat_str(id, ""); }
// BRIEF.format(pitch=..., target=..., out=..., habit_line=..., reasons=..., **card)
inline std::string fill_brief(const Val& card, const std::vector<std::pair<std::string, Val>>& kw) {
    if (card.t != Val::DICT) type_error("str.format() argument after ** must be a mapping, not " + tname(card));
    for (const auto& [k, v] : card.d->items) for (const auto& kv : kw) if (kv.first == k) type_error("str.format() got multiple values for keyword argument '" + k + "'");
    const std::string t = BRIEF; std::string o;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char ch = t[i];
        if (ch == '{' && i + 1 < t.size() && t[i + 1] == '{') { o += '{'; ++i; continue; }
        if (ch == '}' && i + 1 < t.size() && t[i + 1] == '}') { o += '}'; ++i; continue; }
        if (ch != '{') { o += ch; continue; }
        const std::size_t e = t.find('}', i);
        std::string field = t.substr(i + 1, e - i - 1), spec;
        if (const std::size_t c = field.find(':'); c != std::string::npos) { spec = field.substr(c + 1); field = field.substr(0, c); }
        const Val* v = nullptr;
        for (const auto& kv : kw) if (kv.first == field) v = &kv.second;
        if (!v) v = card.d->get(field);
        if (!v) throw PyErr("KeyError", str_repr(field));
        o += spec == "," ? fmt_comma(*v) : pystr(*v);
        i = e;
    }
    return o;
}
inline std::vector<std::vector<std::pair<std::string, std::string>>> write_briefs(const std::string& d, long long wave) {
    const auto [state, cards] = load_state(d);
    const Val target = idx(idx(state, "customer"), "target");
    std::vector<std::pair<std::string, std::string>> todo;
    for (const Val& c : iter_of(cards)) {
        const std::string id = card_id(c);
        const std::string outp = pth(d, "answers", id + ".json");
        if (answer_problem(outp, id).empty()) continue;
        const Val habit = fget(c, "habit");
        const std::string brief = fill_brief(c, {{"pitch", idx(state, "pitch")}, {"target", target}, {"out", Val::str(abspath(outp))},
                                                 {"habit_line", Val::str(truthy(habit) ? "Your day: " + pystr(habit) + ".\n" : std::string())},
                                                 {"reasons", Val::str(join(REASONS, " / "))}});
        const std::string bp = pth(d, "briefs", id + ".md");
        write_text(bp, brief);
        todo.emplace_back(id, abspath(bp));
    }
    if (wave == 0) throw PyErr("ValueError", "range() arg 3 must not be zero");
    std::vector<std::vector<std::pair<std::string, std::string>>> waves;
    if (wave > 0)
        for (std::size_t i = 0; i < todo.size(); i += static_cast<std::size_t>(wave))
            waves.emplace_back(todo.begin() + static_cast<std::ptrdiff_t>(i), todo.begin() + static_cast<std::ptrdiff_t>(std::min(todo.size(), i + static_cast<std::size_t>(wave))));
    return waves;
}
// json.JSONDecoder().raw_decode(text): the first JSON value and nothing after it
inline Val raw_decode(const std::string& text) {
    try { return loads(text); }
    catch (const JSONDecodeError& e) {
        if (e.msg != "Extra data") throw;
        std::size_t off = 0, cp = 0;
        while (off < text.size() && cp < e.pos) { off++; while (off < text.size() && (static_cast<unsigned char>(text[off]) & 0xC0) == 0x80) off++; cp++; }
        return loads(text.substr(0, off));
    }
}
inline std::string save(const std::string& d, const std::string& pid, const std::string& text) {
    const auto st = load_state(d);
    bool known = false;
    for (const Val& c : iter_of(st.second)) { const Val id = idx(c, "id"); hashable(id); if (id.t == Val::STR && id.s == pid) known = true; }
    if (!known) throw PanelError("no buyer " + pid + " on this panel");
    const std::size_t start = text.find('{');
    if (start == std::string::npos) throw PanelError("no JSON object in the reply");
    Val obj;
    try { obj = raw_decode(text.substr(start)); }
    catch (const JSONDecodeError& e) { throw PanelError(std::string("the reply's JSON does not parse: ") + e.what()); }
    const std::string path = pth(d, "answers", pid + ".json");
    write_json(path, obj);
    const std::string prob = answer_problem(path, pid);
    if (!prob.empty()) {
        std::error_code ec; std::filesystem::remove(fspath(path), ec);
        if (ec) throw OSError(ec.value(), path);
        throw PanelError(pid + "'s answer is not valid (" + prob + "); re-run its brief instead");
    }
    return path;
}
inline std::vector<std::pair<std::string, std::string>> check(const std::string& d) {
    const auto st = load_state(d);
    std::vector<std::pair<std::string, std::string>> probs;
    for (const Val& c : iter_of(st.second)) {
        const std::string id = card_id(c);
        const std::string p = answer_problem(pth(d, "answers", id + ".json"), id);
        if (!p.empty()) probs.emplace_back(id, p);
    }
    return probs;
}

// ---- tally
struct Group { Val key; long long n; double rate; };
struct Why { std::string code; long long n; std::vector<Val> quotes; std::vector<Val> ids; };
struct Tally {
    Val business, seed; long long dealt = 0, answered = 0, buys = 0, passes = 0; double buy_rate = 0;
    std::vector<std::pair<std::string, std::string>> skipped;
    std::vector<Group> by_segment, by_behaviour, by_income; std::vector<Why> why_not, why_yes;
    std::vector<Val> flip; bool has_pfm = false; double pfm = 0; long long price_answers = 0;
};
inline double rate_of(const std::vector<Val>& rows) {
    if (rows.empty()) return 0.0;
    long long y = 0; for (const auto& r : rows) if (truthy(idx(r, "buys"))) y++;
    return static_cast<double>(y) / static_cast<double>(rows.size());
}
inline bool num_ok(const Val& v, double& out) {   // _num
    try { out = to_float(v); return true; }
    catch (const PyErr& e) { if (e.type == "TypeError" || e.type == "ValueError") return false; throw; }
}
inline std::vector<Group> group(const std::vector<Val>& rows, const std::string& key) {
    std::vector<std::pair<Val, std::vector<Val>>> out;
    for (const auto& r : rows) {
        const Val k = idx(r, key); hashable(k);
        bool found = false;
        for (auto& [kk, v] : out) if (key_eq(kk, k)) { v.push_back(r); found = true; break; }
        if (!found) out.push_back({k, {r}});
    }
    std::vector<Group> g;
    for (const auto& [k, v] : out) g.push_back({k, static_cast<long long>(v.size()), rate_of(v)});
    std::stable_sort(g.begin(), g.end(), [](const Group& a, const Group& b) { return -a.rate < -b.rate; });
    return g;
}
inline std::vector<Why> reasons(const std::vector<Val>& rs) {
    std::vector<std::pair<Val, std::vector<Val>>> out;
    for (const auto& r : rs) {
        const Val k = idx(r, "reason_code"); hashable(k);
        bool found = false;
        for (auto& [kk, v] : out) if (key_eq(kk, k)) { v.push_back(r); found = true; break; }
        if (!found) out.push_back({k, {r}});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    std::vector<Why> w;
    for (const auto& [k, v] : out) {
        Why x{pystr(k), static_cast<long long>(v.size()), {}, {}};
        for (std::size_t i = 0; i < v.size() && i < 2; ++i) { x.quotes.push_back(idx(v[i], "reason")); x.ids.push_back(idx(v[i], "id")); }
        w.push_back(x);
    }
    return w;
}
inline Tally tally(const std::string& d) {
    const auto [state, cards] = load_state(d);
    const std::vector<Val> cs = iter_of(cards);
    std::map<std::string, Val> by_id;
    for (const auto& c : cs) { const Val id = idx(c, "id"); hashable(id); if (id.t == Val::STR) by_id[id.s] = c; }
    Tally t; std::vector<Val> rows;
    for (const auto& c : cs) {
        const std::string id = card_id(c);
        const std::string path = pth(d, "answers", id + ".json");
        const std::string p = answer_problem(path, id);
        if (!p.empty()) { t.skipped.emplace_back(id, p); continue; }
        const Val a = read_json(path);
        const Val& card = by_id[id];
        if (card.t != Val::DICT) type_error("'" + tname(card) + "' object is not a mapping");
        Val r = shallow(card);
        for (const auto& [k, v] : a.d->items) if (k != "id") r.d->set(k, v);
        r.d->set("id", Val::str(id));
        rows.push_back(r);
    }
    if (rows.empty()) throw PanelError("no valid answers yet: run the briefs first (panel.py prompts)");
    std::vector<Val> incomes;
    for (const auto& r : rows) incomes.push_back(idx(r, "income"));
    if (incomes.size() > 1) {
        const int k0 = kind(incomes[0]);
        for (const auto& v : incomes) if (kind(v) == 0 || kind(v) != k0) cmp_error("<", v, incomes[0]);
        std::stable_sort(incomes.begin(), incomes.end(), [](const Val& x, const Val& y) { return lt(x, y); });
    }
    const Val cut1 = incomes[incomes.size() / 3], cut2 = incomes[2 * incomes.size() / 3];
    for (auto& r : rows) {
        const Val inc = idx(r, "income");
        std::string band;
        if (lt(inc, cut1)) band = "under $" + fmt_comma(cut1);
        else if (ge(inc, cut2)) band = "$" + fmt_comma(cut2) + " and up";
        else band = "$" + fmt_comma(cut1) + " to $" + fmt_comma(cut2);
        r.d->set("income_band", Val::str(band));
    }
    std::vector<Val> no, yes;
    for (const auto& r : rows) (truthy(idx(r, "buys")) ? yes : no).push_back(r);
    t.business = idx(idx(state, "customer"), "business");
    t.seed = idx(state, "seed");
    t.dealt = static_cast<long long>(pylen(cards));
    t.answered = static_cast<long long>(rows.size()); t.buys = static_cast<long long>(yes.size()); t.passes = static_cast<long long>(no.size());
    t.buy_rate = static_cast<double>(yes.size()) / static_cast<double>(rows.size());
    t.by_segment = group(rows, "segment"); t.by_behaviour = group(rows, "behaviour"); t.by_income = group(rows, "income_band");
    t.why_not = reasons(no); t.why_yes = reasons(yes);
    for (const auto& r : no) {
        const Val w = fget(r, "would_change_my_mind");
        if (!truthy(w)) continue;
        if (w.t != Val::STR) throw PyErr("AttributeError", "'" + tname(w) + "' object has no attribute 'strip'");
        if (!strip(w.s).empty()) t.flip.push_back(idx(r, "would_change_my_mind"));
    }
    std::vector<double> vals;
    for (const auto& r : yes) { double x; if (num_ok(fget(r, "purchases_first_month"), x)) vals.push_back(x); }
    if (!vals.empty()) { t.has_pfm = true; t.pfm = fsum(vals) / static_cast<double>(vals.size()); }
    for (const auto& r : rows) {
        bool all = true;
        for (const char* k : {"price_too_cheap", "price_cheap", "price_expensive", "price_too_expensive"}) { double x; if (!num_ok(fget(r, k), x)) { all = false; break; } }
        if (all) t.price_answers++;
    }
    return t;
}
inline Val tally_json(const Tally& t) {
    Val o = Val::dict(); auto& d = *o.d;
    d.set("business", t.business); d.set("seed", t.seed); d.set("dealt", Val::integer(t.dealt)); d.set("answered", Val::integer(t.answered));
    Val sk = Val::list(); for (const auto& [a, b] : t.skipped) sk.l->push_back(Val::list({Val::str(a), Val::str(b)}));
    d.set("skipped", sk);
    d.set("buys", Val::integer(t.buys)); d.set("passes", Val::integer(t.passes)); d.set("buy_rate", Val::flt(t.buy_rate));
    auto groups = [](const std::vector<Group>& g) { Val l = Val::list(); for (const auto& x : g) l.l->push_back(Val::list({x.key, Val::integer(x.n), Val::flt(x.rate)})); return l; };
    d.set("by_segment", groups(t.by_segment)); d.set("by_behaviour", groups(t.by_behaviour)); d.set("by_income", groups(t.by_income));
    auto whys = [](const std::vector<Why>& w) { Val l = Val::list(); for (const auto& x : w) l.l->push_back(Val::list({Val::str(x.code), Val::integer(x.n), Val::list(x.quotes), Val::list(x.ids)})); return l; };
    d.set("why_not", whys(t.why_not)); d.set("why_yes", whys(t.why_yes));
    d.set("flip", Val::list(t.flip));
    d.set("purchases_first_month_per_buyer", t.has_pfm ? Val::flt(t.pfm) : Val());
    d.set("price_answers", Val::integer(t.price_answers));
    return o;
}
inline std::string quote_cell(const Why& w) {
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < w.quotes.size() && i < w.ids.size(); ++i) {
        const Val& q = w.quotes[i];
        if (q.t != Val::STR) throw PyErr("AttributeError", "'" + tname(q) + "' object has no attribute 'replace'");
        parts.push_back("\"" + replace_all(q.s, "|", "/") + "\" (" + pystr(w.ids[i]) + ")");
    }
    return join(parts, " \xC2\xB7 ");
}
inline std::string results_md(const Tally& t) {
    std::vector<std::string> L = {"# Buyer panel: " + pystr(t.business), ""};
    L.push_back("**" + std::to_string(t.buys) + " buy \xC2\xB7 " + std::to_string(t.passes) + " pass** (" + pct0(t.buy_rate) + " buy) out of " + std::to_string(t.answered) +
                " simulated buyers. Seed " + pystr(t.seed) + ", so the same cards can be dealt again.");
    if (!t.skipped.empty()) {
        L.push_back("");
        std::vector<std::string> s;
        for (std::size_t i = 0; i < t.skipped.size() && i < 10; ++i) s.push_back(t.skipped[i].first + " (" + t.skipped[i].second + ")");
        L.push_back(std::to_string(t.skipped.size()) + " of " + std::to_string(t.dealt) + " buyers have no valid answer and are left out: " + join(s, ", ") + ".");
    }
    L.push_back("");
    L.push_back("These are simulated buyers, not customers. Use this to find objections and weak spots, then confirm the"
                " big ones with real people before you spend.");
    const std::pair<const char*, const std::vector<Group>*> secs[] = {{"By segment", &t.by_segment}, {"By buying behaviour", &t.by_behaviour}, {"By income", &t.by_income}};
    for (const auto& [title, g] : secs) {
        for (const char* l : {"", "", "", "| group | buyers | buy rate |", "| --- | ---: | ---: |"}) L.push_back(l);
        L[L.size() - 4] = std::string("## ") + title;
        for (const auto& x : *g) L.push_back("| " + pystr(x.key) + " | " + std::to_string(x.n) + " | " + pct0(x.rate) + (x.n < 8 ? "  (thin)" : "") + " |");
    }
    for (const char* l : {"", "## Why they pass", "", "| reason | buyers | in their words |", "| --- | ---: | --- |"}) L.push_back(l);
    for (const auto& w : t.why_not) L.push_back("| " + w.code + " | " + std::to_string(w.n) + " | " + quote_cell(w) + " |");
    for (const char* l : {"", "## Why they buy", "", "| reason | buyers | in their words |", "| --- | ---: | --- |"}) L.push_back(l);
    for (const auto& w : t.why_yes) L.push_back("| " + w.code + " | " + std::to_string(w.n) + " | " + quote_cell(w) + " |");
    if (!t.flip.empty()) {
        L.push_back(""); L.push_back("## What would flip a no"); L.push_back("");
        for (std::size_t i = 0; i < t.flip.size() && i < 12; ++i) L.push_back("- " + pystr(t.flip[i]));
    }
    if (t.has_pfm) { L.push_back(""); L.push_back("Buyers say they would buy **" + fmt_f(t.pfm, 1) + " times** in the first month on average."); }
    if (t.price_answers) { L.push_back(""); L.push_back(std::to_string(t.price_answers) + " buyers gave all four price answers. Run founder-pricing's van_westendorp.py on the answers folder."); }
    return join(L, "\n") + "\n";
}
inline std::string read_stdin_strict() {   // sys.stdin.read(): UTF-8, universal newlines
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::string t; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) t.append(buf, n);
    if (const auto bad = utf8_decode_error(t)) throw PyErr("UnicodeDecodeError", *bad);
    std::string o; o.reserve(t.size());
    for (std::size_t k = 0; k < t.size(); ++k) { if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') ++k; } else o += t[k]; }
    return o;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("panel", "The buyer panel for founder-consumer. Standard library only, no network.");
#ifdef _WIN32
    p.opt({"--dir"}).def("founder\\panel");
#else
    p.opt({"--dir"}).def("founder/panel");
#endif
    p.subcommands("cmd", true);
    { Parser& s = p.sub("init");
      s.opt({"--customer"}).required(); s.opt({"--pitch"}).required();
      s.opt({"--n"}).integer().def(100); s.opt({"--seed"}).integer(); }
    { Parser& s = p.sub("prompts"); s.opt({"--wave"}).integer().def(WAVE); }
    p.sub("check");
    { Parser& s = p.sub("save"); s.pos("id"); }
    { Parser& s = p.sub("tally"); s.opt({"--out"}); }
    p.sub("status");
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), d = S(a, "dir");
    try {
        if (cmd == "init") {
            const Val seed = get(a, "seed");
            const Val cards = init(d, S(a, "customer"), S(a, "pitch"), get(a, "n").i, !seed.is_none(), seed.is_none() ? 0 : seed.i);
            const auto st = load_state(d);
            print("dealt " + std::to_string(cards.size()) + " buyers (seed " + pystr(idx(st.first, "seed")) + ") into " + d + ". Next: panel.py prompts");
        } else if (cmd == "prompts") {
            const long long wave = get(a, "wave").i;
            const auto waves = write_briefs(d, wave);
            std::size_t n = 0; for (const auto& w : waves) n += w.size();
            if (!n) print("every buyer has a valid answer. Next: panel.py tally");
            else {
                print(std::to_string(n) + " briefs in " + std::to_string(waves.size()) + " waves of up to " + std::to_string(wave) + ". Launch one wave at a time:");
                for (std::size_t k = 0; k < waves.size(); ++k) {
                    std::vector<std::string> jobs; for (const auto& [id, bp] : waves[k]) jobs.push_back(id + "=" + bp);
                    print("wave " + std::to_string(k + 1) + ": " + join(jobs, " "));
                }
            }
        } else if (cmd == "check") {
            const auto probs = check(d);
            if (probs.empty()) print("all answers present and valid");
            for (const auto& [pid, prob] : probs) print(pid + ": " + prob);
            return probs.empty() ? 0 : 1;
        } else if (cmd == "save") {
            const std::string pid = S(a, "id");
            print("saved " + save(d, pid, read_stdin_strict()));
        } else if (cmd == "tally") {
            const Tally t = tally(d);
            const std::string text = results_md(t);
            std::string outp = S(a, "out");
            if (outp.empty()) outp = pth(d, "results.md");
            write_text(outp, text);
            write_json(pth(d, "results.json"), tally_json(t));
            print(std::to_string(t.buys) + " buy \xC2\xB7 " + std::to_string(t.passes) + " pass (" + pct0(t.buy_rate) + "). Wrote " + outp);
        } else if (cmd == "status") {
            const auto st = load_state(d);
            const auto probs = check(d);
            const long long n = static_cast<long long>(pylen(st.second));
            print(std::to_string(n) + " buyers dealt (seed " + pystr(idx(st.first, "seed")) + "), " + std::to_string(n - static_cast<long long>(probs.size())) + " answered, " +
                  std::to_string(probs.size()) + " to go");
        }
    } catch (const PanelError& e) {
        eprint(std::string("error: ") + e.what());
        return 2;
    }
    return 0;
}
PACKTOOL("founder-consumer/panel", main);
}  // namespace panel
}  // namespace packtools::founder
