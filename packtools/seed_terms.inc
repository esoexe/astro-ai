// seed-terms/terms: the math investors do before they send a SAFE or a term sheet (post-money SAFE ownership, the
// priced round with converting SAFEs and the option-pool shuffle, in exact fractions) and a clause scanner for the
// terms founders regret. A C++ port of seed-skill/seed-terms/terms.py (MIT, Jake Schincariol); same output, files and
// exit codes: 0 done (scan: no high-severity flag), 1 refused or an error, 3 scan found a high-severity flag.
// Not legal advice and not investment advice.
namespace packtools::seed_terms {
using namespace pyrt;
using namespace packtools::seed;
using seed_scout::py_int_val;

struct TermsError : std::runtime_error { explicit TermsError(const std::string& m) : std::runtime_error(m) {} };

inline const std::string NOTE = "Not legal advice and not investment advice: a startup lawyer should read the documents before you sign. "
                                "The numbers are only as good as the inputs.";
inline int severity_order(const std::string& s) { return s == "high" ? 0 : s == "medium" ? 1 : s == "low" ? 2 : 3; }

// ------------------------------------------------------------ inputs
struct Safe { std::optional<std::string> name; Fraction investment; std::optional<Fraction> cap; Fraction discount; };

inline Fraction frac_of(const std::string& digits) { return Fraction::from_decimal(Decimal::parse(ascii_digits(digits))); }   // Fraction("12.5")
inline std::string money(const Fraction& v);
inline Fraction parse_money(const Val& text, const std::string& what) {
    std::string s = lower(strip(pystr(text)));
    s = replace_all(replace_all(replace_all(s, ",", ""), "$", ""), " ", "");
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)([kmb]?)$)", s);
    if (!m) throw TermsError(what + " wants an amount like 500k, 1.5m or 1500000, got " + repr(text));
    const std::string u = m.group(2);
    const Fraction v = frac_of(m.group(1)) * Fraction(u == "k" ? 1000LL : u == "m" ? 1000000LL : u == "b" ? 1000000000LL : 1LL);
    if (v <= Fraction(0)) throw TermsError(what + " must be more than zero");
    return v;
}
inline Fraction parse_pct(const Val& text, const std::string& what, const Fraction& top = Fraction(BigInt(99), BigInt(100))) {
    const std::string s = strip(pystr(text));
    const bool pct = !s.empty() && s.back() == '%';
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)%?$)", s);
    if (!m) throw TermsError(what + " wants a percentage like 20 or 20%, got " + repr(text));
    Fraction v = frac_of(m.group(1));
    if (Fraction(0) < v && v < Fraction(1) && !pct) throw TermsError(what + ": write 20 for 20% (got " + s + ")");
    v /= Fraction(100);
    if (!(Fraction(0) <= v && v <= top)) throw TermsError(what + " must be between 0 and " + (top * Fraction(100)).trunc().str() + "%");
    return v;
}
inline BigInt parse_shares(const Val& text, const std::string& what) {
    const std::string s = replace_all(replace_all(strip(pystr(text)), ",", ""), "_", "");
    if (!re::match(R"(^\d+$)", s)) throw TermsError(what + " wants a whole number of shares, got " + repr(text));
    return BigInt::parse(ascii_digits(s));
}
inline bool blank(const Val& v) { return v.is_none() || (v.t == Val::STR && v.s.empty()); }   // v in (None, "")
inline Safe make_safe(const Val& name, const Val& inv, const Val& cap = Val(), const Val& disc = Val()) {
    Safe s;
    if (truthy(name)) {
        if (name.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(name) + "' object has no attribute 'strip'");
        const std::string n = strip(name.s); if (!n.empty()) s.name = n;
    }
    s.investment = parse_money(inv, "SAFE investment");
    if (!blank(cap)) s.cap = parse_money(cap, "SAFE cap");
    s.discount = !blank(disc) ? parse_pct(disc, "SAFE discount", Fraction(BigInt(1), BigInt(2))) : Fraction(0);
    if (s.cap && *s.cap <= s.investment)
        throw TermsError("a " + money(*s.cap) + " cap with a " + money(s.investment) + " investment would hand over the whole company: the post-money cap must "
                         "be bigger than the investment");
    return s;
}
inline Safe parse_safe(const std::string& text) {
    const re::Match m = re::match(R"(^(?:(?P<name>[^=@:]+)=)?(?P<inv>[^@:]+)(?:@(?P<cap>[^:]*))?(?::(?P<disc>.+))?$)", strip(text));
    if (!m) throw TermsError("a SAFE looks like [name=]investment[@cap][:discount], e.g. 500k@10m or 250k@8m:20, got " + str_repr(text));
    auto g = [&](int k) { return m.has(k) ? sv(m.group(k)) : Val(); };
    const Val cap = g(3);
    return make_safe(g(1), g(2), truthy(cap) ? cap : Val(), g(4));
}

// ------------------------------------------------------------ formatting
inline std::string money(const Fraction& v) {
    const bool whole_v = v.denominator() == BigInt(1);
    return "$" + pyrt::commas(fmt_f(v.to_double(), whole_v ? 0 : 2));
}
inline std::string money_i(const BigInt& v) { return money(Fraction(v)); }
inline std::string short_m(const Fraction& v) {
    const double x = v.to_double();
    const std::pair<const char*, double> units[] = {{"B", 1e9}, {"M", 1e6}, {"K", 1e3}};
    for (const auto& [unit, div] : units)
        if (x >= div) return "$" + rstrip_chars(rstrip_chars(fmt_f(x / div, 2), "0"), ".") + unit;
    return money(v);
}
inline std::string pct(const Fraction& v) { return fmt_f(v.to_double() * 100, 2) + "%"; }
inline std::string price(const Fraction& v) { return "$" + fmt_f(v.to_double(), 4); }
inline std::string whole(const BigInt& v) { return commas(v); }
inline BigInt floor_shares(const Fraction& v) { return v.floor(); }
inline Val num(const Fraction& v, int places = 6) { return Val::flt(round_n(v.to_double(), places)); }
inline std::string gfmt(double x) { char b[64]; std::snprintf(b, sizeof b, "%g", x); return b; }   // "%g" % x

