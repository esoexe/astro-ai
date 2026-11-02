// closer_common.inc - the Closer pack's sales board (closer/pipeline.json), shared by every closer tool, and the
// pipeline/deals helper. A C++ port of closer-skill/pipeline/deals.py (MIT, Jake Schincariol): same schema checks,
// messages, file format (json indent 2, UTF-8 kept) and exit codes.
namespace packtools::closer {
using namespace pyrt;

struct PipelineError : std::runtime_error { using std::runtime_error::runtime_error; };

inline const char* DEFAULT_FILE = "closer/pipeline.json";
inline const std::vector<std::string> STAGES = {"lead", "contacted", "replied", "call-booked", "proposal", "contract-sent", "won", "lost"};
inline const std::vector<std::string> OPEN = {"lead", "contacted", "replied", "call-booked", "proposal", "contract-sent"};
inline const std::vector<std::string> CHANNELS = {"email", "call", "meeting", "linkedin", "sms", "note"};
inline const std::vector<std::string> TYPES = {"opening", "followup", "reply", "call", "meeting", "note", "proposal", "contract", "winback", "stage"};

// ---------------------------------------------------------------- Python pieces
inline Val sv(const std::string& s) { return Val::str(s); }
inline bool is(const Val& v, const std::string& s) { return v.t == Val::STR && v.s == s; }
inline bool in(const Val& v, const std::vector<std::string>& xs) { if (v.t != Val::STR) return false; for (const auto& x : xs) if (x == v.s) return true; return false; }
inline bool in(const std::string& v, const std::vector<std::string>& xs) { for (const auto& x : xs) if (x == v) return true; return false; }
inline Val g(const Val& o, const std::string& k, Val dflt = Val()) {   // o.get(k, dflt)
    if (o.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'get'");
    return o.d->at(k, std::move(dflt));
}
inline Val at(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* p = o.d->get(k); if (!p) throw PyErr("KeyError", str_repr(k)); return *p;
}
inline Val& at_ref(Val& o, const std::string& k) {
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    Val* p = o.d->get(k); if (!p) throw PyErr("KeyError", str_repr(k)); return *p;
}
inline void set(Val& o, const std::string& k, Val v) { if (o.t != Val::DICT) throw PyErr("TypeError", "'" + std::string(type_name(o)) + "' object does not support item assignment"); o.d->set(k, std::move(v)); }
inline const std::string& str_of(const Val& v) {   // a str argument where Python needs one (re.match, slicing a str)
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("expected string or bytes-like object, got '") + type_name(v) + "'");
    return v.s;
}
inline List& lst(const Val& v) { if (v.t != Val::LIST) throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable"); return *v.l; }
inline List& lstk(const Val& o, const char* k) {   // o[k] as a list, without a temporary
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* p = o.d->get(k); if (!p) throw PyErr("KeyError", str_repr(k)); return lst(*p);
}
inline std::string ps(const Val& v) { return pystr(v); }   // "%s" % v
inline std::string pr(const Val& v) { return repr(v); }    // "%r" % v
inline double numv(const Val& v) {   // a value in arithmetic
    if (v.is_real()) return v.num();
    throw PyErr("TypeError", std::string("unsupported operand type(s) for +: 'int' and '") + type_name(v) + "'");
}
inline std::string fmt_pct_d(double x) { return std::to_string(static_cast<long long>(x)); }   // "%d" % x
inline bool match(const char* pat, const std::string& s, int flags = 0) { return static_cast<bool>(re::compile(pat, flags).match(s)); }
inline std::string head(const std::string& s, long n) { return cpslice(s, 0, n); }   // s[:n] in code points
inline std::string head(const Val& v, long n) { return cpslice(str_of(v), 0, n); }

// date.fromisoformat (3.11+): YYYY-MM-DD, YYYYMMDD and the ISO week forms YYYY-Www[-D], YYYYWww[D]; a day number
inline long long date_fromiso(const Val& v) {
    if (v.t != Val::STR) throw PyErr("TypeError", "fromisoformat: argument must be str");
    const std::string& s = v.s;
    const auto bad = [&]() -> long long { throw PyErr("ValueError", "Invalid isoformat string: " + str_repr(s)); };
    const auto dig = [&](std::size_t a, std::size_t n) { for (std::size_t k = a; k < a + n; ++k) if (k >= s.size() || s[k] < '0' || s[k] > '9') return false; return true; };
    const auto num = [&](std::size_t a, std::size_t n) { long long r = 0; for (std::size_t k = a; k < a + n; ++k) r = r * 10 + (s[k] - '0'); return r; };
    const auto leap = [](long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; };
    const auto wd = [](long long days) { return static_cast<int>(((days + 3) % 7 + 7) % 7); };
    if (s.size() < 7 || !dig(0, 4)) return bad();
    const long long y = num(0, 4);
    std::size_t k = 4; const bool dash = s[4] == '-'; if (dash) k = 5;
    if (k < s.size() && s[k] == 'W') {
        ++k; if (!dig(k, 2)) return bad(); const long long w = num(k, 2); k += 2; long long d = 1;
        if (k < s.size()) { if (dash) { if (s[k] != '-') return bad(); ++k; } if (!dig(k, 1) || k + 1 != s.size()) return bad(); d = num(k, 1); }
        if (y < 1) return bad();
        const long long jan4 = civil_days(y, 1, 4) - 719468, wk1 = jan4 - wd(jan4);
        const int jan1 = wd(civil_days(y, 1, 1) - 719468);
        const int weeks = (jan1 == 3 || (jan1 == 2 && leap(y))) ? 53 : 52;
        if (w < 1 || w > weeks || d < 1 || d > 7) return bad();
        return wk1 + (w - 1) * 7 + (d - 1);
    }
    if (dash ? (s.size() != 10 || s[7] != '-' || !dig(5, 2) || !dig(8, 2)) : (s.size() != 8 || !dig(4, 4))) return bad();
    const long long m = dash ? num(5, 2) : num(4, 2), d = dash ? num(8, 2) : num(6, 2);
    if (y < 1) throw PyErr("ValueError", "year 0 is out of range");
    if (m < 1 || m > 12) throw PyErr("ValueError", "month must be in 1..12");
    if (!valid_ymd(y, m, d)) throw PyErr("ValueError", "day is out of range for month");
    return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) - 719468;
}
inline long long date_fromiso(const std::string& s) { return date_fromiso(sv(s)); }
inline int weekday(long long days) { return static_cast<int>(((days + 3) % 7 + 7) % 7); }   // Monday = 0
inline std::string iso(long long days) { return date_iso(days); }
inline long long today_arg(const Val& v) { return truthy(v) ? date_fromiso(v) : date_fromiso(today_iso()); }

// "${:,.0f}".format(n)
inline std::string money(double n) { return "$" + fmt_fc(n, 0); }
inline std::string money(const Val& v) { return money(numv(v)); }

// ---------------------------------------------------------------- the yes and the no
inline const char* NO_PAT = R"(\bnot interested\b|\bno,? thanks?\b|\bno thank you\b|\bunsubscribe\b|\bremove me\b|\btake me off\b|\bstop (emailing|messaging|contacting|reaching out)\b|\bdo ?n[o']?t (contact|email|message|call|reach out)\b|\bplease stop\b|\bleave me alone\b|\bnot a (good )?fit\b|\bwe('re| are) all set\b|\bwent with (someone|another|a different)\b|\bno longer (interested|needed)\b|\bpass on this\b|\bnot for us\b|\bwrong person\b|\bspam\b)";
inline const char* UNSUB_PAT = R"(\bunsubscribe\b|\bremove me\b|\btake me off\b|\bstop (emailing|messaging|contacting)\b|\bdo ?n[o']?t (contact|email|message|call)\b|\bplease stop\b|\bleave me alone\b|\bspam\b)";
inline bool says_no(const std::string& t) { return static_cast<bool>(re::compile(NO_PAT, re::I).search(t)); }
inline bool says_unsub(const std::string& t) { return static_cast<bool>(re::compile(UNSUB_PAT, re::I).search(t)); }
inline bool says_no(const Val& t) { return says_no(str_of(t)); }

// An explicit yes for ONE message: 'yes' or 'yes, send it'. Never 'ok', 'sure' or 'yes to all'. (ok, why)
inline std::pair<bool, std::string> parse_yes(const Val& reply) {
    const std::string text = lower(strip(truthy(reply) ? str_of(reply) : std::string()));
    if (text.empty()) return {false, "no reply"};
    if (ends(text, "?")) return {false, "that is a question, not a yes"};
    const auto words = re::compile("[a-z']+").findall(text);
    if (words.empty() || (words[0] != "yes" && words[0] != "y")) return {false, "only an explicit 'yes' sends a message (got " + repr(reply) + ")"};
    static const std::set<std::string> BATCH = {"all", "every", "everyone", "everybody", "everything", "rest", "remaining", "them", "both", "bulk", "batch", "each", "others", "these", "those"};
    static const std::set<std::string> DOUBT = {"no", "not", "dont", "don't", "wait", "cancel", "stop", "but", "maybe", "later", "hold", "unless", "if", "tomorrow", "after", "before", "change", "edit"};
    for (const auto& w : words) if (BATCH.count(w)) return {false, "one yes per message: say yes to each one on its own"};
    for (std::size_t k = 1; k < words.size(); ++k) if (DOUBT.count(words[k])) return {false, "that yes has a condition in it; ask again"};
    return {true, "yes"};
}

inline std::string slug(const std::string& text) {
    std::string s = re::sub("[^a-z0-9]+", "-", lower(text));
    std::size_t a = 0, b = s.size(); while (a < b && s[a] == '-') ++a; while (b > a && s[b - 1] == '-') --b;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------- the file
inline Val empty(const Val& me = Val()) {
    Val d = Val::dict(); d.d->set("version", Val::integer(1)); d.d->set("me", truthy(me) ? me : Val::dict());
    for (const char* k : {"contacts", "deals", "activities", "suppression"}) d.d->set(k, Val::list());
    return d;
}
inline Val load(const std::string& path) {
    if (!exists(path)) throw PipelineError("No " + path + " yet. Run: deals.py init");
    return loads(read_text(path));
}
inline std::pair<std::vector<std::string>, std::vector<std::string>> validate(const Val& data);
inline void save(const Val& data, const std::string& path) {
    const auto [errors, warnings] = validate(data);
    if (!errors.empty()) throw PipelineError("Not saved, the pipeline would be invalid:\n  " + join(errors, "\n  "));
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path + ".tmp", dumps(data, o) + "\n");
    os_replace(path + ".tmp", path);
}
inline Val* find(const Val& items, const std::string& id) {   // the dict itself (shared), or null
    for (Val& it : lst(items)) if (is(g(it, "id"), id)) return &it;
    return nullptr;
}
inline Val* find(const Val& items, const Val& id) { if (id.t != Val::STR) { for (Val& it : lst(items)) if (g(it, "id").t == id.t && repr(g(it, "id")) == repr(id)) return &it; return nullptr; } return find(items, id.s); }
inline bool suppressed(const Val& data, const Val& email0) {
    const std::string email = lower(strip(truthy(email0) ? str_of(email0) : std::string()));
    if (email.empty()) return false;
    const Val sup = g(data, "suppression", Val::list());
    for (const Val& s : lst(sup)) if (lower(str_of(s)) == email) return true;
    return false;
}

// Returns (errors, warnings). Errors block a save.
inline std::pair<std::vector<std::string>, std::vector<std::string>> validate(const Val& data) {
    std::vector<std::string> errors, warnings;
    for (const char* key : {"me", "contacts", "deals", "activities", "suppression"}) if (!has(data, key)) errors.push_back(std::string("missing top-level key: ") + key);
    if (!errors.empty()) return {errors, warnings};
    static const char* ID_RE = "^[a-z0-9]+(-[a-z0-9]+)*$";
    static const char* DATE_RE = R"(^\d{4}-\d{2}-\d{2}$)";
    static const char* STAMP_RE = R"(^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}(:\d{2})?)?$)";
    static const char* EMAIL_RE = R"(^[^@\s]+@[^@\s]+\.[a-z]{2,}$)";
    std::set<std::string> seen;
    for (const Val& c : lstk(data, "contacts")) {
        const Val cidv = g(c, "id", sv("")); const std::string& cid = str_of(cidv);
        if (!match(ID_RE, cid)) errors.push_back("contact id " + str_repr(cid) + " must be lowercase-with-hyphens");
        if (seen.count(cid)) errors.push_back("duplicate id " + cid);
        seen.insert(cid);
        if (!truthy(g(c, "name"))) errors.push_back("contact " + cid + " has no name");
        if (truthy(g(c, "email")) && !match(EMAIL_RE, str_of(at(c, "email")), re::I)) errors.push_back("contact " + cid + " email " + pr(at(c, "email")) + " does not look like an email");
        if (truthy(g(c, "email")) && suppressed(data, at(c, "email")) && !truthy(g(c, "do_not_contact"))) warnings.push_back("contact " + cid + " is on the suppression list; set do_not_contact");
    }
    std::set<std::string> contact_ids; for (const Val& c : lstk(data, "contacts")) contact_ids.insert(str_of(at(c, "id")));
    const auto in_contacts = [&](const Val& v) { return v.t == Val::STR && contact_ids.count(v.s); };
    std::set<std::string> deal_ids;
    for (const Val& d : lstk(data, "deals")) {
        const Val didv = g(d, "id", sv("")); const std::string& did = str_of(didv);
        if (!match(ID_RE, did)) errors.push_back("deal id " + str_repr(did) + " must be lowercase-with-hyphens");
        if (seen.count(did) || deal_ids.count(did)) errors.push_back("duplicate id " + did);
        deal_ids.insert(did);
        if (!in_contacts(g(d, "contact_id"))) errors.push_back("deal " + did + " points at unknown contact " + pr(g(d, "contact_id")));
        if (!in(g(d, "stage"), STAGES)) errors.push_back("deal " + did + " stage " + pr(g(d, "stage")) + " is not one of " + join(STAGES, ", "));
        const Val v = g(d, "value", Val::integer(0));
        if (!v.is_real() || v.num() < 0) errors.push_back("deal " + did + " value must be a number >= 0");
        for (const char* k : {"close_date", "created", "revisit"})
            if (truthy(g(d, k)) && !match(DATE_RE, str_of(at(d, k)))) errors.push_back("deal " + did + " " + k + " " + pr(at(d, k)) + " must be YYYY-MM-DD");
        if (is(g(d, "stage"), "lost") && !truthy(g(d, "lost_reason"))) errors.push_back("deal " + did + " is lost with no lost_reason");
        if (in(g(d, "stage"), OPEN) && !truthy(g(d, "next_step"))) warnings.push_back("deal " + did + " has no next step");
    }
    std::set<std::string> act_ids;
    for (const Val& a : lstk(data, "activities")) {
        const Val aid = g(a, "id", sv("")); const std::string key = std::string(type_name(aid)) + ":" + repr(aid);
        if (!truthy(aid) || act_ids.count(key)) errors.push_back("activity id " + pr(aid) + " is missing or duplicated");
        act_ids.insert(key);
        if (!match(STAMP_RE, str_of(g(a, "date", sv(""))))) errors.push_back("activity " + ps(aid) + " date " + pr(g(a, "date")) + " must be YYYY-MM-DD or YYYY-MM-DDTHH:MM");
        if (!in_contacts(g(a, "contact_id"))) errors.push_back("activity " + ps(aid) + " points at unknown contact " + pr(g(a, "contact_id")));
        if (truthy(g(a, "deal_id")) && !(at(a, "deal_id").t == Val::STR && deal_ids.count(at(a, "deal_id").s))) errors.push_back("activity " + ps(aid) + " points at unknown deal " + pr(at(a, "deal_id")));
        if (!in(g(a, "channel"), CHANNELS)) errors.push_back("activity " + ps(aid) + " channel " + pr(g(a, "channel")) + " is not one of " + join(CHANNELS, ", "));
        if (!in(g(a, "type"), TYPES)) errors.push_back("activity " + ps(aid) + " type " + pr(g(a, "type")) + " is not one of " + join(TYPES, ", "));
        const Val dir = g(a, "direction");
        if (!(dir.is_none() || is(dir, "out") || is(dir, "in"))) errors.push_back("activity " + ps(aid) + " direction must be out, in or null");
        if (is(dir, "out") && in(g(a, "channel"), {"email", "linkedin", "sms"}) && !truthy(g(a, "approval"))) errors.push_back("activity " + ps(aid) + " went out with no recorded approval");
    }
    return {errors, warnings};
}

inline std::string next_id(const Val& data, const std::string& prefix = "a") {
    long long n = 0;
    const std::string pat = "^" + prefix + R"((\d+)$)"; const re::Regex* rx = &re::compile(pat);
    for (const Val& a : lstk(data, "activities")) {
        const Val id = g(a, "id", sv(""));
        if (const auto m = rx->match(str_of(id))) n = std::max(n, std::stoll(m.group(1)));
    }
    return prefix + std::to_string(n + 1);
}
// add_activity(data, **fields): the default keys first, then the fields in the order given
inline Val add_activity(Val& data, const std::vector<std::pair<std::string, Val>>& fields) {
    Val act = Val::dict();
    act.d->set("id", sv(next_id(data)));
    for (const char* k : {"date", "contact_id", "deal_id", "channel", "type", "direction", "subject"}) act.d->set(k, Val());
    act.d->set("text", sv(""));
    for (const auto& [k, v] : fields) act.d->set(k, v);
    lstk(data, "activities").push_back(act);
    return act;
}
// activities of a contact and/or deal, oldest first (sorted by date, stable)
inline List history(const Val& data, const Val* contact_id, const Val* deal_id) {
    List out;
    for (const Val& a : lstk(data, "activities")) {
        if (contact_id && !(repr(at(a, "contact_id")) == repr(*contact_id))) continue;
        if (deal_id && !(repr(g(a, "deal_id")) == repr(*deal_id))) continue;
        out.push_back(a);
    }
    std::stable_sort(out.begin(), out.end(), [](const Val& x, const Val& y) { return str_of(at(x, "date")) < str_of(at(y, "date")); });
    return out;
}
inline List history_contact(const Val& data, const Val& cid) { return history(data, &cid, nullptr); }
inline List history_deal(const Val& data, const Val& did) { return history(data, nullptr, &did); }
inline Val last_touch(const Val& data, const Val& deal) {
    List acts = history_deal(data, at(deal, "id"));
    if (acts.empty()) acts = history_contact(data, at(deal, "contact_id"));
    return acts.empty() ? g(deal, "created") : sv(head(at(acts.back(), "date"), 10));
}

inline Val upsert_contact(Val& data, const std::vector<std::pair<std::string, Val>>& f) {
    const Val id = f[0].second;
    Val* c = find(at(data, "contacts"), id);
    if (!c) {
        Val n = Val::dict(); n.d->set("id", id);
        for (const char* k : {"name", "title", "company", "email", "phone", "website", "source"}) n.d->set(k, Val());
        n.d->set("notes", sv("")); n.d->set("do_not_contact", Val::boolean(false));
        lstk(data, "contacts").push_back(n); c = &lstk(data, "contacts").back();
    }
    for (const auto& [k, v] : f) if (!v.is_none()) c->d->set(k, v);
    return *c;
}
inline Val upsert_deal(Val& data, long long today, const std::vector<std::pair<std::string, Val>>& f) {
    const Val id = f[0].second;
    Val* d = find(at(data, "deals"), id);
    if (!d) {
        Val n = Val::dict(); n.d->set("id", id); n.d->set("contact_id", Val()); n.d->set("title", Val()); n.d->set("stage", sv("lead"));
        n.d->set("value", Val::integer(0)); n.d->set("currency", sv("USD")); n.d->set("close_date", Val()); n.d->set("created", sv(iso(today)));
        n.d->set("next_step", Val()); n.d->set("lost_reason", Val());
        lstk(data, "deals").push_back(n); d = &lstk(data, "deals").back();
    }
    for (const auto& [k, v] : f) if (!v.is_none()) d->d->set(k, v);
    return *d;
}

inline Val move(Val& data, const std::string& deal_id, const std::string& stage, long long today, const Val& reason = Val(), const Val& next_step = Val()) {
    Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) throw PipelineError("No deal " + deal_id);
    Val d = *dp;   // shares the dict
    if (!in(stage, STAGES)) throw PipelineError("Stage must be one of: " + join(STAGES, ", "));
    if (stage == "lost" && !truthy(reason)) throw PipelineError("Say why it was lost (--reason): it is what /win-back reads later.");
    const Val old = at(d, "stage");
    d.d->set("stage", sv(stage));
    if (stage == "lost") d.d->set("lost_reason", reason);
    if (stage == "won") d.d->set("close_date", sv(iso(today)));
    if (!next_step.is_none()) d.d->set("next_step", next_step);
    if (stage == "won" || stage == "lost") d.d->set("next_step", stage == "won" ? g(d, "next_step") : Val());
    add_activity(data, {{"date", sv(iso(today))}, {"contact_id", at(d, "contact_id")}, {"deal_id", at(d, "id")}, {"channel", sv("note")},
                        {"type", sv("stage")}, {"text", sv(ps(old) + " -> " + stage + (truthy(reason) ? ": " + ps(reason) : ""))}});
    return d;
}

