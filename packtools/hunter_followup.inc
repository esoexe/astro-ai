// hunter-followup/followup: polite follow-ups with the connector's stop rules, from hunter/pipeline.json.
// It drafts; it never sends. A C++ port of skills/hunter-followup/followup.py (MIT, Jake Schincariol).
#pragma once
#include <array>
#include <regex>

namespace packtools::hunter_followup {
using namespace pyrt;

struct FollowupError : std::runtime_error { using std::runtime_error::runtime_error; };

constexpr int MAX_FOLLOWUPS = 2, SILENCE_DAYS = 21, GAP_FIRST = 3, GAP_NEXT = 7, APP_CHECKIN_DAYS = 10;
inline const std::vector<std::string> ACTIVE = {"saved", "applied", "screen", "interview", "onsite", "offer"};
inline const std::vector<std::string> YES_WORDS = {"yes", "y"};
inline const std::set<std::string> BATCH_WORDS = {"all", "every", "everyone", "everybody", "everything", "rest", "remaining", "them", "both",
    "bulk", "batch", "each", "others"};
inline const std::set<std::string> DOUBT_WORDS = {"no", "not", "dont", "don't", "wait", "cancel", "stop", "but", "maybe", "later", "hold",
    "unless", "if", "tomorrow"};

// ---------------------------------------------------------------- Python's == and < on JSON values
inline bool num(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; }
inline double numv(const Val& v) { return v.t == Val::FLOAT ? v.f : v.t == Val::INT ? static_cast<double>(v.i) : (v.b ? 1.0 : 0.0); }
inline bool peq(const Val& a, const Val& b) {
    if (num(a) && num(b)) {
        if (a.t != Val::FLOAT && b.t != Val::FLOAT) return (a.t == Val::INT ? a.i : a.b) == (b.t == Val::INT ? b.i : b.b);
        return numv(a) == numv(b);
    }
    if (a.t != b.t) return false;
    switch (a.t) {
        case Val::NONE: return true;
        case Val::STR: return a.s == b.s;
        case Val::LIST: { if (a.l->size() != b.l->size()) return false; for (std::size_t i = 0; i < a.l->size(); ++i) if (!peq((*a.l)[i], (*b.l)[i])) return false; return true; }
        case Val::DICT: {
            if (a.d->items.size() != b.d->items.size()) return false;
            for (const auto& [k, v] : a.d->items) { const Val* o = b.d->get(k); if (!o || !peq(v, *o)) return false; }
            return true;
        }
        default: return false;
    }
}
inline bool peq(const Val& a, const char* s) { return a.t == Val::STR && a.s == s; }
inline bool plt(const Val& a, const Val& b) {   // a < b, raising TypeError for mixed types like Python
    if (a.t == Val::STR && b.t == Val::STR) return a.s < b.s;
    if (num(a) && num(b)) return numv(a) < numv(b);
    throw PyErr("TypeError", std::string("'<' not supported between instances of '") + type_name(a) + "' and '" + type_name(b) + "'");
}
inline bool in_list(const Val& v, const std::vector<std::string>& l) { return v.t == Val::STR && std::find(l.begin(), l.end(), v.s) != l.end(); }
inline Val str_v(const std::string& s) { return Val::str(s); }
inline Val& L(const Val& data, const char* k) { return *const_cast<Val*>(data.d->get(k)); }
inline void append(Val& lst, const Val& item) {   // lst.append(item)
    if (lst.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(lst) + "' object has no attribute 'append'");
    lst.l->push_back(item);
}
inline void pop_key(Val& d, const std::string& k) {   // d.pop(k, None)
    auto& it = d.d->items;
    it.erase(std::remove_if(it.begin(), it.end(), [&](const auto& kv) { return kv.first == k; }), it.end());
}
inline List or_iter(const Val& v) { return truthy(v) ? iter(v) : List{}; }   // for m in (v or [])
inline std::string need_str(const Val& v, const char* method) {   // v.method() on a value that must be a str
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + method + "'");
    return v.s;
}
inline std::string re_str(const Val& v) {   // re.search(pattern, v) needs a str
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("expected string or bytes-like object, got '") + type_name(v) + "'");
    return v.s;
}

// ---------------------------------------------------------------- the regexes, on a one-byte-per-code-point shadow
// ASCII stays; under re.I a code point whose case key is an ASCII letter becomes that letter (K, ı, İ, ſ);
// other letters and numbers become '_' (word), other whitespace '\f' (only U+0020 matches ' '), anything else '\x01'.
inline bool word_cp(uint32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    const char* cat = uni::category(c);
    return cat[0] == 'L' || cat[0] == 'N';
}
inline std::string shadow(const std::vector<std::string>& cs) {
    std::string o;
    for (const auto& ch : cs) {
        const uint32_t c = cp_of(ch);
        if (c < 0x80) { o += static_cast<char>(c); continue; }
        const char32_t k = hunter_resume::ckey(static_cast<char32_t>(c));
        if (k >= U'a' && k <= U'z') o += static_cast<char>(k);
        else if (py_space(c)) o += '\f';
        else if (word_cp(c)) o += '_';
        else o += '\x01';
    }
    return o;
}
// re.search(pattern, text, re.I): the matched text (group 0), or nullopt
inline std::optional<std::string> search_i(const std::regex& re, const std::string& text) {
    const auto cs = cps(text);
    const std::string sh = shadow(cs);
    std::smatch m;
    if (!std::regex_search(sh, m, re)) return std::nullopt;
    std::string o;
    for (long i = m.position(0); i < m.position(0) + m.length(0); ++i) o += cs[static_cast<std::size_t>(i)];
    return o;
}
inline const std::regex& NO_RE() {   // a "no" or any sign of disinterest; one match closes every thread with that person
    static const std::regex r(
        R"(\bnot interested\b|\bno,? thanks?\b|\bno thank you\b|\bunsubscribe\b|)"
        R"(\bremove me\b|\b(please )?stop (emailing|messaging|contacting|reaching out)\b|)"
        R"(\bdo ?n[o']?t (contact|email|message|reach out)\b|\bplease stop\b|)"
        R"(\bnot (a good )?fit\b|\bnot hiring\b|\b(role|position) (has been|is) (filled|closed)\b|)"
        R"(\bnot (moving|move) forward\b|\bdecided to (go|move) (with|forward with) (another|other)\b|)"
        R"(\bnot at this time\b|\bleave me alone\b|\bno longer (interested|hiring)\b|)"
        R"(\bpass on this\b|\bnot open to\b|\bplease don'?t\b|\btake me off\b|)"
        R"(\bnot the right person\b|\bnot looking\b|\bnot (right )?now\b|\btoo busy\b|)"
        R"(\bno bandwidth\b|\bcan'?t help\b|\bno (open )?(roles|openings|headcount)\b)", std::regex::icase);
    return r;
}
inline const std::regex& DNC_RE() { static const std::regex r("do not contact", std::regex::icase); return r; }
inline const std::regex& INTERVIEW_RE() { static const std::regex r(R"(\b(interview|onsite|on-site|screen|chat|call|coffee|meeting|loop)\b)", std::regex::icase); return r; }

