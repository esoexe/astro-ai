// hunter-tracker/pipeline: every role, contact and conversation in hunter/pipeline.json, with its schema checked.
// A C++ port of skills/hunter-tracker/pipeline.py (MIT, Jake Schincariol). Nothing here sends anything: it is a record.
#pragma once

namespace packtools::hunter_tracker {
using namespace pyrt;
using hunter_followup::peq; using hunter_followup::plt; using hunter_followup::in_list; using hunter_followup::str_v;
using hunter_followup::stamp; using hunter_followup::nd; using hunter_followup::nd_value; using hunter_followup::need_str;
using hunter_followup::re_str; using hunter_followup::upd; using hunter_followup::or_iter; using hunter_followup::append;

struct PipelineError : std::runtime_error { using std::runtime_error::runtime_error; };

using SV = std::vector<std::string>;
inline const SV STAGES = {"saved", "applied", "screen", "interview", "onsite", "offer", "rejected", "withdrawn"};
inline const SV ACTIVE = {"saved", "applied", "screen", "interview", "onsite", "offer"};
inline const SV CLOSED = {"rejected", "withdrawn"};
inline const SV NEEDS_APPLIED = {"applied", "screen", "interview", "onsite", "offer"};
inline const SV THREAD_STATUSES = {"drafted", "sent", "replied", "booked", "declined", "closed"};
inline const SV CHANNELS = {"email", "linkedin", "phone", "in-person", "other"};
inline const SV DIRECTIONS = {"out", "in"};
inline const SV ROLE_FIELDS = {"id", "company", "title", "url", "stage", "applied", "next_step", "next_date", "notes"};
inline const SV CONTACT_FIELDS = {"id", "name", "role", "company", "linkedin", "email", "email_confidence", "source", "notes"};
inline const SV THREAD_FIELDS = {"contact_id", "role_id", "channel", "messages", "status", "next_followup"};
inline const SV MESSAGE_FIELDS = {"date", "direction", "text"};
inline const SV THREAD_OPTIONAL = {"invite", "draft", "approval", "booked"};
inline const SV CONTACT_OPTIONAL = {"do_not_contact"};
inline const SV CONFIDENCE_WORDS = {"published", "high", "medium", "low", "unknown"};
inline bool in(const std::string& s, const SV& l) { return std::find(l.begin(), l.end(), s) != l.end(); }

// ---------------------------------------------------------------- Python sets and dicts keyed by JSON values
inline void hashable(const Val& v) {
    if (v.t == Val::LIST || v.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(v) + "'");
}
struct PySet {
    List items;
    bool has(const Val& v) const { hashable(v); for (const Val& x : items) if (peq(x, v)) return true; return false; }
    void add(const Val& v) { if (!has(v)) items.push_back(v); }
};
struct PyMap {   // {key: value}: a later equal key replaces the value
    std::vector<std::pair<Val, Val>> items;
    void set(const Val& k, const Val& v) { hashable(k); for (auto& kv : items) if (peq(kv.first, k)) { kv.second = v; return; } items.emplace_back(k, v); }
    Val get(const Val& k, const Val& dflt) const { hashable(k); for (const auto& kv : items) if (peq(kv.first, k)) return kv.second; return dflt; }
};
inline long long pylen(const Val& v) {
    switch (v.t) {
        case Val::LIST: return static_cast<long long>(v.l->size());
        case Val::DICT: return static_cast<long long>(v.d->items.size());
        case Val::STR: return static_cast<long long>(cps(v.s).size());
        default: throw PyErr("TypeError", std::string("object of type '") + type_name(v) + "' has no len()");
    }
}
inline bool empty_v(const Val& v) { return v.is_none() || peq(v, ""); }   // v in (None, "")

// ---------------------------------------------------------------- dates
inline long long parse_day(const Val& s, const std::string& what = "date") {
    const auto ymd = stamp(strip(truthy(s) ? pystr(s) : ""));
    if (!ymd) throw PipelineError(what + " must be YYYY-MM-DD, got " + repr(s));
    try { return py_date((*ymd)[0], (*ymd)[1], (*ymd)[2]); }
    catch (const PyErr& e) { if (e.type != "ValueError") throw; throw PipelineError(what + " is not a real date: " + repr(s)); }
}
inline std::optional<long long> day_or_none(const Val& s) {
    if (empty_v(s)) return std::nullopt;
    try { return parse_day(s); } catch (const PipelineError&) { return std::nullopt; }
}
inline long long today_from(const Val& arg) { return truthy(arg) ? parse_day(arg, "--today") : parse_day(str_v(today_iso())); }
inline std::string iso(long long d) { return date_iso(d); }
// STAMP_RE / DATE_RE .match(s): as a regex, $ also matches before one final "\n"
inline bool stamp_match(const std::string& s, bool time) {
    if (stamp(s, time)) return true;
    return !s.empty() && s.back() == '\n' && stamp(s.substr(0, s.size() - 1), time);
}

// ---------------------------------------------------------------- file
inline Val empty_pipeline() { Val d = Val::dict(); upd(d, {{"roles", Val::list()}, {"contacts", Val::list()}, {"threads", Val::list()}}); return d; }
inline Val load(const std::string& path, bool must_exist = true) {
    if (!exists(path)) {
        if (must_exist) throw PipelineError("no pipeline at " + path + " (run: pipeline.py init)");
        return empty_pipeline();
    }
    Val data;
    try { data = loads(read_text(path)); }
    catch (const PyErr& e) { if (e.type != "json.decoder.JSONDecodeError") throw; throw PipelineError(path + " is not valid JSON: " + e.what()); }
    if (data.t != Val::DICT) throw PipelineError(path + " must hold a JSON object with roles, contacts and threads");
    for (const char* k : {"roles", "contacts", "threads"}) if (data.d->at(k, Val()).is_none()) data.d->set(k, Val::list());
    return data;
}
inline Val& L(const Val& data, const char* k) { return *const_cast<Val*>(data.d->get(k)); }

inline std::string slug(const std::vector<std::string>& parts) {
    std::vector<std::string> keep; for (const auto& p : parts) if (!p.empty()) keep.push_back(p);
    std::string s = lower(join(keep, "-"));
    s = replace_all(replace_all(replace_all(s, "&", " and "), "+", "p"), "#", "sharp");
    std::string o; bool dash = false;   // re.sub(r"[^a-z0-9]+", "-", s)
    for (const char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) { o += ch; dash = false; }
        else if (!dash) { o += '-'; dash = true; }
    }
    std::size_t a = 0, b = o.size();
    while (a < b && o[a] == '-') ++a;
    while (b > a && o[b - 1] == '-') --b;
    o = o.substr(a, b - a);
    return o.empty() ? "item" : o;
}
inline std::string unique_id(const std::string& base, const PySet& taken) {
    if (!taken.has(str_v(base))) return base;
    long n = 2;
    while (taken.has(str_v(base + "-" + std::to_string(n)))) ++n;
    return base + "-" + std::to_string(n);
}
inline Val find(const Val& items, const std::string& ident, const std::string& what) {
    for (const Val& it : iter(items)) if (peq(dget(it, "id"), ident.c_str())) return it;
    throw PipelineError("no " + what + " with id " + str_repr(ident));
}
inline void add_note(Val& rec, long long day, const std::string& text) {
    const std::string line = iso(day) + ": " + text;
    const Val notes = dget(rec, "notes");
    rec.d->set("notes", str_v(rstrip(truthy(notes) ? need_str(notes, "rstrip") : "")));
    rec.d->set("notes", str_v(strip(rec.d->get("notes")->s + "\n" + line)));
}

