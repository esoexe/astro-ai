// seed-updates/update: the monthly investor update (TL;DR, the numbers, highlights, lowlights, asks, runway) and the
// short version for warm prospects. Deterministic; nothing is sent. A C++ port of seed-skill/seed-updates/update.py
// (MIT, Jake Schincariol); same output and exit codes: 0 done, 1 refused (a file already there), 2 bad input.
namespace packtools::seed_updates {
using namespace pyrt;
using namespace packtools::seed;

struct UpdateError : std::runtime_error {
    int code;
    explicit UpdateError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {}
};

inline const char* const MONTH_NAMES[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
inline const char* OPT_OUT = "Not a fit any more? Reply 'no' and I won't send these again.";
constexpr int BURN_MONTHS = 3;
inline bool is_unit(const Val& u) { return u.t == Val::STR && (u.s == "usd" || u.s == "count" || u.s == "pct"); }

// ---------------------------------------------------------------- numbers and months
inline Decimal D(const Val& x, const std::string& what = "a number") {
    if (x.t == Val::BOOL || x.is_none()) throw UpdateError(what + " is not a number: " + repr(x));
    try {
        if (x.t == Val::FLOAT) return Decimal::parse(float_repr(x.f));
        std::string t = strip(pystr(x));
        std::size_t k = 0; while (k < t.size() && t[k] == '$') ++k;
        t = t.substr(k);
        std::string u; for (char c : t) if (c != ',') u += c;
        return Decimal::parse(ascii_digits(rstrip_chars(u, "%")));
    } catch (const DecimalError&) { throw UpdateError(what + " is not a number: " + repr(x)); }
}
inline Decimal D(const Decimal& x) { return x; }

inline Decimal rnd(const Decimal& x, long dp) { return x.quantize(Decimal::triple(false, BigInt(1), -dp), Rounding::HALF_UP); }   // Decimal(1).scaleb(-dp)
inline std::string trim(const Decimal& q) { const std::string s = q.format("f"); return s.find('.') != std::string::npos ? rstrip_chars(rstrip_chars(s, "0"), ".") : s; }
inline bool integral(const Decimal& d) { return d == d.to_integral_value(); }

inline std::string money(const Decimal& d) {
    const Decimal q = rnd(d, integral(d) ? 0 : 2);
    return std::string(q < Decimal(0) ? "-" : "") + "$" + q.abs().format(",f");
}
inline std::string money(const Val& x) { return money(D(x)); }
inline std::string short_money(const Decimal& d) {
    if (d.abs() >= Decimal(1000000)) return "$" + trim(rnd(d / Decimal(1000000), d.abs() < Decimal(10000000) ? 2 : 1)) + "M";
    return money(d);
}

struct YM { long long y; int m; };
inline YM parse_month(const Val& text, const std::string& what = "--month") {
    const std::string s = truthy(text) ? pystr(text) : "";
    const re::Match mt = re::match(R"(^(\d{4})-(\d{2})$)", s);
    const int mo = mt ? static_cast<int>(to_ll(mt.group(2))) : 0;
    if (!mt || mo < 1 || mo > 12) throw UpdateError(what + " wants YYYY-MM, like 2026-11 (got " + repr(text) + ")");
    return {to_ll(mt.group(1)), mo};
}
inline YM parse_month(const std::string& text, const std::string& what = "--month") { return parse_month(Val::str(text), what); }
inline std::string month_name(const std::string& ym) { const YM p = parse_month(ym, "month"); return std::string(MONTH_NAMES[p.m - 1]) + " " + std::to_string(p.y); }
inline std::string short_month(const std::string& ym) { const YM p = parse_month(ym, "month"); return std::string(MONTH_NAMES[p.m - 1]).substr(0, 3) + " " + std::to_string(p.y); }
inline std::string pad(const BigInt& n, std::size_t width) {   // "%0Nd"
    const std::string d = n.digits(); const std::size_t w = n.negative() ? width - 1 : width;
    return (n.negative() ? "-" : "") + (d.size() < w ? std::string(w - d.size(), '0') : "") + d;
}
inline std::string add_months(const std::string& ym, const BigInt& k) {
    const YM p = parse_month(ym, "month");
    const BigInt idx = BigInt(p.y) * BigInt(12) + BigInt(p.m - 1) + k;
    const BigInt yy = idx / BigInt(12);   // floor division, like Python
    const BigInt mm = idx - yy * BigInt(12) + BigInt(1);
    return pad(yy, 4) + "-" + pad(mm, 2);
}
inline std::string prev_month(const std::string& ym) { return add_months(ym, BigInt(-1)); }

inline std::string show(const Val& value, const std::string& unit) {
    if (unit == "usd") return money(value);
    const Decimal d = D(value);
    if (unit == "pct") return trim(rnd(d, 2)) + "%";
    return integral(d) ? commas(d.to_int()) : d.format(",f");
}

struct Change { std::string text; int sign; };
inline Change change(const Val& cur, const Val& last, const std::string& unit) {   // this month against last month
    if (last.is_none()) return {"new this month", 0};
    const Decimal c = D(cur), l = D(last);
    if (unit == "pct") {
        const Decimal diff = rnd(c - l, 2);
        if (diff == Decimal(0)) return {"flat", 0};
        return {std::string(diff > Decimal(0) ? "up" : "down") + " " + trim(diff.abs()) + " pts", diff > Decimal(0) ? 1 : -1};
    }
    if (l == Decimal(0)) return c == Decimal(0) ? Change{"flat", 0} : Change{"new this month", 0};
    const Decimal g = rnd((c - l) / l.abs() * Decimal(100), 1);
    if (g == Decimal(0)) return {"flat", 0};
    return {std::string(g > Decimal(0) ? "up" : "down") + " " + trim(g.abs()) + "%", g > Decimal(0) ? 1 : -1};
}
inline std::string mom(const Change& ch) {   // +23.9%, -0.4 pts: the short form for the TL;DR
    if (starts(ch.text, "up ") || starts(ch.text, "down ")) return std::string(ch.sign > 0 ? "+" : "-") + ch.text.substr(ch.text.find(' ') + 1);
    return ch.text;
}

// ---------------------------------------------------------------- the metrics file
inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (!exists(path)) {
        if (required) throw UpdateError("no " + what + " at " + path);
        return Val();
    }
    try {
        return loads(read_text(path));
    } catch (const OSError&) {
        throw;
    } catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw UpdateError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2;   // json.dump(doc, fh, indent=2): ensure_ascii stays on
    write_text(path, dumps(doc, o) + "\n");
}

inline Val values_of(const Val& e) {   // (e.get("values") or {})
    const Val v = get(e, "values");
    if (!truthy(v)) return Val::dict();
    if (v.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'items'");
    return v;
}

inline const Val& validate(const Val& doc) {
    if (doc.t != Val::DICT) throw UpdateError("metrics.json must be a JSON object");
    std::vector<std::string> errs;
    Val metrics = get(doc, "metrics");
    if (metrics.t != Val::LIST || metrics.l->empty()) { errs.push_back("metrics is missing: a list of {key, label, unit}"); metrics = Val::list(); }
    std::vector<std::string> keys; std::set<std::string> str_keys;   // keys for the message; the string ones can match a value key
    for (std::size_t i = 0; i < metrics.l->size(); ++i) {
        const Val& m = (*metrics.l)[i];
        if (m.t != Val::DICT || !truthy(get(m, "key"))) { errs.push_back("metrics[" + std::to_string(i) + "] needs a key"); continue; }
        if (has(m, "unit") && !is_unit(get(m, "unit"))) errs.push_back("metrics[" + std::to_string(i) + "].unit is usd, count or pct (got " + repr(get(m, "unit")) + ")");
        keys.push_back(pystr(get(m, "key"))); if (get(m, "key").t == Val::STR) str_keys.insert(get(m, "key").s);
    }
    std::set<std::string> seen;
    const List months = items_of(get(doc, "months"));
    for (std::size_t i = 0; i < months.size(); ++i) {
        const Val& e = months[i];
        if (e.t != Val::DICT) { errs.push_back("months[" + std::to_string(i) + "] must be an object"); continue; }
        try { parse_month(get(e, "month"), "months[" + std::to_string(i) + "].month"); }
        catch (const UpdateError& x) { errs.push_back(x.what()); continue; }
        const std::string ym = get(e, "month").s;
        if (seen.count(ym)) errs.push_back("months has " + ym + " twice");
        seen.insert(ym);
        const Val vals = values_of(e);
        for (const auto& [k, v] : vals.d->items) {
            if (!str_keys.count(k)) errs.push_back(ym + ": " + str_repr(k) + " is not one of the metrics (" + join(keys, ", ") + ")");
            try { D(v, ym + " " + k); } catch (const UpdateError& x) { errs.push_back(x.what()); }
        }
        for (const char* k : {"cash", "money_in", "net_burn"})
            if (!get(e, k).is_none()) { try { D(get(e, k), ym + " " + k); } catch (const UpdateError& x) { errs.push_back(x.what()); } }
    }
    if (!errs.empty()) throw UpdateError("metrics.json needs fixing:\n  - " + join(errs, "\n  - "));
    return doc;
}

inline Val months_by_key(const Val& doc) {   // {e["month"]: e}
    Val by = Val::dict();
    for (const Val& e : items_of(get(doc, "months"))) by.d->set(pystr(get(e, "month")), e);
    return by;
}

struct Burn { bool known = false; Decimal v; };
inline Burn burn_of(const Val& doc, const std::string& ym) {   // net burn in month ym (positive = cash went out)
    const Val by = months_by_key(doc);
    const Val e = get(by, ym);
    if (!truthy(e)) return {};
    if (!get(e, "net_burn").is_none()) return {true, D(get(e, "net_burn"))};
    const Val last = get(by, prev_month(ym));
    if (get(e, "cash").is_none() || !truthy(last) || get(last, "cash").is_none()) return {};
    const Val mi = get(e, "money_in");
    return {true, D(get(last, "cash")) + (truthy(mi) ? D(mi) : Decimal(0)) - D(get(e, "cash"))};
}

struct Runway { bool has_cash = false; Decimal cash; Burn burn; bool has_avg = false; Decimal avg_burn; long months_used = 0; bool has_runway = false; Decimal runway; std::string until; };
inline Runway runway(const Val& doc, const std::string& ym) {
    const Val e = get(months_by_key(doc), ym);
    Runway out;
    if (!get(e, "cash").is_none()) { out.has_cash = true; out.cash = D(get(e, "cash")); }
    out.burn = burn_of(doc, ym);
    std::vector<Decimal> burns;
    for (int k = 0; k < BURN_MONTHS; ++k) {
        const Burn b = burn_of(doc, add_months(ym, BigInt(-k)));
        if (!b.known) break;
        burns.push_back(b.v);
    }
    if (!burns.empty()) {
        Decimal sum(0); for (const Decimal& b : burns) sum = sum + b;
        out.avg_burn = sum / Decimal(static_cast<long long>(burns.size())); out.has_avg = true;
        out.months_used = static_cast<long>(burns.size());
    }
    if (out.has_cash && out.has_avg && out.avg_burn > Decimal(0)) {
        const Decimal r = out.cash / out.avg_burn;
        out.has_runway = true; out.runway = rnd(r, 1);
        out.until = add_months(ym, r.to_integral_value(Rounding::CEILING).to_int());
    }
    return out;
}

// ---------------------------------------------------------------- the update
struct Row { std::string key, label, unit; Val value, last; std::string change, mom; int sign; };
inline std::vector<Row> metric_rows(const Val& doc, const std::string& ym) {
    const Val by = months_by_key(doc);
    const Val cv = get(get(by, ym), "values"), lv = get(get(by, prev_month(ym)), "values");
    const Val cur = truthy(cv) ? cv : Val::dict(), last = truthy(lv) ? lv : Val::dict();
    std::vector<Row> rows;
    for (const Val& m : *get(doc, "metrics").l) {
        const Val k = get(m, "key");
        if (k.t != Val::STR || get(cur, k.s).is_none()) continue;
        const std::string unit = has(m, "unit") ? get(m, "unit").s : "count";
        const Change ch = change(get(cur, k.s), get(last, k.s), unit);
        const Val label = get(m, "label");
        rows.push_back({k.s, truthy(label) ? pystr(label) : k.s, unit, get(cur, k.s), get(last, k.s), ch.text, mom(ch), ch.sign});
    }
    return rows;
}

inline std::vector<Row> headline(const Val& doc, const std::vector<Row>& rows) {
    List keys;
    const Val h = get(doc, "headline");
    if (truthy(h)) keys = items_of(h);
    else for (std::size_t i = 0; i < rows.size() && i < 3; ++i) keys.push_back(Val::str(rows[i].key));
    std::vector<Row> out;
    for (const Val& k : keys) {
        if (k.t != Val::STR) continue;
        for (const Row& r : rows) if (r.key == k.s) { out.push_back(r); break; }
    }
    return out;
}

inline std::string tldr_line(const Val& doc, const std::vector<Row>& rows, const Runway* rw) {
    std::vector<std::string> parts;
    const std::vector<Row> hl = headline(doc, rows);
    for (std::size_t i = 0; i < hl.size(); ++i) {
        const Row& r = hl[i];
        std::string label = r.label;
        if (i > 0) { const auto c = cps(r.label); if (!c.empty()) label = lower(c[0]) + r.label.substr(c[0].size()); }
        std::string mo = r.mom;
        if ((starts(mo, "+") || starts(mo, "-")) && i == 0) mo += " MoM";
        parts.push_back(label + " " + show(r.value, r.unit) + " (" + mo + ")");
    }
    std::string line = parts.empty() ? "" : join(parts, ", ") + ".";
    if (rw && rw->has_runway) line += " Runway " + trim(rw->runway) + " months.";
    return strip(line);
}

inline std::pair<std::string, std::string> company_and_founder(const Val& doc, const Val& cfg) {
    const Val st = get(cfg, "startup"), fo = get(cfg, "founder");
    const Val sn = get(st, "name"), dc = get(doc, "company"), fn = get(fo, "name");
    return {truthy(sn) ? pystr(sn) : truthy(dc) ? pystr(dc) : "Our company", truthy(fn) ? pystr(fn) : ""};
}

inline std::vector<std::string> texts(const Val& v) {   // [str(x).strip() for x in v or [] if str(x).strip()]
    std::vector<std::string> o;
    if (!truthy(v)) return o;
    for (const Val& x : items_of(v)) { const std::string s = strip(pystr(x)); if (!s.empty()) o.push_back(s); }
    return o;
}
inline std::string months_text(long n) { return n == 1 ? "month" : std::to_string(n) + " months"; }

inline std::string sorted_months(const Val& by) {
    std::vector<std::string> ks; for (const auto& kv : by.d->items) ks.push_back(kv.first);
    std::sort(ks.begin(), ks.end());
    return join(ks, ", ");
}

inline std::pair<std::string, std::vector<std::string>> investor_update(const Val& doc, const std::string& ym, const Val& cfg) {
    const Val by = months_by_key(doc);
    const Val e = get(by, ym);
    if (!truthy(e)) {
        const std::string have = sorted_months(by);
        throw UpdateError("no " + ym + " in metrics.json (it has " + (have.empty() ? "no months" : have) + "): add it with update.py add --month " + ym);
    }
    const std::vector<Row> rows = metric_rows(doc, ym);
    if (rows.empty()) throw UpdateError(ym + " has no values: add them with update.py add --month " + ym + " --set key=value");
    const Runway rw = runway(doc, ym);
    const auto [company, founder] = company_and_founder(doc, cfg);
    std::vector<std::string> warnings;
    std::vector<std::string> L = {"Subject: " + company + " investor update: " + month_name(ym), "", "Hi everyone,", ""};
    const Val tl = get(e, "tldr");
    L.push_back("**TL;DR:** " + (truthy(tl) ? pystr(tl) : tldr_line(doc, rows, &rw))); L.push_back("");
    L.push_back("**The numbers (" + month_name(ym) + ")**"); L.push_back("");
    for (const Row& r : rows) {
        if (r.last.is_none()) L.push_back("- " + r.label + ": " + show(r.value, r.unit) + ", new this month");
        else if (r.change == "flat") L.push_back("- " + r.label + ": " + show(r.value, r.unit) + ", flat on " + short_month(prev_month(ym)));
        else L.push_back("- " + r.label + ": " + show(r.value, r.unit) + ", " + r.change + " from " + show(r.last, r.unit));
    }
    L.push_back("");
    struct Sec { const char* key; const char* title; bool numbered; const char* warn; };
    static const Sec SECS[] = {{"highlights", "Highlights", false, "No highlights for %s: what went well?"},
                               {"lowlights", "Lowlights", false, "No lowlights for %s: investors trust updates that say what went wrong."},
                               {"asks", "Asks", true, "No asks for %s: investors want to help, give them something specific."}};
    for (const Sec& s : SECS) {
        const std::vector<std::string> items = texts(get(e, s.key));
        L.push_back(std::string("**") + s.title + "**"); L.push_back("");
        if (!items.empty()) {
            for (std::size_t i = 0; i < items.size(); ++i) L.push_back(s.numbered ? std::to_string(i + 1) + ". " + items[i] : "- " + items[i]);
        } else {
            L.push_back("- None this month.");
            std::string w = s.warn; w.replace(w.find("%s"), 2, ym); warnings.push_back(w);
        }
        L.push_back("");
    }
    L.push_back("**Runway**"); L.push_back("");
    if (!rw.has_cash) {
        L.push_back("Cash at month end was not recorded.");
        warnings.push_back("No cash for " + ym + ": add it with update.py add --month " + ym + " --cash N.");
    } else {
        std::string line = std::string("Cash at the end of ") + MONTH_NAMES[parse_month(ym).m - 1] + ": " + money(rw.cash) + ".";
        if (rw.burn.known) line += rw.burn.v > Decimal(0) ? " Net burn this month: " + money(rw.burn.v) + "." : " Cash grew " + money(-rw.burn.v) + " this month.";
        if (rw.has_runway)
            line += " Average net burn over the last " + months_text(rw.months_used) + ": " + money(rnd(rw.avg_burn, 0)) + " a month, so the cash lasts about " +
                    trim(rw.runway) + " months, to around " + short_month(rw.until) + ".";
        else if (rw.has_avg) line += " No net burn on average over the last " + months_text(rw.months_used) + ": the cash is not running down.";
        else line += " Burn needs last month's cash too, so there is no runway figure yet.";
        L.push_back(line);
    }
    L.push_back("");
    for (const std::string& s : {std::string("Thank you for the help,"), founder.empty() ? std::string("{your_name}") : founder, "Founder, " + company, std::string()}) L.push_back(s);
    if (founder.empty()) warnings.push_back("No founder name in seed/config.json: the sign-off says {your_name}.");
    return {join(L, "\n"), warnings};
}

inline std::pair<std::string, std::vector<std::string>> prospect_update(const Val& doc, const std::string& ym, const Val& cfg) {
    const Val e = get(months_by_key(doc), ym);
    if (!truthy(e)) throw UpdateError("no " + ym + " in metrics.json: add it with update.py add --month " + ym);
    const std::vector<Row> rows = metric_rows(doc, ym);
    if (rows.empty()) throw UpdateError(ym + " has no values: add them with update.py add --month " + ym + " --set key=value");
    const auto [company, founder] = company_and_founder(doc, cfg);
    const Val st = get(cfg, "startup");
    std::vector<std::string> warnings;
    std::vector<std::string> L = {"Subject: " + company + ": " + month_name(ym) + " update", "", "Hi {first_name},", "",
                                  "You asked me to keep you posted, so here is where " + company + " is.", "",
                                  "**" + month_name(ym) + ":** " + tldr_line(doc, rows, nullptr), ""};
    std::vector<std::string> tops = texts(get(e, "highlights"));
    if (tops.size() > 2) tops.resize(2);
    if (!tops.empty()) { for (const std::string& x : tops) L.push_back("- " + x); L.push_back(""); }
    else warnings.push_back("No highlights for " + ym + ": the prospect update has only the numbers.");
    if (truthy(get(st, "raise"))) {
        const Val stage = get(st, "stage");
        std::vector<std::string> t;
        for (const std::string& x : {short_money(D(get(st, "raise"))), truthy(stage) ? pystr(stage) : std::string("seed")}) if (!x.empty()) t.push_back(x);
        const Val inst = get(st, "instrument");
        L.push_back("We are raising a " + join(t, " ") + (truthy(inst) ? " (" + pystr(inst) + ")" : "") + ". If the timing is better now, would a 20-minute call be useful?");
    } else {
        L.push_back("We are raising our seed round. If the timing is better now, would a 20-minute call be useful?");
        warnings.push_back("No raise in seed/config.json: the update says 'our seed round'.");
    }
    for (const std::string& s : {std::string(), std::string(OPT_OUT), std::string(), founder.empty() ? std::string("{your_name}") : founder, "Founder, " + company, std::string()}) L.push_back(s);
    if (founder.empty()) warnings.push_back("No founder name in seed/config.json: the sign-off says {your_name}.");
    warnings.push_back("Replace {first_name} for each investor before it goes out.");
    return {join(L, "\n"), warnings};
}

// ---------------------------------------------------------------- commands
inline std::string dir_of(const Val& a) { return S(a, "dir"); }
inline std::string metrics_path(const Val& a) { return join_path(dir_of(a), "metrics.json"); }
inline Val load(const Val& a) { return validate(read_json(metrics_path(a), "metrics (run update.py init first)")); }

inline int cmd_init(const Val& a) {
    const std::string dest = metrics_path(a);
    if (exists(dest) && !truthy(get(a, "force"))) {
        eprint("update.py: " + dest + " already exists. Add months with update.py add, or --force to start again from the example.");
        return 1;
    }
    write_json(dest, read_json(abspath(packtools::skill_file("metrics.example.json")), "example metrics"));
    print("Wrote " + dest + " (the Leashly example: replace the metrics and months with yours).");
    print("Next: each month, update.py add --month YYYY-MM --set key=value --cash N, then update.py write.");
    return 0;
}

inline int cmd_add(const Val& a) {
    const std::string path = metrics_path(a);
    Val doc = load(a);
    const std::string month = S(a, "month");
    parse_month(month);
    std::vector<std::string> keys;   // {m["key"]: m}: unique, in order
    for (const Val& m : *get(doc, "metrics").l) {
        const std::string k = pystr(get(m, "key"));
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    }
    Val values = Val::dict();
    for (const Val& item : items_of(get(a, "set"))) {
        const std::string s = item.s;
        const std::size_t eq = s.find('=');
        if (eq == std::string::npos) throw UpdateError("--set wants key=value, like revenue=7875 (got " + str_repr(s) + ")");
        const std::string k = strip(s.substr(0, eq)), v = s.substr(eq + 1);
        bool known = false; for (const Val& m : *get(doc, "metrics").l) if (get(m, "key").t == Val::STR && get(m, "key").s == k) known = true;
        if (!known) throw UpdateError(str_repr(k) + " is not one of your metrics (" + join(keys, ", ") + "). Add it to metrics in " + path + " first.");
        values.d->set(k, num_val(D(Val::str(v), "--set " + k)));
    }
    Val e = get(months_by_key(doc), month);
    if (e.is_none()) {
        e = obj({{"month", sv(month)}, {"values", Val::dict()}});
        if (get(doc, "months").t != Val::LIST) doc.d->set("months", Val::list());
        get(doc, "months").l->push_back(e);
    }
    if (get(e, "values").t != Val::DICT) e.d->set("values", Val::dict());
    for (const auto& [k, v] : values.d->items) get(e, "values").d->set(k, v);
    for (const auto& [k, flag] : std::vector<std::pair<std::string, std::string>>{{"cash", "--cash"}, {"money_in", "--money-in"}, {"net_burn", "--net-burn"}}) {
        const Val v = get(a, k);
        if (!v.is_none()) e.d->set(k, num_val(D(v, flag)));
    }
    for (const auto& [k, arg] : std::vector<std::pair<std::string, std::string>>{{"highlights", "highlight"}, {"lowlights", "lowlight"}, {"asks", "ask"}}) {
        const Val items = get(a, arg);
        if (!truthy(items)) continue;
        Val l = Val::list();
        const Val old = get(e, k);
        if (old.t == Val::LIST) *l.l = *old.l;
        for (const Val& x : *items.l) { const std::string s = strip(x.s); if (!s.empty()) l.l->push_back(sv(s)); }
        e.d->set(k, l);
    }
    if (truthy(get(a, "tldr"))) e.d->set("tldr", sv(strip(S(a, "tldr"))));
    List& ms = *get(doc, "months").l;
    std::stable_sort(ms.begin(), ms.end(), [](const Val& x, const Val& y) { return pystr(get(x, "month")) < pystr(get(y, "month")); });
    validate(doc);
    write_json(path, doc);
    const std::size_t n = get(e, "values").d->items.size();
    print("Saved " + month + " in " + path + ": " + std::to_string(n) + " value" + (n == 1 ? "" : "s") + ", cash " +
          (get(e, "cash").is_none() ? std::string("not set") : money(get(e, "cash"))) + ".");
    std::vector<std::string> missing;
    for (const std::string& k : keys) if (!get(e, "values").d->has(k)) missing.push_back(k);
    if (!missing.empty()) print("Not set yet for " + month + ": " + join(missing, ", ") + ".");
    return 0;
}

inline int cmd_write(const Val& a) {
    const Val doc = load(a);
    std::string ym = S(a, "month");
    if (ym.empty()) {
        if (!truthy(get(doc, "months"))) throw UpdateError("metrics.json has no months yet: update.py add --month YYYY-MM ...");
        const Val by = months_by_key(doc);
        std::vector<std::string> ks; for (const auto& kv : by.d->items) ks.push_back(kv.first);
        ym = *std::max_element(ks.begin(), ks.end());
    }
    parse_month(ym);
    const Val cfg = read_json(join_path(dir_of(a), "config.json"), "config", false);
    const bool prospects = S(a, "to") == "prospects";
    const auto [text, warnings] = prospects ? prospect_update(doc, ym, cfg) : investor_update(doc, ym, cfg);
    const std::string out = join_path(join_path(dir_of(a), "updates"), ym + (prospects ? "-prospects.md" : ".md"));
    const bool to_stdout = truthy(get(a, "stdout"));
    if (to_stdout) {
        flush(); std::fwrite(text.data(), 1, text.size(), stdout);
    } else {
        makedirs(dirname(out));
        write_text(out, text);
        print("Wrote " + out + " (" + (prospects ? "prospect" : "investor") + " update for " + month_name(ym) + ").");
    }
    for (const std::string& w : warnings) { if (to_stdout) eprint("Note: " + w); else print("Note: " + w); }
    if (!to_stdout) print("Nothing was sent. Read it with the founder, then send it from their own Gmail after a yes.");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("update.py", "Investor Updates (seed-updates): the monthly investor update.");
    p.opt({"--dir"}).def("seed").help("the seed/ folder (default ./seed)");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("init"); s.help_line = "copy metrics.example.json into seed/metrics.json"; s.opt({"--force"}).store_true(); s.opt({"--dir"}).no_default(); }
    {
        Parser& s = p.sub("add"); s.help_line = "record a month's numbers and notes in seed/metrics.json";
        s.opt({"--month"}).required().help("YYYY-MM");
        s.opt({"--set"}).append().metavar("KEY=VALUE").help("a metric's value (repeat it)");
        s.opt({"--cash"}).help("cash in the bank at month end");
        s.opt({"--money-in"}).help("new funding that arrived this month (not burn)");
        s.opt({"--net-burn"}).help("override the computed net burn for this month");
        s.opt({"--highlight"}).append().help("repeat it");
        s.opt({"--lowlight"}).append().help("repeat it");
        s.opt({"--ask"}).append().help("repeat it");
        s.opt({"--tldr"}).help("your own TL;DR line");
        s.opt({"--dir"}).no_default();
    }
    {
        Parser& s = p.sub("write"); s.help_line = "write the month's update";
        s.opt({"--month"}).help("YYYY-MM (default: the latest month in metrics.json)");
        s.opt({"--to"}).choices({"investors", "prospects"}).def("investors");
        s.opt({"--stdout"}).store_true().help("print it, write nothing");
        s.opt({"--dir"}).no_default();
    }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "add") return cmd_add(a);
        return cmd_write(a);
    } catch (const UpdateError& e) { eprint(std::string("update.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-updates/update", main);
}  // namespace packtools::seed_updates