// ---------------------------------------------------------------- dates (datetime.date as days since 1970-01-01)
inline bool nd(uint32_t c) { return c < 0x80 ? (c >= '0' && c <= '9') : std::string(uni::category(c)) == "Nd"; }
inline int nd_value(uint32_t c) {   // Nd code points come in runs of ten, 0 to 9
    if (c < 0x80) return static_cast<int>(c - '0');
    uint32_t k = 0;
    while (c - k - 1 > 0 && nd(c - k - 1)) ++k;
    return static_cast<int>(k % 10);
}
// STAMP_RE: ^(\d{4}-\d{2}-\d{2})(?:[T ]\d{2}:\d{2}(?::\d{2})?(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?)?$  ->  {y, m, d}
// time=false: DATE_RE, ^\d{4}-\d{2}-\d{2}$ (the date alone)
inline std::optional<std::array<long long, 3>> stamp(const std::string& s, bool time = true) {
    std::vector<uint32_t> c; for (const auto& ch : cps(s)) c.push_back(cp_of(ch));
    std::size_t i = 0;
    const auto digits = [&](std::size_t n, long long& v) { v = 0; for (std::size_t k = 0; k < n; ++k) { if (i >= c.size() || !nd(c[i])) return false; v = v * 10 + nd_value(c[i++]); } return true; };
    const auto lit = [&](uint32_t x) { if (i < c.size() && c[i] == x) { ++i; return true; } return false; };
    long long y, m, d, t;
    if (!digits(4, y) || !lit('-') || !digits(2, m) || !lit('-') || !digits(2, d)) return std::nullopt;
    if (i < c.size()) {
        if (!time) return std::nullopt;
        if (!(lit('T') || lit(' ')) || !digits(2, t) || !lit(':') || !digits(2, t)) return std::nullopt;
        if (i < c.size() && c[i] == ':') { ++i; if (!digits(2, t)) return std::nullopt; }
        if (i < c.size() && c[i] == '.') { ++i; if (!digits(1, t)) return std::nullopt; while (i < c.size() && nd(c[i])) ++i; }
        if (lit('Z')) {}
        else if (i < c.size() && (c[i] == '+' || c[i] == '-')) { ++i; if (!digits(2, t)) return std::nullopt; lit(':'); if (!digits(2, t)) return std::nullopt; }
        if (i != c.size()) return std::nullopt;
    }
    return std::array<long long, 3>{y, m, d};
}
inline long long parse_day(const Val& s, const std::string& what = "date") {
    const auto ymd = stamp(strip(truthy(s) ? pystr(s) : ""));
    if (!ymd) throw FollowupError(what + " must be YYYY-MM-DD, got " + repr(s));
    try { return py_date((*ymd)[0], (*ymd)[1], (*ymd)[2]); }
    catch (const PyErr& e) { if (e.type != "ValueError") throw; throw FollowupError(what + " is not a real date: " + repr(s)); }
}
inline std::optional<long long> day_or_none(const Val& s) {
    if (s.is_none() || peq(s, "")) return std::nullopt;
    try { return parse_day(s); } catch (const FollowupError&) { return std::nullopt; }
}
inline std::string iso(long long d) { return date_iso(d); }

// ---------------------------------------------------------------- basics
inline Val load(const std::string& path) {
    if (!exists(path)) throw FollowupError("no pipeline at " + path + " (hunter-tracker: pipeline.py init)");
    const std::string text = read_text(path);
    Val data;
    try { data = loads(text); }
    catch (const PyErr& e) { if (e.type != "json.decoder.JSONDecodeError") throw; throw FollowupError(path + " is not valid JSON: " + e.what()); }
    for (const char* k : {"roles", "contacts", "threads"}) if (dget(data, k).is_none()) data.d->set(k, Val::list());
    return data;
}
inline void save(const std::string& path, const Val& data) {
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path + ".tmp", dumps(data, o) + "\n");
    os_replace(path + ".tmp", path);
}
inline Val load_templates() {
    const Val t = loads(read_text(skill_file("templates.json")));
    if (t.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(t) + "' object has no attribute 'items'");
    Val o = Val::dict();
    for (const auto& [k, v] : t.d->items) if (!starts(k, "_")) o.d->set(k, v);
    return o;
}
inline Val contact_by_id(const Val& data, const Val& cid) {
    for (const Val& c : iter(L(data, "contacts"))) if (peq(dget(c, "id"), cid)) return c;
    throw FollowupError("no contact with id " + repr(cid) + " in the pipeline");
}
inline Val role_by_id(const Val& data, const Val& rid) {
    for (const Val& r : iter(L(data, "roles"))) if (peq(dget(r, "id"), rid)) return r;
    throw FollowupError("no role with id " + repr(rid) + " in the pipeline");
}
inline std::string first_name(const Val& name) {
    const std::string n = strip(truthy(name) ? need_str(name, "strip") : "");
    return n.empty() ? "there" : split_ws(n)[0];
}