// ---------------------------------------------------------------- validation
using Msgs = std::vector<std::string>;
inline void date_field(Msgs& errors, const std::string& where, const Val& rec, const std::string& key, bool allow_time = false) {
    const Val v = dget(rec, key);
    if (empty_v(v)) return;
    if (!stamp_match(pystr(v), allow_time) || !day_or_none(v)) errors.push_back(where + ": " + key + " must be YYYY-MM-DD, got " + repr(v));
}
inline void text_field(Msgs& errors, const std::string& where, const Val& rec, const std::string& key, bool required) {
    const Val v = dget(rec, key);
    if (empty_v(v)) { if (required) errors.push_back(where + ": " + key + " is required"); return; }
    if (v.t != Val::STR) errors.push_back(where + ": " + key + " must be text, got " + repr(v));
}
// (error, warning) for an email_confidence value
inline std::pair<std::optional<std::string>, std::optional<std::string>> check_confidence(const Val& value, const Val& source) {
    if (empty_v(value)) return {};
    if (value.t == Val::BOOL) return {"email_confidence must be a word or a number from 0 to 1, got " + repr(value), std::nullopt};
    if (value.t == Val::INT || value.t == Val::FLOAT) {
        const double x = value.t == Val::INT ? static_cast<double>(value.i) : value.f;
        if (0 <= x && x <= 1) return {};
        if (1 < x && x <= 100) return {std::nullopt, "email_confidence " + repr(value) + " looks like a percent; use 0 to 1"};
        return {"email_confidence must be from 0 to 1, got " + repr(value), std::nullopt};
    }
    std::string word = lower(strip(pystr(value)));
    if (word == "verified") word = "published";
    if (!in(word, CONFIDENCE_WORDS)) return {"email_confidence must be one of " + join(CONFIDENCE_WORDS, ", ") + " or a number from 0 to 1, got " + repr(value), std::nullopt};
    if (word == "published" && pystr(orv(source, str_v(""))).find("http") == std::string::npos)
        return {"email_confidence " + repr(value) + " needs the public page where the address is published in 'source' (a URL); otherwise it is a guess", std::nullopt};
    return {};
}
// EMAIL_RE: ^[^@\s]+@[^@\s]+\.[A-Za-z]{2,}$  ($ also before one final "\n")
inline bool email_ok(std::string s) {
    if (!s.empty() && s.back() == '\n') s.pop_back();
    const auto cs = cps(s);
    std::size_t at = std::string::npos, n_at = 0;
    for (std::size_t i = 0; i < cs.size(); ++i) { const uint32_t c = cp_of(cs[i]); if (py_space(c)) return false; if (c == '@') { at = i; ++n_at; } }
    if (n_at != 1 || at == 0) return false;
    std::size_t dot = std::string::npos; for (std::size_t i = at + 1; i < cs.size(); ++i) if (cs[i] == ".") dot = i;
    if (dot == std::string::npos || dot == at + 1 || cs.size() - dot - 1 < 2) return false;
    for (std::size_t i = dot + 1; i < cs.size(); ++i) { const std::string& c = cs[i]; if (c.size() != 1 || !std::isalpha(static_cast<unsigned char>(c[0]))) return false; }
    return true;
}
inline bool is_dict(const Val& v) { return v.t == Val::DICT; }