// ------------------------------------------------------------ the math
inline std::pair<std::vector<Fraction>, Fraction> safe_ownership(const std::vector<Safe>& safes) {
    for (const auto& s : safes)
        if (!s.cap) throw TermsError((s.name ? *s.name : std::string("a SAFE")) + " has no cap, so what it owns depends on the next round's price: add --round-pre and "
                                     "--round-raise");
    std::vector<Fraction> rows; Fraction total(0);
    for (const auto& s : safes) { rows.push_back(s.investment / *s.cap); total += rows.back(); }
    if (total >= Fraction(1)) throw TermsError("these SAFEs would own " + pct(total) + " of the company: that leaves nothing for you");
    return {rows, total};
}
struct Conv { Safe s; std::optional<Fraction> cap_price; Fraction discount_price, price; std::string via; Fraction shares; };
struct Res { Fraction pps, fd_pre, company_cap, pool_topup, new_shares, fd_post, post_money, pool_after; std::vector<Conv> safes; };
inline Res solve_round(const BigInt& S, const BigInt& U, const std::vector<Safe>& safes, const Fraction& V, const Fraction& N, const Fraction& P) {
    std::vector<bool> at_cap; for (const auto& s : safes) at_cap.push_back(s.cap.has_value());
    const Fraction one(1);
    Fraction F, K, top, pps;
    const unsigned long long tries = safes.size() < 60 ? (1ULL << safes.size()) + 2 : ~0ULL;
    bool settled = false;
    for (unsigned long long it = 0; it < tries; ++it) {
        Fraction R(0), D(0);
        for (std::size_t k = 0; k < safes.size(); ++k) if (at_cap[k]) R += safes[k].investment / *safes[k].cap;
        for (std::size_t k = 0; k < safes.size(); ++k) if (!at_cap[k]) D += safes[k].investment / (V * (one - safes[k].discount));
        if (R >= one) throw TermsError("the SAFEs' caps add up to " + pct(R) + " of the company");
        const Fraction a = one - P * (V + N) / V;
        const Fraction den = a - D / (one - R);
        if (den <= Fraction(0)) throw TermsError("a " + pct(P) + " pool and these SAFEs leave nothing for the existing holders");
        F = (Fraction(S) / (one - R) - Fraction(U)) / den;
        K = (Fraction(S) + D * F) / (one - R);
        top = F - K;
        if (top < Fraction(0) || P.is_zero()) {
            if (one - R - D <= Fraction(0)) throw TermsError("these SAFEs leave nothing for the existing holders");
            F = Fraction(S) / (one - R - D);
            K = F; top = Fraction(0);
        }
        pps = V / F;
        std::vector<bool> nw;
        for (const auto& s : safes) nw.push_back(s.cap.has_value() && *s.cap / K <= pps * (one - s.discount));
        if (nw == at_cap) { settled = true; break; }
        at_cap = nw;
    }
    if (!settled) throw TermsError("the SAFE conversion did not settle; check the inputs");
    Res r;
    for (std::size_t k = 0; k < safes.size(); ++k) {
        const Safe& s = safes[k];
        Conv c{s, std::nullopt, pps * (one - s.discount), Fraction(0), "", Fraction(0)};
        if (s.cap) c.cap_price = *s.cap / K;
        c.price = at_cap[k] ? *c.cap_price : c.discount_price;
        c.via = at_cap[k] ? "cap" : (!s.discount.is_zero() ? "discount" : "round price");
        c.shares = s.investment / c.price;
        r.safes.push_back(c);
    }
    r.pps = pps; r.fd_pre = F; r.company_cap = K; r.pool_topup = top; r.new_shares = N / pps;
    r.fd_post = F + r.new_shares; r.post_money = V + N; r.pool_after = Fraction(U) + top;
    return r;
}
struct Holder { std::string name; Val shares_val; BigInt shares; };
struct Row { std::string name; std::optional<BigInt> before; Val before_val; BigInt after; Val after_val; };
inline std::vector<Row> cap_table(const std::vector<Holder>& holders, const BigInt& U, const Res& res, const std::string& investor = "New investors") {
    std::vector<Row> rows;
    for (const auto& h : holders) rows.push_back({h.name, h.shares, h.shares_val, h.shares, h.shares_val});
    const BigInt topf = floor_shares(res.pool_topup);
    if (!U.is_zero() || !topf.is_zero()) rows.push_back({"Option pool (unissued)", U, int_val(U), U + topf, int_val(U + topf)});
    for (std::size_t i = 0; i < res.safes.size(); ++i) {
        const auto& s = res.safes[i];
        const BigInt sh = floor_shares(s.shares);
        rows.push_back({"SAFE: " + (s.s.name ? *s.s.name : "SAFE " + std::to_string(i + 1)), std::nullopt, Val(), sh, int_val(sh)});
    }
    const BigInt ns = floor_shares(res.new_shares);
    rows.push_back({investor, std::nullopt, Val(), ns, int_val(ns)});
    return rows;
}
inline std::vector<std::string> render_table(const std::vector<Row>& rows) {
    BigInt before_total(0), after_total(0);
    for (const auto& r : rows) { if (r.before) before_total = before_total + *r.before; after_total = after_total + r.after; }
    std::size_t w = 5; for (const auto& r : rows) w = std::max(w, cplen(r.name));
    std::vector<std::string> out;
    out.push_back("  " + ljust("", w) + "  " + rjust("before", 14) + " " + rjust("", 8) + "  " + rjust("after", 14) + " " + rjust("", 8));
    for (const auto& r : rows) {
        const std::string bs = r.before ? rjust(whole(*r.before), 14) + " " + rjust(!before_total.is_zero() ? pct(Fraction(*r.before, before_total)) : "-", 8)
                                        : rjust("-", 14) + " " + rjust("-", 8);
        out.push_back("  " + ljust(r.name, w) + "  " + bs + "  " + rjust(whole(r.after), 14) + " " + rjust(pct(Fraction(r.after, after_total)), 8));
    }
    out.push_back("  " + ljust("Total", w) + "  " + rjust(whole(before_total), 14) + " " + rjust("100.00%", 8) + "  " + rjust(whole(after_total), 14) + " " + rjust("100.00%", 8));
    return out;
}
inline void holders_from(const Val& a, std::vector<Holder>& holders, BigInt& U, std::vector<Safe>& safes) {
    const std::string ct = S(a, "cap_table");
    if (!ct.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(fspath(ct), ec)) throw TermsError("no such file: " + ct);
        Val doc;
        try { doc = loads(read_text(ct)); }
        catch (const OSError&) { throw; }
        catch (const PyErr& e) {
            if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw TermsError(ct + " is not valid JSON: " + e.what());
            throw;
        }
        if (doc.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(doc) + "' object has no attribute 'get'");
        const Val hs = get(doc, "holders");
        for (const Val& h : items_of(truthy(hs) ? hs : Val::list())) {
            if (h.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(h) + "' object has no attribute 'get'");
            const Val sh = get(h, "shares");
            if (!truthy(get(h, "name")) || !(sh.t == Val::INT || sh.t == Val::BOOL) || (sh.t == Val::INT && big_of(sh).negative()))
                throw TermsError(ct + ": every holder needs a name and whole shares");
            const Val nm = get(h, "name");
            if (nm.t != Val::STR) throw PyErr("TypeError", std::string("object of type '") + type_name(nm) + "' has no len()");
            holders.push_back({nm.s, sh, sh.t == Val::BOOL ? BigInt(sh.b ? 1 : 0) : big_of(sh)});
        }
        if (holders.empty()) throw TermsError(ct + " has no holders");
        const Val pu = get(doc, "pool_unissued");
        U = truthy(pu) ? py_int_val(pu) : BigInt(0);
        const Val ss = get(doc, "safes");
        for (const Val& s : items_of(truthy(ss) ? ss : Val::list())) {
            if (s.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(s) + "' object has no attribute 'get'");
            safes.push_back(make_safe(get(s, "name"), get(s, "investment"), get(s, "cap"), get(s, "discount")));
        }
        return;
    }
    const BigInt Sh = parse_shares(get(a, "shares"), "--shares");
    if (Sh.is_zero() || Sh.negative()) throw TermsError("--shares must be more than zero");
    holders.push_back({"Founders and existing holders", int_val(Sh), Sh});
    U = parse_shares(get(a, "pool_now"), "--pool-now");
}
inline std::string write_out(const Val& a, const std::string& name, const Val& data, const std::optional<std::string>& text = std::nullopt) {
    const std::string folder = join_path(S(a, "dir"), "terms");
    makedirs(folder);
    const std::string path = join_path(folder, name);
    if (text) write_text(path, *text);
    else {
        DumpOpts o; o.indent = 2;
        const std::string tmp = path + ".tmp";
        write_text(tmp, dumps(data, o) + "\n");
        os_replace(tmp, path);
    }
    return path;
}
inline BigInt total_shares(const std::vector<Holder>& hs) { BigInt t(0); for (const auto& h : hs) t = t + h.shares; return t; }
inline std::string safe_label(const Conv& s, std::size_t i) { return s.s.name ? *s.s.name : "SAFE " + std::to_string(i); }
inline std::vector<std::string> round_lines(const std::vector<Holder>& holders, const BigInt& U, const Fraction& V, const Fraction& N, const Fraction& P, const Res& res) {
    const BigInt Sall = total_shares(holders) + U;
    std::vector<std::string> lines = {
        "Priced round: " + money(N) + " of new money at " + money(V) + " pre-money (" + money(V + N) + " post-money)",
        "  Price per share  " + price(res.pps) + "  (" + money(V) + " / " + whole(floor_shares(res.fd_pre)) + " fully diluted pre-money shares)",
        "  New shares       " + whole(floor_shares(res.new_shares)) + " for the new investors (" + pct(res.new_shares / res.fd_post) + " of the company after)"};
    if (!P.is_zero()) lines.push_back("  Option pool      " + whole(floor_shares(res.pool_topup)) + " new shares, so the unissued pool is " + pct(P) + " after the round");
    for (std::size_t i = 0; i < res.safes.size(); ++i) {
        const Conv& s = res.safes[i];
        const std::string cap = s.cap_price ? "cap " + price(*s.cap_price) + " a share" : "no cap";
        const std::string disc = !s.s.discount.is_zero() ? "discount " + price(s.discount_price) + " a share" : "round price " + price(res.pps);
        lines.push_back("  " + safe_label(s, i + 1) + ", " + money(s.s.investment) + ": " + cap + " vs " + disc + ", converts at the " + s.via + ": " +
                        whole(floor_shares(s.shares)) + " shares (" + pct(s.shares / res.fd_post) + " after)");
    }
    lines.push_back("");
    for (auto& l : render_table(cap_table(holders, U, res))) lines.push_back(l);
    lines.push_back("");
    const BigInt existing = total_shares(holders);
    const Fraction before(existing, Sall);
    const Fraction after = Fraction(existing) / res.fd_post;
    if (!res.pool_topup.is_zero()) {
        const BigInt cost = (res.pool_topup * res.pps).round_half_even();
        lines.push_back("The option-pool shuffle: the new pool is counted in the pre-money, so its " + whole(floor_shares(res.pool_topup)) + " shares (" + money_i(cost) +
                        " at the round price) come out of the existing holders' side, not the new investors'. Effective pre-money: " + money(V) + " - " +
                        money_i(cost) + " = " + money(V - Fraction(cost)) + ".");
        lines.push_back("Ask for the pool to be sized to a real 18-month hiring plan, not a round number.");
    }
    if (!res.safes.empty()) {
        BigInt conv(0); Fraction sum(0);
        for (const auto& s : res.safes) { conv = conv + floor_shares(s.shares); sum += s.shares; }
        lines.push_back("The SAFEs convert into " + whole(conv) + " shares worth " + money_i((sum * res.pps).round_half_even()) + " at the round price, also inside the pre-money.");
    }
    lines.push_back("Existing holders go from " + pct(before) + " to " + pct(after) + " (diluted by " + pct(Fraction(1) - after / before) + " of what they had).");
    return lines;
}
inline Val opt_name(const std::optional<std::string>& n) { return n ? sv(*n) : Val(); }
inline Val round_json(const std::vector<Holder>& holders, const BigInt& U, const Fraction& V, const Fraction& N, const Fraction& P, const Res& res) {
    Val safes = Val::list();
    for (const auto& s : res.safes)
        safes.l->push_back(obj({{"name", opt_name(s.s.name)}, {"investment", num(s.s.investment, 2)}, {"cap", s.s.cap ? num(*s.s.cap, 2) : Val()},
                                {"discount", num(s.s.discount)}, {"converts_at", sv(s.via)}, {"price", num(s.price)}, {"shares", int_val(floor_shares(s.shares))},
                                {"ownership_after", num(s.shares / res.fd_post)}}));
    Val table = Val::list();
    for (const auto& r : cap_table(holders, U, res)) table.l->push_back(obj({{"holder", sv(r.name)}, {"before", r.before_val}, {"after", r.after_val}}));
    return obj({{"pre_money", num(V, 2)}, {"new_money", num(N, 2)}, {"post_money", num(V + N, 2)}, {"pool_target", num(P)}, {"price_per_share", num(res.pps)},
                {"fd_pre_money_shares", int_val(floor_shares(res.fd_pre))}, {"pool_topup_shares", int_val(floor_shares(res.pool_topup))},
                {"new_investor_shares", int_val(floor_shares(res.new_shares))}, {"fd_post_money_shares", int_val(floor_shares(res.fd_post))},
                {"effective_pre_money", num(V - res.pool_topup * res.pps, 2)}, {"safes", safes}, {"table", table}, {"note", sv(NOTE)}});
}