// ---------------------------------------------------------------- the rules
inline List dict_msgs(const Val& thread) { List o; for (const Val& m : or_iter(dget(thread, "messages"))) if (m.t == Val::DICT) o.push_back(m); return o; }
// Messages that really went out or came in: not a message flagged "draft": true, nor (when no message carries
// the flag) the last outgoing message of a thread whose status is "drafted".
inline List sent_messages(const Val& thread) {
    List msgs = dict_msgs(thread);
    bool flagged = false; for (const Val& m : msgs) if (m.d->has("draft")) flagged = true;
    if (flagged) { List o; for (const Val& m : msgs) if (!truthy(dget(m, "draft"))) o.push_back(m); return o; }
    if (peq(dget(thread, "status"), "drafted"))
        for (std::size_t i = msgs.size(); i-- > 0;)
            if (peq(dget(msgs[i], "direction"), "out")) { msgs.erase(msgs.begin() + static_cast<long>(i)); return msgs; }
    return msgs;
}
inline bool has_pending_draft(const Val& thread) {
    const Val d = dget(thread, "draft");
    if (d.t == Val::DICT && truthy(dget(d, "text"))) return true;
    for (const Val& m : dict_msgs(thread)) if (truthy(dget(m, "draft"))) return true;
    return peq(dget(thread, "status"), "drafted");
}
// why this person must never get another message, or nullopt
inline std::optional<std::string> said_no(const Val& data, const Val& cid) {
    Val c = Val::dict();
    for (const Val& x : iter(L(data, "contacts"))) if (peq(dget(x, "id"), cid)) { c = x; break; }
    if (truthy(dget(c, "do_not_contact")) || search_i(DNC_RE(), re_str(orv(dget(c, "notes"), str_v(""))))) return std::string("marked do not contact");
    for (const Val& t : iter(L(data, "threads"))) {
        if (!peq(dget(t, "contact_id"), cid)) continue;
        if (peq(dget(t, "status"), "declined")) return "said no (" + pystr(dget(t, "channel")) + " thread declined)";
        for (const Val& m : or_iter(dget(t, "messages"))) {
            if (peq(dget(m, "direction"), "in")) {
                if (const auto hit = search_i(NO_RE(), re_str(orv(dget(m, "text"), str_v("")))))
                    return "said \"" + *hit + "\" on " + cpslice(pystr(dget(m, "date")), 0, 10);
            }
        }
    }
    return std::nullopt;
}
inline List threads_of(const Val& data, const Val& cid) { List o; for (const Val& t : iter(L(data, "threads"))) if (peq(dget(t, "contact_id"), cid)) o.push_back(t); return o; }
inline void upd(Val& st, std::initializer_list<std::pair<const char*, Val>> kv) { for (const auto& [k, v] : kv) st.d->set(k, v); }