// check the whole file against the schema: (errors, warnings)
inline std::pair<Msgs, Msgs> validate(const Val& data) {
    Msgs errors, warnings;
    if (!is_dict(data)) return {{"the file must hold a JSON object"}, {}};
    for (const char* k : {"roles", "contacts", "threads"}) if (data.d->at(k, Val()).t != Val::LIST) errors.push_back(std::string(k) + " must be a list");
    for (const auto& [k, v] : data.d->items) if (k != "roles" && k != "contacts" && k != "threads" && !starts(k, "_")) warnings.push_back("extra top-level field " + str_repr(k) + " (kept)");
    if (!errors.empty()) return {errors, warnings};

    PySet role_ids, contact_ids;
    long i = -1;
    for (const Val& r : *L(data, "roles").l) {
        ++i;
        std::string where = "roles[" + std::to_string(i) + "]";
        if (!is_dict(r)) { errors.push_back(where + " must be an object"); continue; }
        if (truthy(dget(r, "id"))) where = "role " + pystr(dget(r, "id"));
        for (const char* k : {"id", "company", "title"}) text_field(errors, where, r, k, true);
        for (const char* k : {"url", "next_step", "notes"}) text_field(errors, where, r, k, false);
        if (role_ids.has(dget(r, "id"))) errors.push_back(where + ": duplicate id");
        role_ids.add(dget(r, "id"));
        const Val stage = dget(r, "stage");
        if (!in_list(stage, STAGES)) errors.push_back(where + ": stage must be one of " + join(STAGES, ", ") + ", got " + repr(stage));
        date_field(errors, where, r, "applied");
        date_field(errors, where, r, "next_date");
        const Val url = dget(r, "url");
        if (truthy(url) && url.t == Val::STR && !starts(url.s, "http://") && !starts(url.s, "https://")) warnings.push_back(where + ": url should start with https://");
        if (truthy(dget(r, "next_date")) && !truthy(dget(r, "next_step"))) warnings.push_back(where + ": next_date without a next_step");
        if (in_list(stage, NEEDS_APPLIED) && !truthy(dget(r, "applied"))) warnings.push_back(where + ": stage " + stage.s + " but no applied date");
        for (const auto& [k, v] : r.d->items) if (!in(k, ROLE_FIELDS)) warnings.push_back(where + ": extra field " + str_repr(k) + " (kept)");
    }
    i = -1;
    for (const Val& c : *L(data, "contacts").l) {
        ++i;
        std::string where = "contacts[" + std::to_string(i) + "]";
        if (!is_dict(c)) { errors.push_back(where + " must be an object"); continue; }
        if (truthy(dget(c, "id"))) where = "contact " + pystr(dget(c, "id"));
        for (const char* k : {"id", "name", "company"}) text_field(errors, where, c, k, true);
        for (const char* k : {"role", "linkedin", "email", "source", "notes"}) text_field(errors, where, c, k, false);
        if (contact_ids.has(dget(c, "id"))) errors.push_back(where + ": duplicate id");
        contact_ids.add(dget(c, "id"));
        const Val email = dget(c, "email");
        if (truthy(email) && email.t == Val::STR && !email_ok(email.s)) errors.push_back(where + ": email " + repr(email) + " is not an email address");
        const auto [err, warn] = check_confidence(dget(c, "email_confidence"), dget(c, "source"));
        if (err) errors.push_back(where + ": " + *err);
        if (warn) warnings.push_back(where + ": " + *warn);
        if (truthy(email) && empty_v(dget(c, "email_confidence"))) warnings.push_back(where + ": email without email_confidence (a guess unless it was found published)");
        const Val li = dget(c, "linkedin");
        if (truthy(li) && li.t == Val::STR && li.s.find("linkedin.com/") == std::string::npos) warnings.push_back(where + ": linkedin should be a linkedin.com URL");
        if (c.d->has("do_not_contact") && c.d->get("do_not_contact")->t != Val::BOOL) errors.push_back(where + ": do_not_contact must be true or false");
        for (const auto& [k, v] : c.d->items) if (!in(k, CONTACT_FIELDS) && !in(k, CONTACT_OPTIONAL)) warnings.push_back(where + ": extra field " + str_repr(k) + " (kept)");
    }
    std::vector<std::array<Val, 3>> seen;
    i = -1;
    for (const Val& t : *L(data, "threads").l) {
        ++i;
        const std::string where = "threads[" + std::to_string(i) + "]";
        if (!is_dict(t)) { errors.push_back(where + " must be an object"); continue; }
        const Val cid = dget(t, "contact_id"), rid = dget(t, "role_id"), ch = dget(t, "channel"), status = dget(t, "status");
        if (!contact_ids.has(cid)) errors.push_back(where + ": contact_id " + repr(cid) + " is not in contacts");
        if (!empty_v(rid) && !role_ids.has(rid)) errors.push_back(where + ": role_id " + repr(rid) + " is not in roles");
        if (!truthy(ch) || ch.t != Val::STR) errors.push_back(where + ": channel is required (email, linkedin, ...)");
        else if (!in(ch.s, CHANNELS)) warnings.push_back(where + ": channel " + repr(ch) + " is not one of " + join(CHANNELS, ", "));
        for (const Val* x : {&cid, &rid, &ch}) hashable(*x);
        bool dup = false; for (const auto& k : seen) if (peq(k[0], cid) && peq(k[1], rid) && peq(k[2], ch)) dup = true;
        if (dup) warnings.push_back(where + ": a second thread for the same contact, role and channel");
        else seen.push_back({cid, rid, ch});
        if (!in_list(status, THREAD_STATUSES)) errors.push_back(where + ": status must be one of " + join(THREAD_STATUSES, ", ") + ", got " + repr(status));
        date_field(errors, where, t, "next_followup");
        Val msgs = dget(t, "messages");
        if (msgs.t != Val::LIST) { errors.push_back(where + ": messages must be a list"); msgs = Val::list(); }
        std::optional<long long> last;
        long j = -1;
        bool outs = false, ins = false;
        for (const Val& m : *msgs.l) {
            ++j;
            const std::string mw = where + ".messages[" + std::to_string(j) + "]";
            if (!is_dict(m)) { errors.push_back(mw + " must be an object"); continue; }
            date_field(errors, mw, m, "date", true);
            if (empty_v(dget(m, "date"))) errors.push_back(mw + ": date is required");
            const Val dir = dget(m, "direction");
            if (!in_list(dir, DIRECTIONS)) errors.push_back(mw + ": direction must be out or in, got " + repr(dir));
            if (dget(m, "text").t != Val::STR) errors.push_back(mw + ": text is required");
            const auto d = day_or_none(dget(m, "date"));
            if (d && last && *d < *last) warnings.push_back(mw + ": messages are out of date order");
            if (d) last = d;
            for (const auto& [k, v] : m.d->items) if (!in(k, MESSAGE_FIELDS)) warnings.push_back(mw + ": extra field " + str_repr(k) + " (kept)");
            if (peq(dir, "out")) outs = true;
            if (peq(dir, "in")) ins = true;
        }
        const std::string st = status.t == Val::STR ? status.s : "";
        if ((st == "sent" || st == "replied" || st == "booked") && !outs) warnings.push_back(where + ": status " + st + " but no outgoing message");
        if ((st == "replied" || st == "booked") && !ins) warnings.push_back(where + ": status " + st + " but no reply logged");
        if ((st == "declined" || st == "closed" || st == "booked") && truthy(dget(t, "next_followup"))) warnings.push_back(where + ": next_followup on a " + st + " thread (stop rules: nothing more is sent)");
        if (t.d->has("invite") && t.d->get("invite")->t != Val::BOOL) errors.push_back(where + ": invite must be true or false");
        for (const char* k : {"draft", "approval", "booked"}) { const Val v = dget(t, k); if (!v.is_none() && !is_dict(v)) errors.push_back(where + ": " + k + " must be an object"); }
        const Val draft = dget(t, "draft");
        if (is_dict(draft) && dget(draft, "text").t != Val::STR) errors.push_back(where + ": draft.text is required");
        if (truthy(dget(t, "approval")) && !truthy(draft)) warnings.push_back(where + ": an approval with no draft (it went out or was dropped)");
        for (const auto& [k, v] : t.d->items) if (!in(k, THREAD_FIELDS) && !in(k, THREAD_OPTIONAL)) warnings.push_back(where + ": extra field " + str_repr(k) + " (kept)");
    }
    return {errors, warnings};
}