inline double value_of(const Val& d) { return numv(g(d, "value", Val::integer(0))); }
inline std::string board(const Val& data, long long today) {
    std::vector<std::string> lines; double total = 0;
    const List& deals = lstk(data, "deals");
    for (const auto& stage : OPEN) {
        List ds; for (const Val& d : deals) if (is(at(d, "stage"), stage)) ds.push_back(d);
        if (ds.empty()) continue;
        double sub = 0; for (const Val& d : ds) sub += value_of(d);
        total += sub;
        lines.push_back(upper(stage) + " (" + std::to_string(ds.size()) + ", " + money(sub) + ")");
        std::stable_sort(ds.begin(), ds.end(), [](const Val& a, const Val& b) { return -value_of(a) < -value_of(b); });
        for (const Val& d : ds) {
            const Val* cp = find(at(data, "contacts"), at(d, "contact_id")); const Val c = cp ? *cp : Val::dict();
            const long long idle = today - date_fromiso(last_touch(data, d));
            const std::string flag = idle >= 14 ? "  [quiet " + std::to_string(idle) + " days]" : "";
            const Val who = orv(orv(g(c, "company"), g(c, "name")), sv(""));
            lines.push_back("  " + ljust(head(at(d, "title"), 28), 28) + " " + ljust(head(who, 22), 22) + " " + rjust(money(value_of(d)), 9) + "  next: " + ps(orv(g(d, "next_step"), sv("(none set)"))) + flag);
        }
    }
    double won = 0; std::size_t open_n = 0;
    for (const Val& d : deals) { if (is(at(d, "stage"), "won")) won += value_of(d); if (in(at(d, "stage"), OPEN)) open_n++; }
    lines.push_back("");
    lines.push_back("Open: " + money(total) + " across " + std::to_string(open_n) + " deals. Won to date: " + money(won) + ".");
    return join(lines, "\n");
}