// the follow-up state for one person across all their threads: due | waiting | draft | your-turn | stopped | done | none
inline Val contact_state(const Val& data, const Val& cid, long long today) {
    const List threads = threads_of(data, cid);
    Val st = Val::dict();
    upd(st, {{"contact_id", cid}, {"state", str_v("none")}, {"reason", str_v("")}, {"due", Val()}, {"followup", Val()},
             {"channel", Val()}, {"role_id", Val()}, {"opening", Val()}, {"followups_sent", Val::integer(0)}});
    if (threads.empty()) { upd(st, {{"reason", str_v("no conversation yet")}}); return st; }
    const auto no = said_no(data, cid);
    List drafts; for (const Val& t : threads) if (has_pending_draft(t)) drafts.push_back(t);
    if (no) { upd(st, {{"state", str_v("stopped")}, {"reason", str_v(*no + ": never message again" + (drafts.empty() ? "" : "; do NOT send the pending draft"))}}); return st; }
    for (const Val& t : threads) if (peq(dget(t, "status"), "booked")) { upd(st, {{"state", str_v("done")}, {"reason", str_v("chat booked: no follow-ups, prepare for it")}}); return st; }
    List live; for (const Val& t : threads) if (!in_list(dget(t, "status"), {"closed", "declined"})) live.push_back(t);
    if (live.empty()) { upd(st, {{"state", str_v("done")}, {"reason", str_v("every thread is closed")}}); return st; }

    struct M { long long d; std::string ds; Val dir, t; };
    std::vector<M> msgs;
    for (const Val& t : threads)
        for (const Val& m : sent_messages(t))
            if (const auto d = day_or_none(dget(m, "date"))) msgs.push_back({*d, pystr(dget(m, "date")), dget(m, "direction"), t});
    std::stable_sort(msgs.begin(), msgs.end(), [](const M& a, const M& b) { return a.d != b.d ? a.d < b.d : a.ds < b.ds; });
    if (msgs.empty()) {
        if (!drafts.empty()) upd(st, {{"state", str_v("draft")}, {"reason", str_v("first message drafted, waiting for your yes")}, {"channel", dget(drafts[0], "channel")}, {"role_id", dget(drafts[0], "role_id")}});
        else upd(st, {{"reason", str_v("no messages yet")}});
        return st;
    }
    long last_in = -1; for (std::size_t i = 0; i < msgs.size(); ++i) if (peq(msgs[i].dir, "in")) last_in = static_cast<long>(i);
    std::optional<long long> silence_from; if (last_in >= 0) silence_from = msgs[static_cast<std::size_t>(last_in)].d;
    if (peq(msgs.back().dir, "in")) {
        const Val& t = msgs.back().t;
        upd(st, {{"state", str_v("your-turn")}, {"reason", str_v("they replied on " + iso(msgs.back().d) + ": answer them (no follow-up rules apply)")}, {"channel", dget(t, "channel")}, {"role_id", dget(t, "role_id")}});
        return st;
    }
    std::vector<M> stretch; for (std::size_t i = static_cast<std::size_t>(last_in + 1); i < msgs.size(); ++i) if (peq(msgs[i].dir, "out")) stretch.push_back(msgs[i]);
    if (stretch.empty()) throw PyErr("IndexError", "list index out of range");
    const long long opening = stretch[0].d;
    const long long silence = silence_from.value_or(opening);
    long long k = 0; for (const M& m : stretch) if (m.d > opening) ++k;
    const M& last_out = stretch.back();
    std::vector<M> nudgeable; for (const M& m : stretch) if (!truthy(dget(m.t, "invite"))) nudgeable.push_back(m);
    upd(st, {{"opening", str_v(iso(opening))}, {"followups_sent", Val::integer(k)}});
    if (nudgeable.empty()) {
        upd(st, {{"state", str_v("waiting")}, {"channel", dget(last_out.t, "channel")}, {"role_id", dget(last_out.t, "role_id")}, {"reason", str_v("LinkedIn invitation pending: no follow-up until they accept")}});
        return st;
    }
    const M* pick = &nudgeable.back();
    for (std::size_t i = nudgeable.size(); i-- > 0;) if (peq(dget(nudgeable[i].t, "channel"), "email")) { pick = &nudgeable[i]; break; }
    upd(st, {{"channel", dget(pick->t, "channel")}, {"role_id", dget(pick->t, "role_id")}});
    if (k >= MAX_FOLLOWUPS) { upd(st, {{"state", str_v("stopped")}, {"reason", str_v(std::to_string(k) + " follow-ups sent with no reply: stop here")}}); return st; }
    if (today - silence > SILENCE_DAYS) { upd(st, {{"state", str_v("stopped")}, {"reason", str_v(std::to_string(today - silence) + " days of silence since " + iso(silence) + ": let it go")}}); return st; }
    const long long cadence = k == 0 ? opening + GAP_FIRST : last_out.d + GAP_NEXT;
    std::optional<long long> explicit_;
    for (const Val& t : live)
        if (truthy(dget(t, "next_followup")) && !truthy(dget(t, "invite")))
            if (const auto d = day_or_none(dget(t, "next_followup"))) if (!explicit_ || *d < *explicit_) explicit_ = *d;
    const long long due = explicit_.value_or(cadence);
    if (due - silence > SILENCE_DAYS) {
        upd(st, {{"state", str_v("stopped")}, {"reason", str_v("the next follow-up would land " + std::to_string(due - silence) + " days into the silence (limit " + std::to_string(SILENCE_DAYS) + "): no more")}});
        return st;
    }
    upd(st, {{"due", str_v(iso(due))}, {"followup", Val::integer(k + 1)}});
    const std::string n = std::to_string(k + 1) + " of " + std::to_string(MAX_FOLLOWUPS);
    if (!drafts.empty()) upd(st, {{"state", str_v("draft")}, {"reason", str_v("follow-up " + n + " drafted, waiting for your yes" + (due <= today ? std::string() : " (not due until " + iso(due) + ")"))}});
    else if (due <= today) upd(st, {{"state", str_v("due")}, {"reason", str_v("follow-up " + n + ": opened " + iso(opening) + ", no reply")}});
    else upd(st, {{"state", str_v("waiting")}, {"reason", str_v("follow-up " + n + " due " + iso(due))}});
    return st;
}

// applications with no news, and next steps that are due
inline List application_items(const Val& data, long long today) {
    List items;
    for (const Val& c : iter(L(data, "contacts"))) {   // names = {c.get("id"): c ...}: the ids must be hashable
        const Val id = dget(c, "id");
        if (id.t == Val::LIST || id.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(id) + "'");
    }
    for (const Val& r : iter(L(data, "roles"))) {
        if (!in_list(dget(r, "stage"), ACTIVE)) continue;
        const auto nd_ = day_or_none(dget(r, "next_date"));
        if (nd_ && *nd_ <= today) {
            const Val stepv = orv(dget(r, "next_step"), str_v("next step"));
            Val item = Val::dict();
            upd(item, {{"kind", str_v("next-step")}, {"role_id", dget(r, "id")}, {"company", dget(r, "company")}, {"title", dget(r, "title")}, {"date", str_v(iso(*nd_))}});
            const bool talk = search_i(INTERVIEW_RE(), re_str(stepv)).has_value();
            const std::string step = stepv.s;
            const std::string tpl = lower(step).find("coffee") != std::string::npos ? "after-coffee" : "thank-you";
            std::string what;
            if (talk && *nd_ == today) what = "today: " + step + ". Afterwards, a thank-you within 24 hours (template " + tpl + ")";
            else if (talk && today - *nd_ == 1) what = step + " was yesterday: send the thank-you today (template " + tpl + ")";
            else if (*nd_ == today) what = "today: " + step;
            else what = step + " was due " + iso(*nd_) + " (" + std::to_string(today - *nd_) + " days ago): do it or update the step in the tracker";
            item.d->set("what", str_v(what));
            items.push_back(item);
            continue;
        }
        if (!peq(dget(r, "stage"), "applied") || nd_) continue;
        const auto ad = day_or_none(dget(r, "applied"));
        if (!ad || today - *ad < APP_CHECKIN_DAYS) continue;
        bool touched = false;
        for (const Val& t : iter(L(data, "threads")))
            if (peq(dget(t, "role_id"), dget(r, "id")))
                for (const Val& m : sent_messages(t)) { const auto d = day_or_none(dget(m, "date")); if (d && *d >= *ad) touched = true; }
        if (touched) continue;   // the thread's own follow-up rules cover it
        const std::string company = lower(pystr(orv(dget(r, "company"), str_v(""))));
        List people;
        for (const Val& c : iter(L(data, "contacts")))
            if (lower(pystr(orv(dget(c, "company"), str_v("")))) == company && !said_no(data, dget(c, "id"))) people.push_back(c);
        Val item = Val::dict();
        upd(item, {{"kind", str_v("application")}, {"role_id", dget(r, "id")}, {"company", dget(r, "company")}, {"title", dget(r, "title")}, {"date", str_v(iso(*ad))}});
        const std::string days = std::to_string(today - *ad);
        if (!people.empty()) {
            item.d->set("contact_id", dget(people[0], "id"));
            item.d->set("what", str_v("applied " + iso(*ad) + " (" + days + " days, no news): one check-in to " + pystr(dget(people[0], "name")) + " (template application-check-in)"));
        } else {
            item.d->set("what", str_v("applied " + iso(*ad) + " (" + days + " days, no news) and no contact at " + pystr(dget(r, "company")) + " yet: /hunter-connector finds the hiring manager"));
        }
        items.push_back(item);
    }
    std::stable_sort(items.begin(), items.end(), [](const Val& a, const Val& b) {
        const std::string& da = a.d->get("date")->s; const std::string& db = b.d->get("date")->s;
        if (da != db) return da < db;
        const Val ra = orv(*a.d->get("role_id"), str_v("")), rb = orv(*b.d->get("role_id"), str_v(""));
        return peq(ra, rb) ? false : plt(ra, rb);
    });
    return items;
}