// ---------------------------------------------------------------- activity
inline std::vector<std::string> note_dates(const std::string& text) {   // re.findall(r"(?m)^(\d{4}-\d{2}-\d{2}):", text)
    std::vector<std::string> o;
    const auto cs = cps(text);
    for (std::size_t i = 0; i < cs.size(); ++i) {
        if (i > 0 && cs[i - 1] != "\n") continue;
        std::size_t k = i; std::string got; bool ok = true;
        for (const int n : {4, -1, 2, -1, 2, -2}) {
            if (n > 0) { for (int q = 0; q < n && ok; ++q) { if (k < cs.size() && nd(cp_of(cs[k]))) got += cs[k++]; else ok = false; } }
            else if (n == -1) { if (k < cs.size() && cs[k] == "-") got += cs[k++]; else ok = false; }
            else { if (!(k < cs.size() && cs[k] == ":")) ok = false; }
            if (!ok) break;
        }
        if (ok) o.push_back(got);
    }
    return o;
}
// the latest date anything happened on a role: applied, a dated note line, a thread message
inline std::optional<long long> last_activity(const Val& data, const Val& role) {
    std::optional<long long> best;
    const auto take = [&](std::optional<long long> d) { if (d && (!best || *d > *best)) best = d; };
    take(day_or_none(dget(role, "applied")));
    for (const auto& s : note_dates(re_str(orv(dget(role, "notes"), str_v(""))))) take(day_or_none(str_v(s)));
    for (const Val& t : iter(dget(data, "threads", Val::list())))
        if (peq(dget(t, "role_id"), dget(role, "id")))
            for (const Val& m : or_iter(dget(t, "messages"))) take(day_or_none(dget(m, "date")));
    return best;
}
struct Stale { Val r; std::optional<long long> last, idle; };
inline std::vector<Stale> stale_roles(const Val& data, long long today, long long days) {
    std::vector<Stale> out;
    for (const Val& r : iter(L(data, "roles"))) {
        if (!in_list(dget(r, "stage"), ACTIVE)) continue;
        const auto nd_ = day_or_none(dget(r, "next_date"));
        if (nd_ && *nd_ >= today) continue;   // something is scheduled
        const auto last = last_activity(data, r);
        std::optional<long long> idle; if (last) idle = today - *last;
        if (!idle || *idle >= days) out.push_back({r, last, idle});
    }
    std::stable_sort(out.begin(), out.end(), [](const Stale& a, const Stale& b) {
        const long long ka = -(a.idle ? *a.idle : 1000000), kb = -(b.idle ? *b.idle : 1000000);
        if (ka != kb) return ka < kb;
        const Val ia = orv(dget(a.r, "id"), str_v("")), ib = orv(dget(b.r, "id"), str_v(""));
        return peq(ia, ib) ? false : plt(ia, ib);
    });
    return out;
}
struct Item { long long d; std::string kind; Val ident; std::string what; };
// next steps on roles and follow-ups on threads, due within `days` (overdue included)
inline std::vector<Item> upcoming(const Val& data, long long today, long long days) {
    const long long horizon = today + days;
    std::vector<Item> items;
    for (const Val& r : iter(L(data, "roles"))) {
        const auto nd_ = day_or_none(dget(r, "next_date"));
        if (in_list(dget(r, "stage"), ACTIVE) && nd_ && *nd_ <= horizon)
            items.push_back({*nd_, "role", dget(r, "id"), pystr(dget(r, "company")) + ", " + pystr(dget(r, "title")) + ": " + pystr(orv(dget(r, "next_step"), str_v("next step")))});
    }
    PyMap names; for (const Val& c : iter(L(data, "contacts"))) names.set(dget(c, "id"), dget(c, "name"));
    for (const Val& t : iter(L(data, "threads"))) {
        const auto nd_ = day_or_none(dget(t, "next_followup"));
        if (nd_ && *nd_ <= horizon && !in_list(dget(t, "status"), {"declined", "closed", "booked"}))
            items.push_back({*nd_, "thread", dget(t, "contact_id"), "follow up with " + pystr(names.get(dget(t, "contact_id"), dget(t, "contact_id"))) + " (" + pystr(dget(t, "channel")) + ")"});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.d != b.d) return a.d < b.d;
        if (a.kind != b.kind) return a.kind < b.kind;
        const Val ia = orv(a.ident, str_v("")), ib = orv(b.ident, str_v(""));
        return peq(ia, ib) ? false : plt(ia, ib);
    });
    return items;
}
inline Val opt_iso(std::optional<long long> d) { return d ? str_v(iso(*d)) : Val(); }
inline Val summary(const Val& data, long long today, long long stale_days = 14) {
    Val by_stage = Val::dict(); for (const auto& s : STAGES) by_stage.d->set(s, Val::integer(0));
    for (const Val& r : iter(L(data, "roles"))) {
        const Val st = dget(r, "stage"); hashable(st);
        if (st.t == Val::STR && by_stage.d->has(st.s)) by_stage.d->set(st.s, Val::integer(by_stage.d->get(st.s)->i + 1));
    }
    Val by_status = Val::dict(); for (const auto& s : THREAD_STATUSES) by_status.d->set(s, Val::integer(0));
    for (const Val& t : iter(L(data, "threads"))) {
        const Val st = dget(t, "status"); hashable(st);
        if (st.t == Val::STR && by_status.d->has(st.s)) by_status.d->set(st.s, Val::integer(by_status.d->get(st.s)->i + 1));
    }
    long long active = 0; for (const auto& s : ACTIVE) active += by_stage.d->get(s)->i;
    long long applied_ever = 0; for (const Val& r : iter(L(data, "roles"))) if (truthy(dget(r, "applied"))) ++applied_ever;
    List next;
    for (const Item& it : upcoming(data, today, 7)) {
        Val n = Val::dict(); upd(n, {{"date", str_v(iso(it.d))}, {"kind", str_v(it.kind)}, {"id", it.ident}, {"what", str_v(it.what)}, {"overdue", Val::boolean(it.d < today)}});
        next.push_back(n);
    }
    List stale;
    for (const Stale& s : stale_roles(data, today, stale_days)) {
        Val n = Val::dict();
        upd(n, {{"id", dget(s.r, "id")}, {"company", dget(s.r, "company")}, {"title", dget(s.r, "title")}, {"stage", dget(s.r, "stage")},
                {"last_activity", opt_iso(s.last)}, {"idle_days", s.idle ? Val::integer(*s.idle) : Val()}});
        stale.push_back(n);
    }
    Val o = Val::dict();
    upd(o, {{"as_of", str_v(iso(today))}, {"roles", Val::integer(pylen(L(data, "roles")))}, {"active", Val::integer(active)}, {"applied", Val::integer(applied_ever)},
            {"by_stage", by_stage}, {"contacts", Val::integer(pylen(L(data, "contacts")))}, {"threads", by_status}, {"next", Val::list(next)}, {"stale", Val::list(stale)}});
    return o;
}
inline std::string plural(long long n, const std::string& word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }
inline std::string render_summary(const Val& s) {
    const auto I = [&](const char* k) { return s.d->get(k)->i; };
    std::vector<std::string> lines = {"Pipeline on " + s.d->get("as_of")->s + ": " + plural(I("roles"), "role") + " (" + std::to_string(I("active")) + " active, " +
                                      std::to_string(I("applied")) + " applied), " + plural(I("contacts"), "contact"), ""};
    lines.push_back("By stage:");
    const Val& bs = *s.d->get("by_stage");
    bool any = false;
    for (const auto& st : STAGES) { const long long n = bs.d->get(st)->i; if (n) { std::string pad = st; pad.resize(std::max<std::size_t>(10, st.size()), ' '); lines.push_back("  " + pad + " " + std::to_string(n)); any = true; } }
    if (!any) lines.push_back("  (no roles yet)");
    std::vector<std::string> conv;
    for (const auto& [st, n] : s.d->get("threads")->d->items) if (n.i) conv.push_back(std::to_string(n.i) + " " + st);
    if (!conv.empty()) { lines.push_back(""); lines.push_back("Conversations: " + join(conv, ", ")); }
    lines.push_back(""); lines.push_back("Next 7 days:");
    const List& next = *s.d->get("next")->l;
    if (!next.empty()) for (const Val& n : next) lines.push_back("  " + n.d->get("date")->s + (n.d->get("overdue")->b ? " OVERDUE" : "") + "  " + n.d->get("what")->s);
    else lines.push_back("  nothing scheduled");
    const List& stale = *s.d->get("stale")->l;
    if (!stale.empty()) {
        lines.push_back(""); lines.push_back("Stale (no activity and nothing scheduled):");
        for (const Val& r : stale) {
            const Val idle = *r.d->get("idle_days");
            lines.push_back("  " + pystr(*r.d->get("id")) + " (" + pystr(*r.d->get("company")) + ", " + pystr(*r.d->get("stage")) + "): " +
                            (idle.is_none() ? std::string("never touched") : std::to_string(idle.i) + " days idle"));
        }
    }
    return join(lines, "\n");
}