// ------------------------------------------------------------ commands
inline void emit(const std::vector<std::string>& lines) { print(join(lines, "\n")); }
inline int cmd_safe(const Val& a) {
    const Val nm = get(a, "name");
    const Safe mine = make_safe(truthy(nm) ? nm : sv("This SAFE"), get(a, "investment"), get(a, "cap"), get(a, "discount"));
    std::vector<Safe> others;
    for (const Val& x : items_of(get(a, "other"))) others.push_back(parse_safe(x.s));
    std::vector<Safe> safes = {mine}; for (const auto& s : others) safes.push_back(s);
    Val out = obj({{"instrument", sv("post-money SAFE")}, {"safes", Val::list()}, {"note", sv(NOTE)}});
    std::vector<std::string> lines = {"Post-money SAFE: " + money(mine.investment) + (mine.cap ? " at a " + money(*mine.cap) + " post-money cap" : std::string(", no cap")) +
                                      (!mine.discount.is_zero() ? ", " + pct(mine.discount) + " discount" : std::string())};
    bool all_capped = true; for (const auto& s : safes) if (!s.cap) all_capped = false;
    if (all_capped) {
        const auto [rows, total] = safe_ownership(safes);
        const Fraction r = rows[0];
        lines.push_back("  At the cap: " + money(mine.investment) + " / " + money(*mine.cap) + " = " + pct(r) +
                        " of the company when it converts, before the next round's new money and any new option pool dilute everyone.");
        if (!others.empty()) {
            Fraction inv(0); for (const auto& s : safes) inv += s.investment;
            lines.push_back("All SAFEs together: " + money(inv) + " for " + pct(total) + ":");
            for (std::size_t k = 0; k < safes.size(); ++k)
                lines.push_back("  " + ljust(safes[k].name ? *safes[k].name : "SAFE", 14) + " " + money(safes[k].investment) + " at " + short_m(*safes[k].cap) + " = " + pct(rows[k]));
        }
        lines.push_back("Existing holders keep " + pct(Fraction(1) - total) + " of the company (from 100.00%) once " + (others.empty() ? "it" : "they") + " convert" +
                        (others.empty() ? "s" : "") + ".");
        lines.push_back("With post-money caps each SAFE's percentage is fixed, so every SAFE you add dilutes you, not the other SAFE holders.");
        Val sl = Val::list();
        for (std::size_t k = 0; k < safes.size(); ++k)
            sl.l->push_back(obj({{"name", opt_name(safes[k].name)}, {"investment", num(safes[k].investment, 2)}, {"cap", num(*safes[k].cap, 2)},
                                 {"discount", num(safes[k].discount)}, {"ownership_at_cap", num(rows[k])}}));
        out.d->set("ownership_at_cap", num(r)); out.d->set("all_safes", num(total)); out.d->set("existing_after_safes", num(Fraction(1) - total));
        out.d->set("safes", sl);
    } else if (!truthy(get(a, "round_pre"))) {
        throw TermsError("a SAFE with no cap owns whatever the next round's price gives it: add --round-pre and --round-raise to see it");
    }
    if (truthy(get(a, "round_pre")) || truthy(get(a, "round_raise"))) {
        if (!(truthy(get(a, "round_pre")) && truthy(get(a, "round_raise")))) throw TermsError("a priced round needs both --round-pre and --round-raise");
        const Fraction V = parse_money(get(a, "round_pre"), "--round-pre");
        const Fraction N = parse_money(get(a, "round_raise"), "--round-raise");
        const Fraction P = truthy(get(a, "pool")) ? parse_pct(get(a, "pool"), "--pool", Fraction(BigInt(1), BigInt(2))) : Fraction(0);
        std::vector<Holder> holders; BigInt U(0); std::vector<Safe> ignored;
        holders_from(a, holders, U, ignored);
        const Res res = solve_round(total_shares(holders) + U, U, safes, V, N, P);
        lines.push_back("");
        for (auto& l : round_lines(holders, U, V, N, P, res)) lines.push_back(l);
        out.d->set("round", round_json(holders, U, V, N, P, res));
    }
    lines.push_back(NOTE);
    emit(lines);
    if (B(a, "write")) print("Wrote " + write_out(a, "safe.json", out) + ".");
    return 0;
}
inline int cmd_round(const Val& a) {
    const Fraction V = parse_money(get(a, "pre"), "--pre");
    const Fraction N = parse_money(get(a, "raise_"), "--raise");
    const Fraction P = !get(a, "pool").is_none() ? parse_pct(get(a, "pool"), "--pool", Fraction(BigInt(1), BigInt(2))) : Fraction(0);
    std::vector<Holder> holders; BigInt U(0); std::vector<Safe> safes;
    holders_from(a, holders, U, safes);
    for (const Val& x : items_of(get(a, "safe"))) safes.push_back(parse_safe(x.s));
    const Res res = solve_round(total_shares(holders) + U, U, safes, V, N, P);
    std::vector<std::string> lines = round_lines(holders, U, V, N, P, res);
    lines.push_back(NOTE);
    emit(lines);
    if (B(a, "write")) print("Wrote " + write_out(a, "round.json", round_json(holders, U, V, N, P, res)) + ".");
    return 0;
}