inline int state_order(const std::string& s) {
    static const std::vector<std::string> o = {"due", "your-turn", "draft", "waiting", "stopped", "done"};
    const auto it = std::find(o.begin(), o.end(), s); return it == o.end() ? 9 : static_cast<int>(it - o.begin());
}
inline Val due_report(const Val& data, long long today) {
    List people;
    for (const Val& c : iter(L(data, "contacts"))) {
        Val st = contact_state(data, dget(c, "id"), today);
        st.d->set("name", dget(c, "name")); st.d->set("company", dget(c, "company"));
        if (st.d->get("state")->s != "none") people.push_back(st);
    }
    std::stable_sort(people.begin(), people.end(), [](const Val& a, const Val& b) {
        const int oa = state_order(a.d->get("state")->s), ob = state_order(b.d->get("state")->s);
        if (oa != ob) return oa < ob;
        const std::string da = pystr(orv(*a.d->get("due"), str_v(""))), db = pystr(orv(*b.d->get("due"), str_v("")));
        if (da != db) return da < db;
        const Val ca = orv(*a.d->get("contact_id"), str_v("")), cb = orv(*b.d->get("contact_id"), str_v(""));
        return peq(ca, cb) ? false : plt(ca, cb);
    });
    Val rep = Val::dict();
    upd(rep, {{"today", str_v(iso(today))}, {"people", Val::list(people)}, {"applications", Val::list(application_items(data, today))}});
    return rep;
}
inline std::string render_due(const Val& rep) {
    std::vector<std::string> out = {"Follow-ups for " + rep.d->get("today")->s + " (drafts only: nothing goes out without your yes)"};
    const std::vector<std::pair<std::string, std::string>> groups = {{"due", "DUE NOW"}, {"your-turn", "YOUR TURN (they replied)"},
        {"draft", "DRAFTS WAITING FOR YOUR YES"}, {"waiting", "WAITING"}, {"stopped", "STOPPED (stop rules)"}, {"done", "DONE"}};
    for (const auto& [key, title] : groups) {
        List rows; for (const Val& p : *rep.d->get("people")->l) if (p.d->get("state")->s == key) rows.push_back(p);
        if (rows.empty()) continue;
        out.push_back(""); out.push_back(title);
        for (const Val& p : rows) {
            const Val ch = *p.d->get("channel");
            out.push_back("  " + pystr(*p.d->get("name")) + " (" + pystr(*p.d->get("company")) + ")" + (truthy(ch) ? " by " + pystr(ch) : std::string()) + ": " + pystr(*p.d->get("reason")));
            if (key == "due") out.push_back("    draft it: followup.py draft --contact " + pystr(*p.d->get("contact_id")) + " --template " + (peq(*p.d->get("followup"), Val::integer(1)) ? "nudge-1" : "nudge-2"));
            if (key == "your-turn") out.push_back("    draft it: followup.py draft --contact " + pystr(*p.d->get("contact_id")) + " --template confirm-time (if they offered a time)");
        }
    }
    const List& apps = *rep.d->get("applications")->l;
    if (!apps.empty()) {
        out.push_back(""); out.push_back("APPLICATIONS AND NEXT STEPS");
        for (const Val& a : apps) out.push_back("  " + pystr(*a.d->get("company")) + ", " + pystr(*a.d->get("title")) + ": " + pystr(*a.d->get("what")));
    }
    if (out.size() == 1) { out.push_back(""); out.push_back("Nothing due. Every thread is waiting on its date or closed."); }
    return join(out, "\n");
}