// ---------------------------------------------------------------- export (csv.writer: QUOTE_MINIMAL, \r\n)
inline std::string csv_field(const Val& v) {
    const std::string s = v.is_none() ? "" : v.t == Val::STR ? v.s : pystr(v);
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    return "\"" + replace_all(s, "\"", "\"\"") + "\"";
}
inline std::string csv_row(const List& row) { std::vector<std::string> o; for (const Val& v : row) o.push_back(csv_field(v)); return join(o, ",") + "\r\n"; }
inline List strs(const SV& v) { List o; for (const auto& s : v) o.push_back(str_v(s)); return o; }
inline std::string to_csv(const Val& data, const std::string& what) {
    std::string buf;
    if (what == "roles" || what == "contacts") {
        const SV& fields = what == "roles" ? ROLE_FIELDS : CONTACT_FIELDS;
        buf += csv_row(strs(fields));
        for (const Val& r : iter(L(data, what.c_str()))) { List row; for (const auto& k : fields) row.push_back(dget(r, k)); buf += csv_row(row); }
    } else if (what == "threads") {
        buf += csv_row(strs({"contact_id", "role_id", "channel", "status", "next_followup", "messages", "first_date", "last_date", "last_direction"}));
        for (const Val& t : iter(L(data, "threads"))) {
            const Val mv = orv(dget(t, "messages"), Val::list());
            const long long n = pylen(mv);
            Val first = str_v(""), last = str_v(""), dir = str_v("");
            if (n) {
                if (mv.t != Val::LIST) throw PyErr(mv.t == Val::DICT ? "KeyError" : "AttributeError", mv.t == Val::DICT ? "0" : "'str' object has no attribute 'get'");
                first = dget(mv.l->front(), "date", str_v("")); last = dget(mv.l->back(), "date", str_v("")); dir = dget(mv.l->back(), "direction", str_v(""));
            }
            buf += csv_row({orv(dget(t, "contact_id"), str_v("")), orv(dget(t, "role_id"), str_v("")), orv(dget(t, "channel"), str_v("")),
                            orv(dget(t, "status"), str_v("")), orv(dget(t, "next_followup"), str_v("")), Val::integer(n), first, last, dir});
        }
    } else throw PipelineError("--what must be roles, contacts or threads");
    return buf;
}