inline std::string show(const Val& data, const std::string& deal_id) {
    const Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) throw PipelineError("No deal " + deal_id);
    const Val d = *dp;
    const Val* cp = find(at(data, "contacts"), at(d, "contact_id")); const Val c = cp ? *cp : Val::dict();
    std::vector<std::string> out = {ps(at(d, "title")) + "  [" + ps(at(d, "stage")) + "]  " + money(value_of(d)),
        ps(g(c, "name")) + ", " + ps(g(c, "company")) + "  " + ps(orv(g(c, "email"), sv(""))),
        "next: " + ps(orv(g(d, "next_step"), sv("-"))) + "   close: " + ps(orv(g(d, "close_date"), sv("-"))), ""};
    for (const Val& a : history_deal(data, at(d, "id"))) {
        const Val dir = g(a, "direction"); const std::string arrow = is(dir, "out") ? "->" : is(dir, "in") ? "<-" : "  ";
        const Val subj = g(a, "subject");
        out.push_back("  " + ps(at(a, "date")) + " " + arrow + " " + ljust(ps(at(a, "channel")), 8) + " " + (truthy(subj) ? ps(subj) : head(g(a, "text", sv("")), 70)));
    }
    return join(out, "\n");
}

// ---------------------------------------------------------------- deals CLI
inline int deals_main(const std::vector<std::string>& argv) {
    Parser p("deals.py", "The Closer pipeline.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    static const std::vector<std::string> ME = {"name", "company", "email", "address", "timezone", "phone", "offer", "price", "plan_b", "value_line", "proof"};
    { Parser& s = p.sub("init"); for (const auto& k : ME) s.opt({"--" + k}); }
    { Parser& s = p.sub("contact"); for (const char* k : {"id", "name", "title", "company", "email", "phone", "website", "source", "notes"}) s.opt({std::string("--") + k}); }
    { Parser& s = p.sub("deal"); for (const char* k : {"id", "contact", "title", "stage", "close", "next", "currency", "revisit"}) s.opt({std::string("--") + k});
      s.opt({"--value"}).number(); s.opt({"--today"}); }
    { Parser& s = p.sub("move"); s.opt({"--deal"}).required(); s.opt({"--stage"}).required(); s.opt({"--reason"}); s.opt({"--next"}); s.opt({"--today"}); }
    { Parser& s = p.sub("board"); s.opt({"--today"}); }
    { Parser& s = p.sub("show"); s.opt({"--deal"}).required(); }
    p.sub("validate");
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        if (cmd == "init") {
            if (exists(file)) throw PipelineError(file + " already exists; nothing changed.");
            Val me = Val::dict(); for (const auto& k : ME) me.d->set(k, get(a, k));
            save(empty(me), file);
            print("Created " + file);
            if (!truthy(get(me, "address"))) print("Add --address before any cold email: the law needs a postal address in it.");
            return 0;
        }
        Val data = load(file);
        if (cmd == "contact") {
            Val id = get(a, "id");
            if (!truthy(id)) id = sv(slug(ps(orv(get(a, "name"), sv(""))) + " " + ps(orv(get(a, "company"), sv("")))));
            std::vector<std::pair<std::string, Val>> f = {{"id", id}};
            for (const char* k : {"name", "title", "company", "email", "phone", "website", "source", "notes"}) f.emplace_back(k, get(a, k));
            upsert_contact(data, f);
            save(data, file);
            print("Saved contact " + ps(id));
        } else if (cmd == "deal") {
            Val value = get(a, "value");
            if (!value.is_none()) {
                const double x = value.num();
                if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
                if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
                if (x == std::trunc(x) && std::fabs(x) < 9.2e18) value = Val::integer(static_cast<long long>(x));
            }
            const long long today = today_arg(get(a, "today"));
            upsert_deal(data, today, {{"id", get(a, "id")}, {"contact_id", get(a, "contact")}, {"title", get(a, "title")}, {"stage", get(a, "stage")},
                                      {"value", value}, {"close_date", get(a, "close")}, {"next_step", get(a, "next")}, {"currency", get(a, "currency")},
                                      {"revisit", get(a, "revisit")}});
            save(data, file);
            print("Saved deal " + ps(get(a, "id")));
        } else if (cmd == "move") {
            const long long today = today_arg(get(a, "today"));
            const Val d = move(data, S(a, "deal"), S(a, "stage"), today, get(a, "reason"), get(a, "next"));
            save(data, file);
            print(ps(at(d, "title")) + " is now " + ps(at(d, "stage")));
        } else if (cmd == "board") {
            print(board(data, today_arg(get(a, "today"))));
        } else if (cmd == "show") {
            print(show(data, S(a, "deal")));
        } else if (cmd == "validate") {
            const auto [errors, warnings] = validate(data);
            for (const auto& e : errors) print("ERROR " + e);
            for (const auto& w : warnings) print("warn  " + w);
            print(errors.empty() ? "OK" : std::to_string(errors.size()) + " errors");
            return errors.empty() ? 0 : 1;
        }
    } catch (const PipelineError& e) {
        eprint(e.what()); return 1;
    }
    return 0;
}
PACKTOOL("pipeline/deals", deals_main);
}  // namespace packtools::closer