// ---------------------------------------------------------------- yes: only an explicit yes for this one message counts
inline std::pair<bool, std::string> parse_yes(const std::string& reply) {
    const std::string text = lower(strip(reply));
    if (text.empty()) return {false, "no reply recorded"};
    if (text.find('?') != std::string::npos) return {false, "a question is not a yes"};
    std::vector<std::string> words; std::string w;   // re.findall(r"[a-z']+", text)
    for (const char ch : text) { if ((ch >= 'a' && ch <= 'z') || ch == '\'') w += ch; else if (!w.empty()) { words.push_back(w); w.clear(); } }
    if (!w.empty()) words.push_back(w);
    if (words.empty() || std::find(YES_WORDS.begin(), YES_WORDS.end(), words[0]) == YES_WORDS.end()) return {false, "the reply must start with \"yes\""};
    for (const auto& x : words) if (BATCH_WORDS.count(x)) return {false, "one yes covers one message; ask again for each"};
    for (const auto& x : words) if (DOUBT_WORDS.count(x)) return {false, "the reply has a condition or a doubt in it"};
    return {true, "explicit yes"};
}

// ---------------------------------------------------------------- drafting
inline Val profile_name(const std::string& path) {
    if (path.empty() || !exists(path)) return Val();
    Val p;
    try { p = loads(read_text(path)); }
    catch (const OSError&) { return Val(); }
    catch (const PyErr& e) { if (e.type == "json.decoder.JSONDecodeError" || e.type == "UnicodeDecodeError" || e.type == "ValueError") return Val(); throw; }
    return dget(orv(p, Val::dict()), "name");
}
inline std::vector<std::string> placeholders(const std::string& t) {   // re.findall(r"\{([a-z_]+)\}", t)
    std::vector<std::string> o;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '{') continue;
        std::size_t j = i + 1; while (j < t.size() && ((t[j] >= 'a' && t[j] <= 'z') || t[j] == '_')) ++j;
        if (j > i + 1 && j < t.size() && t[j] == '}') { o.push_back(t.substr(i + 1, j - i - 1)); i = j; }
    }
    return o;
}
inline std::pair<std::string, std::vector<std::string>> fill(const Val& tplv, const Val& values) {
    const std::string tpl = re_str(tplv);
    std::string out = tpl; std::vector<std::string> missing;
    for (const auto& key : placeholders(tpl)) {
        const Val v = values.d->at(key, Val());
        if (v.is_none() || peq(v, "")) { missing.push_back(key); out = replace_all(out, "{" + key + "}", "[fill: " + key + "]"); }
        else out = replace_all(out, "{" + key + "}", pystr(v));
    }
    std::sort(missing.begin(), missing.end()); missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    return {out, missing};
}
inline Val draft(const Val& data, const Val& templates, const std::string& name, const Val& contact_id, const Val& role_id, const Val& me,
                 const Val& hook, const Val& subject, const Val& day, std::optional<long long> today) {
    if (!templates.d->has(name)) {
        std::vector<std::string> ks; for (const auto& [k, v] : templates.d->items) ks.push_back(k);
        std::sort(ks.begin(), ks.end());
        throw FollowupError("no template " + str_repr(name) + " (have: " + join(ks, ", ") + ")");
    }
    const Val c = truthy(contact_id) ? contact_by_id(data, contact_id) : Val::dict();
    if (truthy(contact_id)) {
        if (const auto no = said_no(data, contact_id)) throw FollowupError(pystr(dget(c, "name")) + " " + *no + ": nothing is drafted for them, ever");
        if (today && (name == "nudge-1" || name == "nudge-2")) {
            const Val st = contact_state(data, contact_id, *today);
            if (st.d->get("state")->s == "stopped") throw FollowupError("stop rule for " + pystr(dget(c, "name")) + ": " + st.d->get("reason")->s);
        }
    }
    Val r = Val::dict();
    if (truthy(role_id)) r = role_by_id(data, role_id);
    else if (truthy(contact_id)) {
        List rids; for (const Val& t : iter(L(data, "threads"))) if (peq(dget(t, "contact_id"), contact_id) && truthy(dget(t, "role_id"))) rids.push_back(dget(t, "role_id"));
        if (!rids.empty()) r = role_by_id(data, rids.back());
    }
    const auto ad = day_or_none(dget(r, "applied"));
    static const char* MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    Val applied;
    if (ad) { long long y; unsigned m, d; civil_from(*ad, y, m, d); applied = str_v(std::string(MONTHS[m - 1]) + " " + std::to_string(d)); }
    Val values = Val::dict();
    upd(values, {{"first", truthy(c) ? str_v(first_name(dget(c, "name"))) : Val()}, {"role", dget(r, "title")},
                 {"company", orv(dget(r, "company"), dget(c, "company"))}, {"applied", applied},
                 {"my_first", truthy(me) ? str_v(first_name(me)) : Val()}, {"my_name", me}, {"hook", hook}, {"subject", subject}, {"day", day}});
    const Val tpl = templates.d->at(name, Val());
    const auto [subj, m1] = fill(dget(tpl, "subject", str_v("")), values);
    const auto [body, m2] = fill(dget(tpl, "body", str_v("")), values);
    std::set<std::string> miss(m1.begin(), m1.end()); miss.insert(m2.begin(), m2.end());
    {   // re.findall(r"\[fill: ([^\]]+)\]", body template)
        const std::string bt = re_str(dget(tpl, "body", str_v("")));
        for (std::size_t k = bt.find("[fill: "); k != std::string::npos; k = bt.find("[fill: ", k + 1)) {
            const std::size_t e = bt.find(']', k + 7);
            if (e != std::string::npos && e > k + 7) { miss.insert(bt.substr(k + 7, e - k - 7)); k = e; }
        }
    }
    List ml; for (const auto& x : miss) ml.push_back(str_v(x));
    Val o = Val::dict();
    upd(o, {{"template", str_v(name)}, {"use", dget(tpl, "use")}, {"subject", str_v(subj)}, {"body", str_v(body)}, {"missing", Val::list(ml)}});
    return o;
}