// ---------------------------------------------------------------- commands
inline void require_valid(const Val& data) {
    const auto errors = validate(data).first;
    if (!errors.empty()) throw PipelineError("the change would leave the pipeline invalid:\n  " + join(errors, "\n  "));
}
inline Val float_or_str(const std::string& s) {   // try: float(s) except ValueError: s  (Unicode digits count, like float())
    std::string ascii;
    for (const auto& ch : cps(s)) { const uint32_t c = cp_of(ch); ascii += (c >= 0x80 && nd(c)) ? std::string(1, static_cast<char>('0' + nd_value(c))) : ch; }
    const auto f = parse_float(ascii);
    return f ? Val::flt(*f) : str_v(s);
}
inline Val opt(const Val& a, const char* k) { return get(a, k); }
inline int cmd_init(const Val& a) {
    const std::string file = S(a, "file");
    if (exists(file)) throw PipelineError(file + " already exists; nothing changed");
    hunter_followup::save(file, empty_pipeline());
    print("Created " + file + " (roles, contacts, threads: all empty)");
    return 0;
}
inline int cmd_validate(const Val& a) {
    const std::string file = S(a, "file");
    const Val data = load(file);
    const auto [errors, warnings] = validate(data);
    if (B(a, "json")) {
        Val o = Val::dict(); upd(o, {{"ok", Val::boolean(errors.empty())}, {"errors", Val::list(strs(errors))}, {"warnings", Val::list(strs(warnings))}});
        DumpOpts jo; jo.indent = 2; print(dumps(o, jo));
    } else {
        if (errors.empty()) print("OK: " + file + " matches the pipeline schema (" + plural(pylen(L(data, "roles")), "role") + ", " +
                                  plural(pylen(L(data, "contacts")), "contact") + ", " + plural(pylen(L(data, "threads")), "thread") + ")");
        for (const auto& e : errors) print("ERROR  " + e);
        for (const auto& w : warnings) print("WARN   " + w);
    }
    return errors.empty() ? 0 : 1;
}
inline int cmd_add_role(const Val& a) {
    const std::string file = S(a, "file");
    Val data = load(file, false);
    const long long today = today_from(opt(a, "today"));
    const std::string company = S(a, "company"), title = S(a, "title");
    const Val url = opt(a, "url");
    for (const Val& r : iter(L(data, "roles"))) {
        const bool same_url = truthy(url) && peq(dget(r, "url"), url);
        const bool same_name = lower(pystr(dget(r, "company", str_v("")))) == lower(company) && lower(pystr(dget(r, "title", str_v("")))) == lower(title);
        if ((same_url || same_name) && !B(a, "force")) {
            print("Already in the pipeline as " + pystr(dget(r, "id")) + " (stage " + pystr(dget(r, "stage")) + "). Use --force to add a second one.");
            return 1;
        }
    }
    const std::string stage = truthy(opt(a, "stage")) ? S(a, "stage") : "saved";
    if (!in(stage, STAGES)) throw PipelineError("stage must be one of " + join(STAGES, ", "));
    PySet taken; for (const Val& r : iter(L(data, "roles"))) taken.add(dget(r, "id"));
    const std::string rid = truthy(opt(a, "id")) ? S(a, "id") : unique_id(slug({company, title}), taken);
    if (taken.has(str_v(rid))) throw PipelineError("id " + str_repr(rid) + " is taken");
    Val applied = opt(a, "applied");
    if (!truthy(applied) && in(stage, NEEDS_APPLIED)) applied = str_v(iso(today));
    Val role = Val::dict();
    upd(role, {{"id", str_v(rid)}, {"company", str_v(company)}, {"title", str_v(title)}, {"url", url}, {"stage", str_v(stage)}, {"applied", applied},
               {"next_step", opt(a, "next_step")}, {"next_date", opt(a, "next_date")}, {"notes", orv(opt(a, "notes"), str_v(""))}});
    add_note(role, today, "added as " + stage);
    append(L(data, "roles"), role);
    require_valid(data);
    hunter_followup::save(file, data);
    print("Added role " + rid + ": " + company + ", " + title + " (" + stage + ")");
    return 0;
}
inline int cmd_update_role(const Val& a) {
    const std::string file = S(a, "file");
    Val data = load(file);
    Val role = find(L(data, "roles"), S(a, "id"), "role");
    std::vector<std::string> changed;
    for (const char* k : {"company", "title", "url", "applied", "next_step", "next_date"}) {
        const Val v = opt(a, k);
        if (!v.is_none()) { role.d->set(k, truthy(v) ? v : Val()); changed.push_back(k); }
    }
    if (B(a, "clear_next")) { role.d->set("next_step", Val()); role.d->set("next_date", Val()); changed.push_back("next step cleared"); }
    if (truthy(opt(a, "notes"))) { add_note(role, today_from(opt(a, "today")), S(a, "notes")); changed.push_back("notes"); }
    if (changed.empty()) { print("Nothing to change."); return 1; }
    require_valid(data);
    hunter_followup::save(file, data);
    print("Updated " + S(a, "id") + ": " + join(changed, ", "));
    return 0;
}
inline int cmd_move(const Val& a) {
    const std::string file = S(a, "file");
    Val data = load(file);
    Val role = find(L(data, "roles"), S(a, "id"), "role");
    const std::string stage = S(a, "stage");
    if (!in(stage, STAGES)) throw PipelineError("stage must be one of " + join(STAGES, ", "));
    const long long today = today_from(opt(a, "today"));
    const Val old = dget(role, "stage");
    role.d->set("stage", str_v(stage));
    if (in(stage, NEEDS_APPLIED) && !truthy(dget(role, "applied"))) role.d->set("applied", str_v(iso(today)));
    if (in(stage, CLOSED)) { role.d->set("next_step", Val()); role.d->set("next_date", Val()); }
    if (!opt(a, "next_step").is_none()) role.d->set("next_step", truthy(opt(a, "next_step")) ? opt(a, "next_step") : Val());
    if (!opt(a, "next_date").is_none()) role.d->set("next_date", truthy(opt(a, "next_date")) ? opt(a, "next_date") : Val());
    add_note(role, today, pystr(old) + " -> " + stage);
    require_valid(data);
    hunter_followup::save(file, data);
    print("Moved " + S(a, "id") + ": " + pystr(old) + " -> " + stage);
    return 0;
}
inline int cmd_add_contact(const Val& a) {
    const std::string file = S(a, "file");
    Val data = load(file, false);
    const std::string name = S(a, "name"), company = S(a, "company");
    for (const Val& c : iter(L(data, "contacts"))) {
        if (lower(pystr(dget(c, "name", str_v("")))) == lower(name) && lower(pystr(dget(c, "company", str_v("")))) == lower(company) && !B(a, "force")) {
            print("Already in the pipeline as " + pystr(dget(c, "id")) + ". Use --force to add a second one.");
            return 1;
        }
    }
    PySet taken; for (const Val& c : iter(L(data, "contacts"))) taken.add(dget(c, "id"));
    const std::string cid = truthy(opt(a, "id")) ? S(a, "id") : unique_id(slug({name, company}), taken);
    if (taken.has(str_v(cid))) throw PipelineError("id " + str_repr(cid) + " is taken");
    Val conf = opt(a, "email_confidence");
    if (!conf.is_none()) conf = float_or_str(conf.s);
    Val contact = Val::dict();
    upd(contact, {{"id", str_v(cid)}, {"name", str_v(name)}, {"role", opt(a, "role")}, {"company", str_v(company)}, {"linkedin", opt(a, "linkedin")},
                  {"email", opt(a, "email")}, {"email_confidence", conf}, {"source", opt(a, "source")}, {"notes", orv(opt(a, "notes"), str_v(""))}});
    append(L(data, "contacts"), contact);
    require_valid(data);
    hunter_followup::save(file, data);
    print("Added contact " + cid + ": " + name + ", " + company);
    return 0;
}
inline int cmd_update_contact(const Val& a) {
    const std::string file = S(a, "file");
    Val data = load(file);
    Val c = find(L(data, "contacts"), S(a, "id"), "contact");
    std::vector<std::string> changed;
    for (const char* k : {"name", "role", "company", "linkedin", "email", "source"}) {
        const Val v = opt(a, k);
        if (!v.is_none()) { c.d->set(k, truthy(v) ? v : Val()); changed.push_back(k); }
    }
    if (!opt(a, "email_confidence").is_none()) {
        const Val v = float_or_str(S(a, "email_confidence"));
        c.d->set("email_confidence", peq(v, "") ? Val() : v);
        changed.push_back("email_confidence");
    }
    if (truthy(opt(a, "notes"))) { add_note(c, today_from(opt(a, "today")), S(a, "notes")); changed.push_back("notes"); }
    if (changed.empty()) { print("Nothing to change."); return 1; }
    require_valid(data);
    hunter_followup::save(file, data);
    print("Updated " + S(a, "id") + ": " + join(changed, ", "));
    return 0;
}
inline int cmd_summary(const Val& a) {
    const Val data = load(S(a, "file"));
    const Val s = summary(data, today_from(opt(a, "today")), get(a, "stale_days").i);
    DumpOpts jo; jo.indent = 2;
    print(B(a, "json") ? dumps(s, jo) : render_summary(s));
    return 0;
}
inline int cmd_stale(const Val& a) {
    const Val data = load(S(a, "file"));
    const long long days = get(a, "days").i;
    const auto rows = stale_roles(data, today_from(opt(a, "today")), days);
    if (rows.empty()) { print("Nothing stale: every active role moved in the last " + std::to_string(days) + " days or has a next step scheduled."); return 0; }
    for (const Stale& s : rows) {
        const std::string when = !s.last ? "never touched" : "last activity " + iso(*s.last) + " (" + std::to_string(*s.idle) + " days)";
        print(pystr(dget(s.r, "id")) + "  " + pystr(dget(s.r, "company")) + ", " + pystr(dget(s.r, "title")) + "  [" + pystr(dget(s.r, "stage")) + "]  " + when);
    }
    return 0;
}
inline int cmd_next(const Val& a) {
    const Val data = load(S(a, "file"));
    const long long today = today_from(opt(a, "today"));
    const long long days = get(a, "days").i;
    const auto items = upcoming(data, today, days);
    if (items.empty()) { print("Nothing scheduled in the next " + std::to_string(days) + " days."); return 0; }
    for (const Item& it : items) print(iso(it.d) + " " + (it.d < today ? "OVERDUE " : it.d == today ? "TODAY   " : "        ") + " " + it.what);
    return 0;
}
inline int cmd_show(const Val& a) {
    const Val data = load(S(a, "file"));
    const std::string id = S(a, "id");
    for (const auto& [coll, what] : std::vector<std::pair<const char*, std::string>>{{"roles", "role"}, {"contacts", "contact"}}) {
        for (const Val& it : iter(L(data, coll))) {
            if (!peq(dget(it, "id"), id.c_str())) continue;
            const char* key = what == "role" ? "role_id" : "contact_id";
            List threads; for (const Val& t : iter(L(data, "threads"))) if (peq(dget(t, key), id.c_str())) threads.push_back(t);
            Val o = Val::dict(); o.d->set(what, it); o.d->set("threads", Val::list(threads));
            DumpOpts jo; jo.indent = 2; jo.ensure_ascii = false; print(dumps(o, jo));
            return 0;
        }
    }
    throw PipelineError("no role or contact with id " + str_repr(id));
}
inline int cmd_export(const Val& a) {
    const Val data = load(S(a, "file"));
    const std::string what = S(a, "what");
    const std::string text = to_csv(data, what);
    if (truthy(opt(a, "out"))) {
        const std::string o = S(a, "out");
        const std::string d = dirname(o);
        if (!d.empty()) makedirs(d);
        write_text(o, text);
        print("Wrote " + o + " (" + std::to_string(pylen(L(data, what.c_str()))) + " " + what + ")");
    } else out(text);
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("pipeline", "The hunter pipeline: roles, contacts and threads in hunter/pipeline.json.");
    p.opt({"--file"}).def("hunter/pipeline.json").help("pipeline file (default hunter/pipeline.json)");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) { s.opt({"--file"}).suppress(); s.opt({"--today"}).help("YYYY-MM-DD (default: the computer's date)"); };
    { Parser& s = p.sub("init"); s.help_line = "create an empty pipeline"; common(s); }
    { Parser& s = p.sub("validate"); s.help_line = "check the file against the schema (exit 1 on errors)"; common(s); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("add-role"); s.help_line = "add a role"; common(s);
      s.opt({"--company"}).required(); s.opt({"--title"}).required(); s.opt({"--url"}); s.opt({"--stage"}).choices(STAGES);
      s.opt({"--applied"}); s.opt({"--next-step"}); s.opt({"--next-date"}); s.opt({"--notes"}); s.opt({"--id"}); s.opt({"--force"}).store_true(); }
    { Parser& s = p.sub("update-role"); s.help_line = "change fields on a role"; common(s); s.pos("id");
      for (const char* k : {"--company", "--title", "--url", "--applied", "--next-step", "--next-date", "--notes"}) s.opt({k});
      s.opt({"--clear-next"}).store_true(); }
    { Parser& s = p.sub("move"); s.help_line = "move a role to a stage"; common(s); s.pos("id"); s.pos("stage").choices(STAGES);
      s.opt({"--next-step"}); s.opt({"--next-date"}); }
    { Parser& s = p.sub("add-contact"); s.help_line = "add a contact"; common(s); s.opt({"--name"}).required(); s.opt({"--company"}).required();
      for (const char* k : {"--role", "--linkedin", "--email", "--email-confidence", "--source", "--notes", "--id"}) s.opt({k});
      s.opt({"--force"}).store_true(); }
    { Parser& s = p.sub("update-contact"); s.help_line = "change fields on a contact"; common(s); s.pos("id");
      for (const char* k : {"--name", "--role", "--company", "--linkedin", "--email", "--email-confidence", "--source", "--notes"}) s.opt({k}); }
    { Parser& s = p.sub("summary"); s.help_line = "counts by stage, next steps, stale roles"; common(s); s.opt({"--stale-days"}).integer().def(14); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("stale"); s.help_line = "active roles with no activity and nothing scheduled"; common(s); s.opt({"--days"}).integer().def(14); }
    { Parser& s = p.sub("next"); s.help_line = "next steps and follow-ups coming up"; common(s); s.opt({"--days"}).integer().def(7); }
    { Parser& s = p.sub("show"); s.help_line = "one role or contact with its threads"; common(s); s.pos("id"); }
    { Parser& s = p.sub("export"); s.help_line = "write roles, contacts or threads as CSV"; common(s);
      s.opt({"--what"}).choices({"roles", "contacts", "threads"}).def("roles"); s.opt({"--out"}); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "validate") return cmd_validate(a);
        if (cmd == "add-role") return cmd_add_role(a);
        if (cmd == "update-role") return cmd_update_role(a);
        if (cmd == "move") return cmd_move(a);
        if (cmd == "add-contact") return cmd_add_contact(a);
        if (cmd == "update-contact") return cmd_update_contact(a);
        if (cmd == "summary") return cmd_summary(a);
        if (cmd == "stale") return cmd_stale(a);
        if (cmd == "next") return cmd_next(a);
        if (cmd == "show") return cmd_show(a);
        if (cmd == "export") return cmd_export(a);
    } catch (const PipelineError& e) {
        eprint(std::string("pipeline: ") + e.what()); return 2;
    }
    return 2;
}
PACKTOOL("hunter-tracker/pipeline", main);
}  // namespace packtools::hunter_tracker