// ------------------------------------------------------------ the clause scanner
inline const std::vector<std::pair<std::string, int>>& NUMWORDS() {
    static const std::vector<std::pair<std::string, int>> w = {{"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8},
        {"nine", 9}, {"ten", 10}, {"fourteen", 14}, {"fifteen", 15}, {"twenty", 20}, {"twenty-one", 21}, {"twenty-four", 24}, {"thirty", 30}, {"thirty-one", 31},
        {"forty", 40}, {"forty-five", 45}, {"forty-eight", 48}, {"sixty", 60}, {"seventy-two", 72}, {"ninety", 90}, {"one hundred twenty", 120}};
    return w;
}
inline const std::string& NUM() {
    static const std::string n = [] {
        std::vector<std::string> ks; for (const auto& [k, v] : NUMWORDS()) ks.push_back(re::escape(k));
        std::stable_sort(ks.begin(), ks.end(), [](const std::string& x, const std::string& y) { return x.size() > y.size(); });
        return "(\\d+(?:\\.\\d+)?|" + join(ks, "|") + ")";
    }();
    return n;
}
inline double to_num(const std::string& s0) {
    const std::string s = lower(s0);
    for (const auto& [k, v] : NUMWORDS()) if (k == s) return v;
    return std::strtod(ascii_digits(s).c_str(), nullptr);
}
inline std::optional<double> amount_before(const std::string& unit, const std::string& text) {
    const re::Match m = re::search("\\b" + NUM() + R"(\s*(?:\(\s*(\d+(?:\.\d+)?)\s*\)\s*)?(?:business\s+|calendar\s+)?)" + unit, text);
    if (!m) return std::nullopt;
    return m.has(2) && !m.group(2).empty() ? std::strtod(ascii_digits(m.group(2)).c_str(), nullptr) : to_num(m.group(1));
}
inline bool rx(const std::string& p, const std::string& s) { return static_cast<bool>(re::search(p, s)); }
inline std::vector<std::string> chunks(const std::string& text) {
    static const std::string HEADING = R"(^\s*(\d+(\.\d+)*[.)]\s+[A-Z]|[A-Z][A-Za-z0-9 /&'()-]{2,48}:\s))";
    std::vector<std::string> out, cur;
    auto flush_cur = [&] { out.push_back(join(split_ws(join(cur, " ")), " ")); };
    for (const auto& line : split(replace_all(text, "\r\n", "\n"), "\n")) {
        const bool empty_l = strip(line).empty();
        if (empty_l || (re::match(HEADING, line) && !cur.empty())) {
            if (!cur.empty()) flush_cur();
            cur.clear(); if (!empty_l) cur.push_back(line);
        } else cur.push_back(line);
    }
    if (!cur.empty()) flush_cur();
    std::vector<std::string> o; for (auto& c : out) if (!c.empty()) o.push_back(c);
    return o;
}
inline std::string excerpt(const std::string& chunk, const std::string& pattern, long width = 240) {
    std::vector<std::string> parts = re::split(R"((?<=\.)\s+(?=[A-Z(]))", chunk); parts.push_back(chunk);
    for (const auto& sentence : parts) {
        const re::Match m = re::search(pattern, lower(sentence));
        if (!m) continue;
        const long n = static_cast<long>(cplen(sentence));
        if (n <= width) return sentence;
        const long start = std::max(0L, std::min(m.start(0) - width / 3, n - width));
        const std::string out = strip(cpslice(sentence, start, start + width));
        return (start ? "..." : "") + out + (start + width < n ? "..." : "");
    }
    return cpslice(chunk, 0, width);
}
inline Val finding(const std::string& key, const std::string& clause, const std::string& severity, const std::string& note, const std::string& ask,
                   const std::string& chunk, const std::string& pattern) {
    return obj({{"key", sv(key)}, {"clause", sv(clause)}, {"severity", sv(severity)}, {"note", sv(note)}, {"ask", sv(ask)}, {"excerpt", sv(excerpt(chunk, pattern))}});
}
inline std::optional<long long> n_of(const std::string& s) {   // _n: a digit, or one..nine
    if (isdigit_str(s)) return std::stoll(ascii_digits(s));
    static const char* W[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    for (int k = 0; k < 9; ++k) if (s == W[k]) return k + 1;
    return std::nullopt;
}
inline List board_findings(const std::string& c, const std::string& low) {
    const std::string seat = R"(\b(\d|one|two|three|four|five|six|seven)\s*(?:\(\d\)\s*)?(?:(?:directors?|members?|seats?|persons?|)"
                             R"(individuals?)\s+)?(?:shall be |to be |will be |who shall be |who will be )?(?:designated|elected|)"
                             R"(appointed|nominated|chosen|selected)\s+by\s+(?:the\s+)?((?:(?! and )[a-z -]){0,50}))";
    long long inv = 0, fnd = 0, ind = 0;
    for (const auto& m : re::compile(seat).finditer(low)) {
        const long long n = n_of(m.group(1)).value_or(0);
        const std::string who = m.group(2);
        if (rx("common|founder", who)) fnd += n;
        else if (rx("mutual|both|independent|board", who)) ind += n;
        else if (rx("investor|preferred|series|lead|purchaser", who)) inv += n;
    }
    for (const auto& m : re::compile(R"(\b(\d|one|two|three)\s*(?:\(\d\)\s*)?independent)").finditer(low)) ind = std::max(ind, n_of(m.group(1)).value_or(0));
    const re::Match tm = re::search(R"((?:consist of|comprise|composed of|size of|fixed at)\s+(\d|one|two|three|four|five|six|seven)\b)", low);
    long long total = tm ? n_of(tm.group(1)).value_or(0) : 0;
    if (!total) total = inv + fnd + ind;
    auto I = [](long long v) { return std::to_string(v); };
    if (!total || !(inv || fnd))
        return {finding("board", "Board composition", "medium",
                        "The board decides most things, including hiring and firing the CEO. The seats could not be counted from the text.",
                        "Who designates each seat, and who controls the board after this round?", c, "board")};
    if (inv * 2 > total)
        return {finding("board", "Board control: investors " + I(inv) + " of " + I(total) + " seats", "high",
                        "The investors would hold a majority of the board, so they could replace you as CEO.",
                        "Can the founders keep a majority (or at least parity) at seed, with an independent seat chosen by both sides?", c, "board|director")};
    if (inv * 2 == total || ((inv + ind) * 2 > total && fnd * 2 <= total))
        return {finding("board", "Board: investors " + I(inv) + ", founders " + I(fnd) + ", independent " + I(ind) + " of " + I(total), "medium",
                        "The founders do not hold a majority on their own; the independent seat or a tie decides.",
                        "Who picks the independent director, and can the founders keep a majority at seed?", c, "board|director")};
    return {finding("board", "Board: founders " + I(fnd) + " of " + I(total) + " seats", "standard", "The founders keep a majority of the board. Standard at seed.", "", c,
                    "board|director")};
}
inline List scan_chunk(const std::string& c) {
    const std::string low = lower(c);
    List out;
    auto has = [&](const std::string& w) { return low.find(w) != std::string::npos; };
    if (rx("liquidation|preference", low)) {
        const re::Match m = re::search(R"(\b(\d+(?:\.\d+)?)\s*(?:x|×)(?![a-z]))", low);
        std::optional<double> mult = m ? std::optional<double>(std::strtod(ascii_digits(m.group(1)).c_str(), nullptr)) : amount_before("times", low);
        if (!mult && rx("original (purchase|issue) price", low) && has("liquidation")) mult = 1.0;
        if (mult && has("liquidation")) {
            if (*mult > 1) {
                const std::string g = gfmt(*mult);
                out.push_back(finding("liquidation_preference", "Liquidation preference " + g + "x", "high",
                                      "In a sale, the investors take back " + g + " times their money before common holders (you) see a dollar. 1x is the norm at seed.",
                                      "Can we get this to 1x non-participating, and what would " + g + "x leave us in a sale at a modest price?", c, R"(\d\s*x|times|liquidation)"));
            } else {
                out.push_back(finding("liquidation_preference", "Liquidation preference 1x", "standard",
                                      "1x: investors get their money back first, then the rest is shared. Standard.", "", c, "liquidation"));
            }
        }
        if (rx("non[- ]?participating|shall not participate|will not participate|does not participate", low)) {
            out.push_back(finding("participating", "Non-participating preferred", "standard", "They take the preference or convert to common, not both. Standard.", "", c,
                                  "participat"));
        } else if (rx("participat", low) && (has("liquidation") || has("as-converted") || has("as converted"))) {
            const bool capped = rx(R"(cap(ped)?\s+(at\s+)?(\d+(\.\d+)?)\s*(x|times)|until .{0,40}(\d+(\.\d+)?)\s*(x|times))", low);
            out.push_back(finding("participating", std::string("Participating preferred") + (capped ? " (capped)" : ""), capped ? "medium" : "high",
                                  std::string("They get their money back AND a share of what is left, as if they had converted: paid twice in a sale.") +
                                      (capped ? " A cap limits it, but it is still unusual at seed." : ""),
                                  "Can this be non-participating? If not, how big must a sale be before participation stops mattering?", c, "participat"));
        }
    }
    if (has("dividend")) {
        if (rx("non[- ]?cumulative", low) || rx("(when|if),? as,? and (if|when) declared", low)) {
            out.push_back(finding("dividends", "Non-cumulative dividends", "standard", "Dividends only if the board declares them. Standard.", "", c, "dividend"));
        } else if (rx("cumulative|accru", low)) {
            out.push_back(finding("dividends", "Cumulative dividends", "high",
                                  "A dividend that piles up every year whether or not it is paid, and is owed before common holders get anything in a sale.",
                                  "Can the dividends be non-cumulative, payable only when declared by the board?", c, "cumulative|accru"));
        }
    }
    if (rx("dilution|ratchet|conversion price", low)) {
        if (rx("full[- ]ratchet", low)) {
            out.push_back(finding("anti_dilution", "Full-ratchet anti-dilution", "high",
                                  "If you ever raise at a lower price, their price drops all the way to it, however few shares are sold. The dilution lands on you.",
                                  "Can we use broad-based weighted average anti-dilution instead of full ratchet?", c, "ratchet"));
        } else if (rx("narrow[- ]based", low)) {
            out.push_back(finding("anti_dilution", "Narrow-based weighted average anti-dilution", "medium",
                                  "A weighted average that counts fewer shares, so a down round hits you harder than the usual broad-based version.",
                                  "Can this be broad-based weighted average?", c, "narrow"));
        } else if (rx("broad[- ]based", low)) {
            out.push_back(finding("anti_dilution", "Broad-based weighted average anti-dilution", "standard", "The usual protection against a down round. Standard.", "", c, "broad"));
        }
    }
    if (rx("redemption|redeem", low)) {
        if (rx(R"(no redemption|not (be )?redeemable|non[- ]?redeemable|shall not be redeem|redemption( rights)?:?\s*none)", low)) {
            out.push_back(finding("redemption", "No redemption rights", "standard", "Investors cannot force the company to buy their shares back. Standard.", "", c,
                                  "redempt|redeem"));
        } else {
            out.push_back(finding("redemption", "Redemption rights", "high",
                                  "After a set time the investors can make the company buy their shares back for cash, money a young company rarely has. Rare at seed.",
                                  "Can we strike redemption rights? If not, what happens if the company cannot pay?", c, "redempt|redeem"));
        }
    }
    if (has("board") && rx("director|seat|member|consist|compos", low))
        for (const Val& f : board_findings(c, low)) out.push_back(f);
    if (rx("protective provision|veto", low) ||
        (rx(R"((consent|approval|vote) of (the )?(holders of )?(at least )?(a majority|\d+%|[a-z-]+ percent))", low) && rx("preferred|investor|series", low) &&
         rx("(shall|will) not|without", low))) {
        const re::Match ops = re::search(R"(\b(budget|hir(e|ing)|terminat\w* any employee|salar|compensation|capital expenditure|indebtedness|debt in excess|operating plan|any contract|key employee|change (the|its) business))", low);
        if (ops) {
            out.push_back(finding("protective_provisions", "Protective provisions with operating vetoes", "high",
                                  "The investors' consent is needed for day-to-day decisions (" + ops.group(0) + "), not just for selling the company or issuing new senior shares.",
                                  "Can the vetoes be limited to the standard list (charter changes, new senior shares, a sale, dividends) and leave running the business to the board?",
                                  c, ops.group(0)));
        } else {
            out.push_back(finding("protective_provisions", "Protective provisions (vetoes)", "low",
                                  "The investors can block certain big moves: a sale, new senior shares, charter changes. Normal, but know the list.",
                                  "Is this the standard list, and does the vote need a majority of the preferred together (not each series on its own)?", c,
                                  "protective|consent|veto|approval"));
        }
    }
    if (rx("pay[- ]to[- ]play", low)) {
        out.push_back(finding("pay_to_play", "Pay-to-play", "medium",
                              "Investors who do not join later rounds lose rights or get converted to common. It mostly protects you, but it can make the next round harder to put together.",
                              "Who does this apply to, and what exactly do non-participating investors lose?", c, "pay[- ]to[- ]play"));
    }
    if (rx("drag[- ]?along", low)) {
        const bool common = rx("common|founder|board", low);
        out.push_back(finding("drag_along", "Drag-along", common ? "medium" : "high",
                              std::string("A big enough group can force everyone, you included, to vote for a sale.") + (common ? "" : " Here the preferred alone seem to be able to trigger it."),
                              "Who has to approve a drag (the board, the common, the founders)? Is there a minimum price, and is it limited to the same price per share for everyone?",
                              c, "drag"));
    }
    if (rx("no[- ]shop|exclusiv", low)) {
        const auto days = amount_before("days?", low);
        if (!days) {
            out.push_back(finding("no_shop", "No-shop with no end date", "medium", "You agree not to talk to other investors, and no end date was found.",
                                  "Can the no-shop end after 30 days, and end early if they walk away?", c, "no[- ]shop|exclusiv"));
        } else if (*days > 30) {
            const std::string g = gfmt(*days);
            out.push_back(finding("no_shop", "No-shop of " + g + " days", *days >= 60 ? "high" : "medium",
                                  g + " days of not talking to anyone else gives them time and you no leverage. 30 days or less is usual.",
                                  "Can the no-shop be 30 days, ending early if they walk away?", c, "no[- ]shop|exclusiv"));
        } else {
            out.push_back(finding("no_shop", "No-shop of " + gfmt(*days) + " days", "standard", "30 days or less. Standard.", "", c, "no[- ]shop|exclusiv"));
        }
    }
    if (rx(R"((offer|term sheet|terms?|proposal)\s+(\w+\s+){0,3}(expire|lapse|terminate|be void|be withdrawn)|exploding|(must|is to) be (signed|accepted|executed) (by|within|no later)|unless (signed|accepted|executed))", low)) {
        const auto hours = amount_before("hours?", low);
        const auto days = amount_before(R"((business\s+)?days?)", low);
        std::string sev, how;
        if ((hours && *hours <= 48) || (days && *days <= 2)) { sev = "high"; how = gfmt(hours ? *hours : *days * 24) + " hours"; }
        else if (hours || (days && *days <= 7)) { sev = "medium"; how = hours ? gfmt(*hours) + " hours" : gfmt(*days) + " days"; }
        else if (days) { sev = "low"; how = gfmt(*days) + " days"; }
        else { sev = "medium"; how = "a fixed date"; }
        out.push_back(finding("exploding", "Exploding deadline (" + how + ")", sev,
                              sev != "low" ? "A short deadline is pressure to sign before you have shopped the terms or had a lawyer read them."
                                           : "A deadline of " + how + ": workable, but the clock runs while you compare offers and your lawyer reads it.",
                              "Can we have a week or two to review this with counsel and the other investors?", c, "expire|lapse|exploding|signed|accepted|executed"));
    }
    if (rx("vest", low) && rx("founder|common", low) &&
        rx(R"(re-?vest|reset|(subject to|become subject to|be subject to) (a )?(new |four[- ]year |4[- ]year |forty-eight[- ]month )?vesting|vesting (schedule )?(shall |will )?(commenc\w*|start\w*|begin\w*) (on|at|upon|from) (the )?(initial )?closing|(shall|will) be (treated as )?unvested)", low)) {
        const bool credit = rx(R"(credit for|vesting credit|(excluding|except|other than) (shares |any shares )?already vested|time (already )?served)", low);
        out.push_back(finding("vesting_reset", "Founder vesting reset", credit ? "medium" : "high",
                              std::string("Shares you already earned go back on a vesting clock that starts at the closing.") +
                                  (credit ? " Some credit for time served is mentioned: check how much." : ""),
                              "Can we keep credit for the time already served (or the part already vested), with double-trigger acceleration if we are acquired and let go?", c,
                              "vest"));
    }
    if (rx(R"(super[- ]pro[- ]rata|more than (its|their) pro[- ]rata|(\d+(\.\d+)?|two|three)\s*(x|times) (its|their) pro[- ]rata)", low)) {
        out.push_back(finding("super_pro_rata", "Super pro rata", "medium", "The right to buy MORE than their share of later rounds. It can crowd out the next lead investor.",
                              "Can this be a standard pro rata right, limited to major investors?", c, "pro[- ]rata"));
    }
    if (rx(R"(most[- ]favou?red[- ]nation|\bmfn\b)", low)) {
        out.push_back(finding("mfn", "Most favored nation (MFN)", "low",
                              "If you later give someone better terms (a lower cap, a bigger discount), this holder can take them too. Common in SAFEs; know it before you price the next one.",
                              "Which later terms does the MFN reach, and does it end at the next priced round?", c, "most[- ]favou?red|mfn"));
    }
    if (rx("option pool|equity incentive pool|available pool|stock option plan|unallocated pool", low) && rx("pre[- ]?money", low)) {
        const re::Match size = re::search(R"((\d+(?:\.\d+)?)\s*%)", low);
        out.push_back(finding("pool_shuffle", "New option pool in the pre-money" + (size ? " (" + size.group(1) + "%)" : std::string()), "medium",
                              "The pool is counted before the new money, so it dilutes only you: the option-pool shuffle. Run terms.py round to see what it costs.",
                              "Can the pool be sized to our real 18-month hiring plan, or counted in the post-money?", c, "pool"));
    }
    return out;
}
inline List scan_text(const std::string& text) {
    List found; std::set<std::pair<std::string, std::string>> seen;
    for (const auto& c : chunks(text))
        for (const Val& f : scan_chunk(c)) {
            const auto sig = std::make_pair(get(f, "key").s, get(f, "severity").s);
            if (seen.count(sig)) continue;
            seen.insert(sig); found.push_back(f);
        }
    std::set<std::string> flagged; for (const Val& f : found) if (get(f, "severity").s != "standard") flagged.insert(get(f, "key").s);
    List keep; for (const Val& f : found) if (!(get(f, "severity").s == "standard" && flagged.count(get(f, "key").s))) keep.push_back(f);
    std::stable_sort(keep.begin(), keep.end(), [](const Val& x, const Val& y) {
        const int a = severity_order(get(x, "severity").s), b = severity_order(get(y, "severity").s);
        if (a != b) return a < b;
        return get(x, "clause").s < get(y, "clause").s;
    });
    return keep;
}
struct Counts { long long high = 0, medium = 0, low = 0; };
inline std::vector<std::string> scan_report(const std::string& name, const List& found, Counts& count) {
    List flags; for (const Val& f : found) if (get(f, "severity").s != "standard") flags.push_back(f);
    for (const Val& f : flags) { const std::string s = get(f, "severity").s; if (s == "high") count.high++; else if (s == "medium") count.medium++; else if (s == "low") count.low++; }
    const long long nf = static_cast<long long>(flags.size()), ns = static_cast<long long>(found.size()) - nf;
    std::vector<std::string> lines = {"Term sheet scan: " + name,
        std::to_string(nf) + " flag" + (nf == 1 ? "" : "s") + ": " + std::to_string(count.high) + " high, " + std::to_string(count.medium) + " medium, " +
            std::to_string(count.low) + " low. " + std::to_string(ns) + " standard term" + (ns == 1 ? "" : "s") + " recognised.",
        NOTE, ""};
    for (const Val& f : flags) {
        lines.push_back(ljust(upper(get(f, "severity").s), 6) + " " + get(f, "clause").s);
        lines.push_back("       \"" + get(f, "excerpt").s + "\"");
        lines.push_back("       What it means: " + get(f, "note").s);
        lines.push_back("       Ask your lawyer: \"" + get(f, "ask").s + "\"");
        lines.push_back("");
    }
    std::vector<std::string> std_; for (const Val& f : found) if (get(f, "severity").s == "standard") std_.push_back(get(f, "clause").s);
    if (!std_.empty()) lines.push_back("Standard: " + join(std_, "; ") + ".");
    if (found.empty()) lines.push_back("No clause this scanner knows was found. That is not a clean bill of health: read it with a lawyer.");
    lines.push_back("The scanner reads words, not intent: a clause it does not know is not flagged. Read the whole document with a startup lawyer before you sign.");
    return lines;
}
inline std::string base_name(const std::string& p) {   // os.path.basename
#ifdef _WIN32
    const std::size_t k = p.find_last_of("/\\");
#else
    const std::size_t k = p.rfind('/');
#endif
    return k == std::string::npos ? p : p.substr(k + 1);
}
inline int cmd_scan(const Val& a) {
    const std::string file = S(a, "file");
    std::error_code ec;
    if (!std::filesystem::exists(fspath(file), ec)) throw TermsError("no such file: " + file);
    std::string text = decode_utf8_replace(read_bytes(file));
    text = replace_all(replace_all(text, "\r\n", "\n"), "\r", "\n");   // text mode: universal newlines
    if (strip(text).empty()) throw TermsError(file + " is empty");
    const List found = scan_text(text);
    const std::string name = base_name(file);
    Counts count;
    const std::vector<std::string> lines = scan_report(name, found, count);
    if (B(a, "json")) {
        Val fl = Val::list(); *fl.l = found;
        DumpOpts o; o.indent = 2;
        print(dumps(obj({{"file", sv(name)}, {"flags", fl}, {"counts", obj({{"high", iv(count.high)}, {"medium", iv(count.medium)}, {"low", iv(count.low)}})}, {"note", sv(NOTE)}}), o));
    } else emit(lines);
    if (B(a, "write")) {
        std::string stem = strip_chars(re::sub("[^a-z0-9]+", "-", lower(splitext_root(name))), "-");
        if (stem.empty()) stem = "document";
        std::vector<std::string> md = {"# Term sheet scan: " + name, "", NOTE, ""};
        for (const Val& f : found) {
            if (get(f, "severity").s == "standard") continue;
            for (const std::string& l : {"## " + upper(get(f, "severity").s) + ": " + get(f, "clause").s, std::string(), "> " + get(f, "excerpt").s, std::string(),
                                         "What it means: " + get(f, "note").s, std::string(), "Ask your lawyer: \"" + get(f, "ask").s + "\"", std::string()})
                md.push_back(l);
        }
        std::vector<std::string> std_; for (const Val& f : found) if (get(f, "severity").s == "standard") std_.push_back(get(f, "clause").s);
        if (!std_.empty()) { md.push_back("## Standard terms"); md.push_back(""); for (const auto& s : std_) md.push_back("- " + s); md.push_back(""); }
        print("Wrote " + write_out(a, "scan-" + stem + ".md", Val(), join(md, "\n")) + ".");
    }
    return count.high ? 3 : 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("terms.py", "Term Sheet Reader: SAFE and priced-round math, and a clause scanner. Not legal advice.");
    p.subcommands("cmd", false);
    auto common = [](Parser& s) {
        s.opt({"--dir"}).def("seed").help("the seed folder, for --write (default ./seed)");
        s.opt({"--write"}).store_true().help("save the result in seed/terms/");
    };
    auto shares = [](Parser& s) {
        s.opt({"--shares"}).def("10000000").help("fully diluted shares before the round, without the pool (default 10,000,000; the percentages do not depend on it)");
        s.opt({"--pool-now"}).def("0").help("unissued option pool shares today");
        s.opt({"--cap-table"}).help("JSON: {\"holders\": [{\"name\", \"shares\"}], \"pool_unissued\", \"safes\": [{\"name\", \"investment\", \"cap\", \"discount\"}]}");
    };
    {
        Parser& s = p.sub("safe"); s.help_line = "post-money SAFE ownership, and its conversion at a priced round";
        common(s);
        s.opt({"--investment"}).required();
        s.opt({"--cap"}).help("post-money valuation cap");
        s.opt({"--discount"}).help("e.g. 20 for 20%%");
        s.opt({"--name"});
        s.opt({"--other"}).append().help("another SAFE in the round: [name=]amount@cap[:discount]");
        s.opt({"--round-pre"});
        s.opt({"--round-raise"});
        s.opt({"--pool"}).help("unissued pool target after the round, e.g. 10");
        shares(s);
    }
    {
        Parser& s = p.sub("round"); s.help_line = "a priced round's cap table, with the option-pool shuffle";
        common(s);
        s.opt({"--pre"}).required().help("pre-money valuation");
        s.opt({"--raise"}).dest("raise_").required().help("new money");
        s.opt({"--pool"}).help("unissued pool target after the round, e.g. 10");
        s.opt({"--safe"}).append().help("a converting SAFE: [name=]amount@cap[:discount]");
        shares(s);
    }
    {
        Parser& s = p.sub("scan"); s.help_line = "flag the clauses to ask a lawyer about";
        common(s);
        s.pos("file");
        s.opt({"--json"}).store_true();
    }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 1; }
    try {
        if (cmd == "safe") return cmd_safe(a);
        if (cmd == "round") return cmd_round(a);
        return cmd_scan(a);
    } catch (const TermsError& e) { eprint(std::string("terms.py: ") + e.what()); return 1; }
}
PACKTOOL("seed-terms/terms", main);
}  // namespace packtools::seed_terms