// ---------------------------------------------------------------- writing
inline std::optional<Val> find_thread(const Val& data, const Val& cid, const std::string& channel, const Val& role_id) {
    List cands; for (const Val& t : iter(L(data, "threads"))) if (peq(dget(t, "contact_id"), cid) && peq(dget(t, "channel"), channel.c_str())) cands.push_back(t);
    if (truthy(role_id)) {
        for (std::size_t i = cands.size(); i-- > 0;) if (peq(dget(cands[i], "role_id"), role_id)) return cands[i];
        return std::nullopt;
    }
    if (cands.empty()) return std::nullopt;
    return cands.back();
}
inline Val new_thread(Val& data, const Val& cid, const Val& role_id, const std::string& channel, const char* status) {
    Val t = Val::dict();
    upd(t, {{"contact_id", cid}, {"role_id", role_id}, {"channel", str_v(channel)}, {"messages", Val::list()}, {"status", str_v(status)}, {"next_followup", Val()}});
    append(L(data, "threads"), t);
    return t;
}
inline void add_message(Val& t, const Val& msg) {   // t.setdefault("messages", []).append(msg)
    if (!t.d->has("messages")) t.d->set("messages", Val::list());
    append(*const_cast<Val*>(t.d->get("messages")), msg);
}
inline Val message(long long today, const char* direction, const std::string& text) {
    Val m = Val::dict(); upd(m, {{"date", str_v(iso(today))}, {"direction", str_v(direction)}, {"text", str_v(text)}}); return m;
}
// log a message that went out after the user's yes: (ok, message)
inline std::pair<bool, std::string> record(Val& data, const Val& cid, const std::string& channel, const Val& text, const std::string& approval, long long today, const Val& role_id) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + ". Ask again, for this one message."};
    const Val c = contact_by_id(data, cid);
    const Val st = contact_state(data, cid, today);
    if (const auto no = said_no(data, cid)) return {false, "Not recorded: " + pystr(dget(c, "name")) + " " + *no + ". Nothing more goes to them."};
    if (st.d->get("state")->s == "stopped") return {false, "Not recorded: stop rule, " + st.d->get("reason")->s + "."};
    if (!truthy(text) || strip(need_str(text, "strip")).empty()) return {false, "Not recorded: the message text is empty."};
    const auto found = find_thread(data, cid, channel, role_id);
    Val t = found ? *found : new_thread(data, cid, role_id, channel, "sent");
    if (dget(t, "draft").t == Val::DICT) { pop_key(t, "draft"); pop_key(t, "approval"); }   // hunter-connector's draft just went out
    else if (has_pending_draft(t)) {   // the draft that just went out: drop it, the sent text replaces it
        const Val mv = orv(dget(t, "messages"), Val::list());
        const List all = iter(mv);
        std::vector<std::size_t> flagged;
        for (std::size_t i = 0; i < all.size(); ++i) if (truthy(dget(all[i], "draft"))) flagged.push_back(i);
        if (mv.t == Val::LIST) {
            List& msgs = *mv.l;
            if (!flagged.empty()) { for (std::size_t j = flagged.size(); j-- > 0;) msgs.erase(msgs.begin() + static_cast<long>(flagged[j])); }
            else for (std::size_t i = msgs.size(); i-- > 0;) if (peq(dget(msgs[i], "direction"), "out")) { msgs.erase(msgs.begin() + static_cast<long>(i)); break; }
        }
    }
    add_message(t, message(today, "out", strip(text.s)));
    if (!peq(dget(t, "status"), "booked")) t.d->set("status", str_v("sent"));
    const Val after = contact_state(data, cid, today);
    for (Val tt : iter(L(data, "threads")))
        if (peq(dget(tt, "contact_id"), cid) && !in_list(dget(tt, "status"), {"booked", "declined", "closed"})) tt.d->set("next_followup", Val());
    const std::string as = after.d->get("state")->s;
    if ((as == "waiting" || as == "due") && truthy(*after.d->get("due"))) t.d->set("next_followup", *after.d->get("due"));
    const std::string s = st.d->get("state")->s;
    std::string kind;
    if (s == "your-turn") kind = "reply";
    else if (s == "done") kind = "message";
    else if ((s == "none" || s == "draft") && !truthy(*st.d->get("opening"))) kind = "first message";
    else {
        const Val f = orv(*st.d->get("followup"), Val::integer(st.d->get("followups_sent")->i + 1));
        kind = "follow-up " + pystr(f) + " of " + std::to_string(MAX_FOLLOWUPS);
    }
    const Val nf = dget(t, "next_followup");
    const std::string nxt = truthy(nf) ? "next follow-up " + pystr(nf) : "no more follow-ups scheduled";
    return {true, "Recorded the " + kind + " to " + pystr(dget(c, "name")) + " by " + channel + " on " + iso(today) + " (" + nxt + ")."};
}
inline std::string log_reply(Val& data, const Val& cid, const std::string& channel, const std::string& text, long long today, const Val& role_id) {
    const Val c = contact_by_id(data, cid);
    const auto found = find_thread(data, cid, channel, role_id);
    Val t = found ? *found : new_thread(data, cid, role_id, channel, "replied");
    add_message(t, message(today, "in", strip(text)));
    if (search_i(NO_RE(), text)) {
        for (Val tt : iter(L(data, "threads"))) if (peq(dget(tt, "contact_id"), cid)) { tt.d->set("status", str_v("declined")); tt.d->set("next_followup", Val()); }
        return "Logged. " + pystr(dget(c, "name")) + " said no: every thread with them is closed for good. Thank them once only if you want to, then stop.";
    }
    if (!peq(dget(t, "status"), "booked")) t.d->set("status", str_v("replied"));
    for (Val tt : iter(L(data, "threads"))) if (peq(dget(tt, "contact_id"), cid)) tt.d->set("next_followup", Val());
    return "Logged the reply from " + pystr(dget(c, "name")) + ". Your turn: answer them (no follow-up rules apply to a reply).";
}
inline std::string mark_stop(Val& data, const Val& cid, long long today, const Val& said) {
    Val c = contact_by_id(data, cid);
    List threads = threads_of(data, cid);
    if (truthy(said) && !threads.empty()) add_message(threads.back(), message(today, "in", said.s));
    for (Val& t : threads) { t.d->set("status", str_v("declined")); t.d->set("next_followup", Val()); }
    const std::string note = iso(today) + ": DO NOT CONTACT (asked to stop). Honour forever.";
    const Val notes = dget(c, "notes");
    c.d->set("notes", str_v(strip(rstrip(truthy(notes) ? need_str(notes, "rstrip") : "") + "\n" + note)));
    return pystr(dget(c, "name")) + " is marked do not contact. " + std::to_string(threads.size()) + " thread(s) closed. Nothing will be drafted for them again.";
}

// ---------------------------------------------------------------- CLI
inline Val text_arg(const Val& a) {
    if (truthy(get(a, "text_file"))) return str_v(read_text(S(a, "text_file")));
    return get(a, "text");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("followup", "Polite follow-ups with stop rules, from hunter/pipeline.json. Drafts only.");
    p.opt({"--file"}).def("hunter/pipeline.json").help("pipeline file (default hunter/pipeline.json)");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) { s.opt({"--file"}).suppress(); s.opt({"--today"}).help("YYYY-MM-DD (default: the computer's date)"); };
    { Parser& s = p.sub("due"); s.help_line = "what is due today"; common(s); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("draft"); s.help_line = "fill a template for one contact or role"; common(s);
      s.opt({"--template"}).required(); s.opt({"--contact"}); s.opt({"--role"});
      s.opt({"--me"}).help("your name (default: name in hunter/profile.json)"); s.opt({"--profile"}).def("hunter/profile.json");
      s.opt({"--hook"}).help("one new, specific, true line"); s.opt({"--subject"}).help("the subject of the first email, for Re: lines");
      s.opt({"--day"}).help("for confirm-time: the day and time they offered"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("record"); s.help_line = "log a message that went out after your yes"; common(s);
      s.opt({"--contact"}).required(); s.opt({"--channel"}).required(); s.opt({"--role"}); s.opt({"--text"}); s.opt({"--text-file"});
      s.opt({"--approval"}).required().help("the user's reply, word for word"); }
    { Parser& s = p.sub("reply"); s.help_line = "log their answer"; common(s);
      s.opt({"--contact"}).required(); s.opt({"--channel"}).required(); s.opt({"--role"}); s.opt({"--text"}); s.opt({"--text-file"}); }
    { Parser& s = p.sub("stop"); s.help_line = "mark a person do-not-contact, forever"; common(s);
      s.opt({"--contact"}).required(); s.opt({"--said"}).help("their words, logged as their last message"); }
    { Parser& s = p.sub("templates"); s.help_line = "list the templates"; common(s); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    try {
        if (cmd == "templates") {
            const Val templates = load_templates();
            for (const auto& [k, v] : templates.d->items) {
                const long n = static_cast<long>(cps(k).size());
                print(k + std::string(static_cast<std::size_t>(std::max(0L, 22 - n)), ' ') + " " + pystr(dget(v, "use", str_v(""))));
            }
            return 0;
        }
        const std::string file = S(a, "file");
        Val data = load(file);
        const long long today = truthy(get(a, "today")) ? parse_day(get(a, "today"), "--today") : parse_day(str_v(today_iso()));
        if (cmd == "due") {
            const Val rep = due_report(data, today);
            DumpOpts jo; jo.indent = 2;
            print(B(a, "json") ? dumps(rep, jo) : render_due(rep));
            return 0;
        }
        if (cmd == "draft") {
            if (!truthy(get(a, "contact")) && !truthy(get(a, "role"))) throw FollowupError("give --contact or --role");
            const Val me = truthy(get(a, "me")) ? get(a, "me") : profile_name(truthy(get(a, "profile")) ? S(a, "profile") : "");
            const Val d = draft(data, load_templates(), S(a, "template"), get(a, "contact"), get(a, "role"), me, get(a, "hook"), get(a, "subject"), get(a, "day"), today);
            if (B(a, "json")) { DumpOpts jo; jo.indent = 2; jo.ensure_ascii = false; print(dumps(d, jo)); }
            else {
                print("DRAFT (" + d.d->get("template")->s + "). Show it to the user; it goes out only after their yes.\n");
                print("Subject: " + d.d->get("subject")->s);
                print("");
                print(d.d->get("body")->s);
                const List& miss = *d.d->get("missing")->l;
                if (!miss.empty()) { std::vector<std::string> ms; for (const Val& m : miss) ms.push_back(m.s); print(""); print("Fill in before showing: " + join(ms, ", ")); }
            }
            return 0;
        }
        if (cmd == "record") {
            const auto [ok, msg] = record(data, get(a, "contact"), S(a, "channel"), text_arg(a), S(a, "approval"), today, get(a, "role"));
            print(msg);
            if (ok) save(file, data);
            return ok ? 0 : 1;
        }
        if (cmd == "reply") {
            const Val text = text_arg(a);
            if (!truthy(text)) throw FollowupError("give --text or --text-file");
            const std::string msg = log_reply(data, get(a, "contact"), S(a, "channel"), text.s, today, get(a, "role"));
            save(file, data);
            print(msg);
            return 0;
        }
        if (cmd == "stop") {
            const std::string msg = mark_stop(data, get(a, "contact"), today, get(a, "said"));
            save(file, data);
            print(msg);
            return 0;
        }
    } catch (const FollowupError& e) {
        eprint(std::string("followup: ") + e.what()); return 2;
    }
    return 2;
}
PACKTOOL("hunter-followup/followup", main);
}  // namespace packtools::hunter_followup
