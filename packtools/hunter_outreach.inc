// hunter-connector/outreach - outreach threads in hunter/pipeline.json with the consent and stop rules: the assistant
// drafts, the user says yes to that exact message and sends it from their own account, then this records it. It never
// sends anything. Stop at the first no; at most 2 follow-ups (3 days, then 7); nothing after 21 days of silence; a stop
// request is honoured forever. LinkedIn invitation notes are at most 200 characters.
// C++ port of hunter-skill/skills/hunter-connector/outreach.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::hunter_outreach {
using namespace pyrt;
using trader_screener::eq;
struct OutreachError : std::runtime_error { using std::runtime_error::runtime_error; };

constexpr int FOLLOWUP_GAPS[2] = {3, 7};
constexpr int MAX_FOLLOWUPS = 2, SILENCE_DAYS = 21, INVITE_NOTE_MAX = 200, EMAIL_WORDS_MAX = 150;
inline const std::vector<std::string> CHANNELS = {"email", "linkedin"};
using std::regex_constants::icase;
inline const std::regex& NO_SIGNS() {
    static const std::regex r(R"(\b(not interested|no thanks|no thank you|not hiring|not a fit|please don'?t|do not contact|don'?t contact|unsubscribe|stop (?:emailing|messaging|contacting)|remove me|take me off|not the right person|not looking|pass on this|leave me alone|not (?:right )?now|not at this time|too busy|no bandwidth|can'?t help|no (?:open )?(?:roles|openings|headcount))\b)", std::regex::ECMAScript | icase);
    return r;
}
inline const std::regex& STOP_SIGNS() {
    static const std::regex r(R"(\b(unsubscribe|stop (?:emailing|messaging|contacting)|do not contact|don'?t contact|remove me|take me off|leave me alone)\b)", std::regex::ECMAScript | icase);
    return r;
}
inline const std::regex& PERSONAL() {
    static const std::regex r(R"(\b(wife|husband|girlfriend|boyfriend|kids?|children|son|daughter|married|wedding|pregnan\w*|divorce\w*|dating|home address|lives? (?:in|at|on)|church|mosque|synagogue|religio\w*|diagnos\w*|illness|birthday|vacation|marathon|gym|workout|instagram|facebook|tiktok|personal account)\b)", std::regex::ECMAScript | icase);
    return r;
}
inline const std::regex YES_START(R"(^(yes|yep|yeah|yup|y|ok|okay|sure|approved?|go ahead|send it|ship it|looks good|lgtm)\b)");
inline const std::regex YES_WORDS(R"(\b(yes|yep|yeah|yup|approved?|send it|go ahead|ship it)\b)");
inline const std::regex NOT_YES(R"(\b(all|every|everyone|everything|both|but|except|change|edit|tweak|wait|hold|no|not|don'?t|later|tomorrow|maybe|after|before|instead|first)\b)");

// ---------------------------------------------------------------- dates (datetime.date as days since 1970-01-01)
inline long long mkdate(long long y, long long m, long long d) { return py_date(y, m, d); }
inline std::string iso(long long days) { return date_iso(days); }
inline long long day(const Val& value) {
    const std::string s = truthy(value) ? pystr(value) : "";
    static const std::regex D(R"(^(\d{4})-(\d{2})-(\d{2}))");
    std::smatch m;
    if (!std::regex_search(s, m, D)) throw OutreachError("dates are YYYY-MM-DD, got " + repr(value));
    return mkdate(std::stoll(m[1].str()), std::stoll(m[2].str()), std::stoll(m[3].str()));
}
inline long long dayS(const std::string& s) { return day(Val::str(s)); }

// ---------------------------------------------------------------- storage
inline void pop(Val& d, const std::string& k) {   // d.pop(k, None)
    if (d.t != Val::DICT) return;
    auto& it = d.d->items;
    it.erase(std::remove_if(it.begin(), it.end(), [&](const auto& kv) { return kv.first == k; }), it.end());
}
inline bool is(const Val& v, const std::string& s) { return v.t == Val::STR && v.s == s; }
inline Val load(const std::string& path) {
    Val data = Val::dict();
    if (is_file(path)) data = loads(read_text(path));
    for (const char* k : {"roles", "contacts", "threads"}) if (!has(data, k)) data.d->set(k, Val::list());
    return data;
}
inline void save(const std::string& path, const Val& data) {
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path + ".tmp", dumps(data, o) + "\n");
    os_replace(path + ".tmp", path);
}
inline std::string slug(const std::string& text) {
    std::string o; bool dash = false;
    for (char c : lower(text)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { if (dash && !o.empty()) o += '-'; dash = false; o += c; }
        else dash = true;
    }
    return o;
}
inline Val& L(const Val& pipe, const char* k) { return *const_cast<Val*>(pipe.d->get(k)); }
inline Val contact(const Val& pipe, const std::string& cid) {
    for (const Val& c : *L(pipe, "contacts").l) if (is(dget(c, "id"), cid)) return c;
    throw OutreachError("no contact " + cid + " (see `outreach.py show`)");
}
inline Val thread(const Val& pipe, const std::string& cid, const std::string& channel, bool create = false, const Val& role_id = Val()) {
    for (const Val& t : *L(pipe, "threads").l) {
        if (is(dget(t, "contact_id"), cid) && is(dget(t, "channel"), channel)) {
            if (truthy(role_id) && !truthy(dget(t, "role_id"))) t.d->set("role_id", role_id);
            return t;
        }
    }
    if (!create) throw OutreachError("no " + channel + " thread with " + cid + " yet");
    Val t = Val::dict();
    t.d->set("contact_id", Val::str(cid)); t.d->set("role_id", role_id); t.d->set("channel", Val::str(channel));
    t.d->set("messages", Val::list()); t.d->set("status", Val::str("drafted")); t.d->set("next_followup", Val());
    L(pipe, "threads").l->push_back(t);
    return t;
}
inline Val role(const Val& pipe, const Val& rid) {
    for (const Val& r : *L(pipe, "roles").l) if (eq(dget(r, "id"), rid)) return r;
    return Val();
}

// ---------------------------------------------------------------- rules
using Pair = std::pair<std::vector<std::string>, std::vector<std::string>>;   // (errors, warnings)
inline Pair research_problems(const std::string& line) {   // public professional information with a source
    Pair r;
    std::smatch m;
    if (std::regex_search(line, m, PERSONAL())) r.first.push_back("personal life (" + repr(Val::str(m[0].str())) + "): research uses public professional information only");
    static const std::regex SRC(R"(https?://|public profile|company site|team page|posting|talk|paper|post)", std::regex::ECMAScript | icase);
    if (!std::regex_search(line, SRC)) r.second.push_back("no source: add the public URL it came from");
    return r;
}
inline std::string collapse_ws(const std::string& s) {   // re.sub(r"\s+", " ", s)
    std::string o; bool sp = false;
    for (const auto& ch : cps(s)) { if (py_space(cp_of(ch))) { if (!sp) o += ' '; sp = true; } else { o += ch; sp = false; } }
    return o;
}
inline std::pair<bool, std::string> is_yes(const std::string& reply) {   // (true, "") only for an explicit yes to this one message
    std::string r = collapse_ws(lower(strip(reply)));
    { std::size_t e = r.size(); while (e > 0 && (r[e - 1] == '!' || r[e - 1] == '.')) --e; r = strip(r.substr(0, e)); }
    if (r.empty()) return {false, "no reply recorded"};
    if (std::regex_search(r, NOT_YES)) return {false, "the reply has a condition or covers more than this one message: ask again"};
    for (const char* x : {"sure", "ok", "okay", "fine", "y", "looks good", "lgtm"})
        if (r == x) return {false, repr(Val::str(reply)) + " is not an explicit yes to send: ask \"Send this exact message? yes or no\""};
    if (std::regex_search(r, YES_START) && std::regex_search(r, YES_WORDS)) return {true, ""};
    for (const char* x : {"send", "send it", "go ahead", "approved", "approve"}) if (r == x) return {true, ""};
    return {false, "not an explicit yes: ask again"};
}
inline std::string draft_hash(const std::string& channel, const std::string& subject, const std::string& text) {
    return sha1_hex(channel + "\n" + subject + "\n" + text).substr(0, 12);
}
inline std::vector<Val> msgs_of(const Val& t) { const Val m = dget(t, "messages"); return m.t == Val::LIST ? *m.l : std::vector<Val>{}; }
inline std::vector<Val> outbound(const Val& t) { std::vector<Val> o; for (const Val& m : msgs_of(t)) if (is(dget(m, "direction"), "out")) o.push_back(m); return o; }
inline Val last_inbound(const Val& t) { Val o; for (const Val& m : msgs_of(t)) if (is(dget(m, "direction"), "in")) o = m; return o; }
inline int followups_sent(const Val& t) {   // outbound messages since their last reply, not counting the one that opened that stretch
    const auto msgs = msgs_of(t);
    long idx = -1;
    for (std::size_t i = 0; i < msgs.size(); ++i) if (is(dget(msgs[i], "direction"), "in")) idx = static_cast<long>(i);
    int after = 0;
    for (std::size_t i = static_cast<std::size_t>(idx + 1); i < msgs.size(); ++i) if (is(dget(msgs[i], "direction"), "out")) ++after;
    return std::max(0, after - 1);
}
inline std::optional<long long> silence_start(const Val& t) {
    const Val li = last_inbound(t);
    if (truthy(li)) return day(get(li, "date"));
    const auto o = outbound(t);
    if (!o.empty()) return day(get(o.front(), "date"));
    return std::nullopt;
}
inline Val compute_next(const Val& t) {   // next follow-up date under the stop rules, or None
    const Val st = dget(t, "status");
    if (!is(st, "sent") && !is(st, "replied")) return Val();
    if (truthy(dget(t, "invite"))) return Val();   // a pending LinkedIn invitation gets no follow-up
    const auto msgs = msgs_of(t);
    if (msgs.empty() || !is(dget(msgs.back(), "direction"), "out")) return Val();   // they spoke last: we owe a reply
    const int n = followups_sent(t);
    if (n >= MAX_FOLLOWUPS) return Val();
    const long long nxt = day(get(msgs.back(), "date")) + FOLLOWUP_GAPS[std::min(n, 1)];
    const auto start = silence_start(t);
    if (start && nxt - *start > SILENCE_DAYS) return Val();
    return Val::str(iso(nxt));
}
inline std::string kind_of(const Val& t) {
    if (outbound(t).empty()) return "first";
    const auto msgs = msgs_of(t);
    return !msgs.empty() && is(dget(msgs.back(), "direction"), "in") ? "reply" : "followup";
}
inline long long word_count(const std::string& s) {   // len(re.findall(r"\S+", s))
    long long n = 0; bool in = false;
    for (const auto& ch : cps(s)) { const bool sp = py_space(cp_of(ch)); if (!sp && !in) ++n; in = !sp; }
    return n;
}
inline Pair lint(const std::string& channel, const std::string& kind, const std::string& text0, const Val& subject = Val(), bool connected = false) {
    Pair r; auto& errors = r.first; auto& warnings = r.second;
    const std::string text = strip(text0);
    if (text.empty()) { errors.push_back("empty message"); return r; }
    const long long len = static_cast<long long>(cplen(text));
    if (channel == "linkedin" && kind == "first" && !connected) {
        if (len > INVITE_NOTE_MAX)
            errors.push_back("a LinkedIn invitation note is at most " + std::to_string(INVITE_NOTE_MAX) + " characters (LinkedIn Help 46662); this one is " + std::to_string(len));
        warnings.push_back("without Premium LinkedIn allows a personal note on 3 invitations a month");
    }
    if (channel == "linkedin" && len > 1000) warnings.push_back("long for a LinkedIn message (" + std::to_string(len) + " characters): keep it to a few lines");
    if (channel == "email") {
        const std::string subj = subject.t == Val::STR ? subject.s : "";
        if (kind == "first" && strip(subj).empty()) errors.push_back("an email needs a subject");
        if (!subj.empty() && cplen(subj) > 60) warnings.push_back("subject is " + std::to_string(cplen(subj)) + " characters: under 60 reads better on a phone");
        const long long words = word_count(text);
        if (words > EMAIL_WORDS_MAX) warnings.push_back(std::to_string(words) + " words: a first note to a busy manager reads best under " + std::to_string(EMAIL_WORDS_MAX));
    }
    const bool invite = channel == "linkedin" && kind == "first" && !connected;
    if ((kind == "first" || kind == "followup") && !invite && text.find('?') == std::string::npos)
        warnings.push_back("no question: end with one small, specific ask (a 20 minute chat next week?)");
    static const std::regex PRESSURE(R"(\b(as per my last|per my last|did you (?:get|see) my|bumping this|why haven'?t you)\b)", std::regex::ECMAScript | icase);
    if (std::regex_search(text, PRESSURE)) warnings.push_back("sounds like pressure: a follow-up adds something new or offers an easy out");
    return r;
}

// ---------------------------------------------------------------- commands
inline std::pair<Val, std::vector<std::string>> add_contact(const Val& pipe, const std::string& name, const Val& role_title, const Val& company,
                                                             const Val& linkedin, const Val& email, const Val& email_confidence, const Val& source,
                                                             const Val& notes, const std::vector<std::string>& research) {
    const std::string cid = slug(name + " " + (truthy(company) ? pystr(company) : ""));
    for (const Val& c : *L(pipe, "contacts").l) if (is(dget(c, "id"), cid)) throw OutreachError("contact " + cid + " already exists");
    if (truthy(email) && email_confidence.is_none()) throw OutreachError("an email needs --email-confidence (from emails.py); 1.0 only if it is published");
    if (!email_confidence.is_none()) { const double x = to_float(email_confidence); if (!(0 <= x && x <= 1)) throw OutreachError("--email-confidence is between 0 and 1"); }
    std::vector<std::string> lines, warnings;
    for (const auto& r : research) {
        const auto [errs, warns] = research_problems(r);
        if (!errs.empty()) throw OutreachError(join(errs, "; ") + ": " + repr(Val::str(r)));
        for (const auto& w : warns) warnings.push_back(w + ": " + r);
        lines.push_back(r);
    }
    std::vector<std::string> parts; if (truthy(notes)) parts.push_back(pystr(notes));
    for (const auto& l : lines) parts.push_back(l);
    Val c = Val::dict();
    c.d->set("id", Val::str(cid)); c.d->set("name", Val::str(name)); c.d->set("role", role_title); c.d->set("company", company);
    c.d->set("linkedin", linkedin); c.d->set("email", email); c.d->set("email_confidence", email_confidence); c.d->set("source", source);
    c.d->set("notes", Val::str(join(parts, "\n")));
    L(pipe, "contacts").l->push_back(c);
    return {c, warnings};
}
inline Val stop_contact(const Val& pipe, const std::string& cid, const Val& reason, long long today) {
    Val c = contact(pipe, cid);
    c.d->set("do_not_contact", Val::boolean(true));
    const std::string line = iso(today) + ": Do not contact (" + (truthy(reason) ? pystr(reason) : "asked to stop") + ")";
    const Val n = dget(c, "notes");
    c.d->set("notes", Val::str(strip(rstrip(truthy(n) ? pystr(n) : "") + "\n" + line)));
    for (Val& t : *L(pipe, "threads").l) {
        if (!is(dget(t, "contact_id"), cid)) continue;
        if (!is(dget(t, "status"), "declined")) t.d->set("status", Val::str("closed"));
        t.d->set("next_followup", Val());
        pop(t, "draft"); pop(t, "approval");
    }
    return c;
}
inline std::pair<Val, std::vector<std::string>> make_draft(const Val& pipe, const std::string& cid, const std::string& channel, const Val& text,
                                                            const Val& subject = Val(), const Val& role_id = Val(), std::optional<long long> today = std::nullopt,
                                                            bool connected = false, bool early = false) {
    if (std::find(CHANNELS.begin(), CHANNELS.end(), channel) == CHANNELS.end()) throw OutreachError("channel is email or linkedin");
    const Val c = contact(pipe, cid);
    if (truthy(dget(c, "do_not_contact"))) throw OutreachError(pystr(get(c, "name")) + " asked not to be contacted: no messages, on any channel, ever");
    if (channel == "email" && !truthy(dget(c, "email"))) throw OutreachError("no email for " + pystr(get(c, "name")) + ": run emails.py and add it with its confidence");
    Val t = thread(pipe, cid, channel, true, role_id);
    const std::string st = pystr(get(t, "status"));
    if (st == "declined" || st == "closed") throw OutreachError("this thread is " + st + ": the stop rules end it here");
    const bool has_msgs = !msgs_of(t).empty();
    if (channel == "linkedin") {
        if (connected) t.d->set("invite", Val::boolean(false));
        else if (truthy(dget(t, "invite")) && has_msgs) throw OutreachError("the invitation is still pending: once they accept, draft with --connected");
        else if (!has_msgs) t.d->set("invite", Val::boolean(true));
    }
    const std::string kind = kind_of(t);
    if (kind == "followup") {
        const int n = followups_sent(t);
        if (n >= MAX_FOLLOWUPS) throw OutreachError("already " + std::to_string(n) + " follow-ups with no reply: close the thread");
        const auto start = silence_start(t);
        if (start && today && *today - *start > SILENCE_DAYS) throw OutreachError(std::to_string(*today - *start) + " days of silence: no more messages on this thread");
        const Val due = dget(t, "next_followup");
        if (truthy(due) && today && *today < day(due) && !early) throw OutreachError("follow-up " + std::to_string(n + 1) + " is not due until " + pystr(due));
    }
    const std::string txt = text.t == Val::STR ? text.s : "";
    const auto [errors, warnings] = lint(channel, kind, txt, subject, connected);
    if (!errors.empty()) throw OutreachError(join(errors, "; "));
    Val d = Val::dict();
    d.d->set("kind", Val::str(kind)); d.d->set("subject", subject); d.d->set("text", Val::str(strip(txt)));
    d.d->set("created", today ? Val::str(iso(*today)) : Val());
    d.d->set("hash", Val::str(draft_hash(channel, truthy(subject) ? pystr(subject) : "", strip(txt))));
    t.d->set("draft", d);
    pop(t, "approval");
    if (!has_msgs) t.d->set("status", Val::str("drafted"));
    return {t, warnings};
}
inline Val approve(const Val& pipe, const std::string& cid, const std::string& channel, const std::string& reply, long long today) {
    Val t = thread(pipe, cid, channel);
    const Val d = dget(t, "draft");
    if (!truthy(d)) throw OutreachError("nothing drafted on this thread");
    const auto [ok, why] = is_yes(reply);
    if (!ok) throw OutreachError("Not recorded: " + why);
    Val a = Val::dict();
    a.d->set("reply", Val::str(reply)); a.d->set("date", Val::str(iso(today))); a.d->set("hash", get(d, "hash"));
    t.d->set("approval", a);
    return t;
}
inline bool approved(const Val& t) {
    const Val d = dget(t, "draft"), a = dget(t, "approval");
    return truthy(a) && truthy(d) && eq(dget(a, "hash"), get(d, "hash"));
}
inline Val mark_sent(const Val& pipe, const std::string& cid, const std::string& channel, long long when) {
    Val t = thread(pipe, cid, channel);
    const Val d = dget(t, "draft");
    if (!truthy(d)) throw OutreachError("nothing drafted on this thread");
    if (!approved(t)) throw OutreachError("no yes recorded for this exact message: show it to the user and run approve first");
    Val m = Val::dict();
    m.d->set("date", Val::str(iso(when))); m.d->set("direction", Val::str("out")); m.d->set("text", get(d, "text"));
    get(t, "messages").l->push_back(m);
    if (is(get(t, "status"), "drafted")) t.d->set("status", Val::str("sent"));
    pop(t, "draft"); pop(t, "approval");
    t.d->set("next_followup", compute_next(t));
    return t;
}
struct ReplyOut { Val t; bool declined, stop; };
inline ReplyOut log_reply(const Val& pipe, const std::string& cid, const std::string& channel, const std::string& text, long long when, bool declined = false, bool stop = false) {
    Val t = thread(pipe, cid, channel);
    if (outbound(t).empty()) throw OutreachError("no message was sent on this thread");
    Val m = Val::dict();
    m.d->set("date", Val::str(iso(when))); m.d->set("direction", Val::str("in")); m.d->set("text", Val::str(text));
    get(t, "messages").l->push_back(m);
    t.d->set("next_followup", Val());
    stop = stop || std::regex_search(text, STOP_SIGNS());
    declined = declined || stop || std::regex_search(text, NO_SIGNS());
    if (stop) { stop_contact(pipe, cid, Val::str("asked to stop"), when); t.d->set("status", Val::str("declined")); }
    else if (declined) { t.d->set("status", Val::str("declined")); pop(t, "draft"); }
    else if (is(get(t, "status"), "drafted") || is(get(t, "status"), "sent")) t.d->set("status", Val::str("replied"));
    return {t, declined, stop};
}
inline Val book(const Val& pipe, const std::string& cid, const std::string& channel, const std::string& when, long long minutes, const Val& where,
                const std::string& reply, const Val& role_id = Val()) {
    Val t = thread(pipe, cid, channel);
    if (!truthy(last_inbound(t))) throw OutreachError("book only after they said yes: log their reply first");
    const std::string st = pystr(get(t, "status"));
    if (st == "declined" || st == "closed") throw OutreachError("this thread is " + st);
    const auto [ok, why] = is_yes(reply);
    if (!ok) throw OutreachError("Not booked: the calendar invite goes to them, so it needs the user's yes (" + why + ")");
    static const std::regex W(R"(^(\d{4}-\d{2}-\d{2})[ T](\d{1,2}):(\d{2})\n?$)");
    std::smatch m;
    if (!std::regex_search(when, m, W)) throw OutreachError("--when wants 'YYYY-MM-DD HH:MM' in the user's time zone");
    const Val c = contact(pipe, cid);
    t.d->set("status", Val::str("booked")); t.d->set("next_followup", Val());
    char hh[8]; std::snprintf(hh, sizeof hh, "%02lld", std::stoll(m[2].str()));
    Val b = Val::dict();
    b.d->set("when", Val::str(m[1].str() + " " + hh + ":" + m[3].str())); b.d->set("minutes", Val::integer(minutes));
    b.d->set("where", where); b.d->set("approved", Val::str(reply));
    t.d->set("booked", b);
    const Val rid = truthy(role_id) ? role_id : dget(t, "role_id");
    Val r = truthy(rid) ? role(pipe, rid) : Val();
    if (truthy(r)) {
        r.d->set("next_step", Val::str("Coffee chat with " + pystr(get(c, "name")) + " (" + (truthy(where) ? pystr(where) : "details in the invite") + ")"));
        r.d->set("next_date", Val::str(m[1].str()));
    }
    return t;
}
inline Val reopen(const Val& pipe, const std::string& cid, const std::string& channel) {   // undo a declined reading (never after a stop)
    const Val c = contact(pipe, cid);
    if (truthy(dget(c, "do_not_contact"))) throw OutreachError(pystr(get(c, "name")) + " asked not to be contacted: that is final");
    Val t = thread(pipe, cid, channel);
    if (!is(get(t, "status"), "declined")) throw OutreachError("only a declined thread can be reopened");
    t.d->set("status", Val::str("replied"));
    return t;
}
inline Val close_thread(const Val& pipe, const std::string& cid, const std::string& channel) {
    Val t = thread(pipe, cid, channel);
    t.d->set("status", Val::str("closed")); t.d->set("next_followup", Val());
    pop(t, "draft"); pop(t, "approval");
    return t;
}
inline Val due(const Val& pipe, long long today) {   // what needs the user today
    Val out = Val::dict();
    for (const char* k : {"approve", "send", "followup", "reply", "close", "chats", "close_keys"}) out.d->set(k, Val::list());
    const auto add = [&](const char* k, const std::string& s) { get(out, k).l->push_back(Val::str(s)); };
    for (const Val& t : *L(pipe, "threads").l) {
        Val c;
        for (const Val& x : *L(pipe, "contacts").l) if (eq(dget(x, "id"), dget(t, "contact_id"))) c = x;   // the last one with that id, like a dict
        if (c.is_none()) { c = Val::dict(); c.d->set("name", dget(t, "contact_id")); }
        const std::string who = pystr(dget(c, "name")) + (truthy(dget(c, "company")) ? " at " + pystr(get(c, "company")) : "") + " (" + pystr(dget(t, "channel")) + ")";
        const Val st = dget(t, "status");
        if (truthy(dget(c, "do_not_contact")) || is(st, "declined") || is(st, "closed")) continue;
        const Val d = dget(t, "draft");
        if (truthy(d) && !approved(t)) add("approve", who + ": " + pystr(get(d, "kind")) + " draft waiting for your yes");
        else if (truthy(d)) add("send", who + ": approved, send it from your own account, then run `sent`");
        if (is(st, "booked")) {
            const Val b0 = dget(t, "booked");
            const Val b = truthy(b0) ? b0 : Val::dict();
            const std::string w = has(b, "when") ? pystr(get(b, "when")) : "";
            if (cpslice(w, 0, 10) >= iso(today)) add("chats", pystr(dget(c, "name")) + ": coffee chat " + pystr(dget(b, "when")) + ", " + (truthy(dget(b, "where")) ? pystr(get(b, "where")) : ""));
            continue;
        }
        const auto msgs = msgs_of(t);
        if (!msgs.empty() && is(dget(msgs.back(), "direction"), "in") && is(st, "replied")) {
            add("reply", who + " replied on " + pystr(get(msgs.back(), "date")) + ": answer them"); continue;
        }
        if (msgs.empty() || truthy(d)) continue;
        const Val nxt = dget(t, "next_followup");
        const auto start = silence_start(t);
        const long long silent = start ? today - *start : 0;
        if (truthy(nxt) && day(nxt) <= today && silent <= SILENCE_DAYS) {
            add("followup", who + ": follow-up " + std::to_string(followups_sent(t) + 1) + " due " + pystr(nxt) + " (" + std::to_string(silent) + " days since " +
                                (truthy(last_inbound(t)) ? "their last reply" : "your first message") + ")");
        } else if (!truthy(nxt)) {
            const long long last_out = day(get(msgs.back(), "date"));
            const auto key = [&] { Val k = Val::list(); k.l->push_back(get(t, "contact_id")); k.l->push_back(get(t, "channel")); get(out, "close_keys").l->push_back(k); };
            if (silent > SILENCE_DAYS) { add("close", who + ": " + std::to_string(silent) + " days of silence, close it"); key(); }
            else if (followups_sent(t) >= MAX_FOLLOWUPS && today - last_out >= FOLLOWUP_GAPS[1]) { add("close", who + ": two follow-ups and no reply, close it"); key(); }
        }
    }
    return out;
}
inline std::string render_due(const Val& d, long long today) {
    const std::vector<std::pair<const char*, const char*>> titles = {{"approve", "Waiting for your yes"}, {"send", "Approved, not sent yet"}, {"followup", "Follow-ups due"},
                                                                     {"reply", "They replied"}, {"chats", "Coffee chats"}, {"close", "Close (stop rules)"}};
    std::vector<std::string> lines = {"Outreach on " + iso(today) + ":"};
    for (const auto& [key, title] : titles) {
        const Val l = get(d, key);
        if (l.size()) { lines.push_back(std::string(title) + ":"); for (const Val& x : *l.l) lines.push_back("  - " + pystr(x)); }
    }
    if (lines.size() == 1) lines.push_back("Nothing due.");
    return join(lines, "\n") + "\n";
}
inline std::string show(const Val& pipe, const std::string& cid = "") {
    std::vector<std::string> lines;
    for (const Val& c : *L(pipe, "contacts").l) {
        if (!cid.empty() && !is(get(c, "id"), cid)) continue;
        const std::string flag = truthy(dget(c, "do_not_contact")) ? "  DO NOT CONTACT" : "";
        const Val conf = dget(c, "email_confidence");
        std::string email = "no email";
        if (truthy(dget(c, "email"))) {
            const bool pub = !conf.is_none() && (conf.t == Val::INT || conf.t == Val::FLOAT || conf.t == Val::BOOL) && to_float(conf) == 1;
            if (!pub && (conf.is_none() || conf.t == Val::STR)) throw PyErr("TypeError", std::string("must be real number, not ") + (conf.is_none() ? "NoneType" : "str"));
            email = pystr(get(c, "email")) + " (" + (pub ? std::string("published") : "guessed, " + fmt_f(to_float(conf), 2)) + ")";
        }
        lines.push_back(pystr(get(c, "id")) + "  " + pystr(get(c, "name")) + ", " + (truthy(dget(c, "role")) ? pystr(get(c, "role")) : "?") + " at " +
                        (truthy(dget(c, "company")) ? pystr(get(c, "company")) : "?") + "  " + email + flag);
        for (const Val& t : *L(pipe, "threads").l) {
            if (!eq(dget(t, "contact_id"), get(c, "id"))) continue;
            lines.push_back("  " + ljust(pystr(get(t, "channel")), 8) + " " + ljust(pystr(get(t, "status")), 8) + " " + std::to_string(msgs_of(t).size()) +
                            " messages  next follow-up: " + (truthy(dget(t, "next_followup")) ? pystr(get(t, "next_followup")) : "none") +
                            (truthy(dget(t, "draft")) ? "  (draft waiting)" : ""));
        }
    }
    if (lines.empty()) lines.push_back("No contacts yet.");
    return join(lines, "\n") + "\n";
}

// ---------------------------------------------------------------- CLI
inline long long today_of(const Val& v) { return truthy(v) ? day(v) : dayS(today_iso()); }
inline int main(const std::vector<std::string>& argv) {
    Parser p("outreach", "Outreach threads with consent and stop rules. Never sends anything.");
    p.opt({"--pipeline"}).def("hunter/pipeline.json");
    p.subcommands("cmd", false);
    { Parser& c = p.sub("contact"); c.help_line = "add a contact or stop contacting one";
      c.subcommands("action", false);
      Parser& ca = c.sub("add");
      ca.opt({"--name"}).required(); ca.opt({"--role"}); ca.opt({"--company"}); ca.opt({"--linkedin"}); ca.opt({"--email"});
      ca.opt({"--email-confidence"}).number();
      ca.opt({"--source"}).help("where you found them: the posting, a team page, a public profile");
      ca.opt({"--notes"});
      ca.opt({"--research"}).append().def(Val::list()).help("one public professional fact + its URL");
      Parser& cs = c.sub("stop");
      cs.opt({"--contact"}).required(); cs.opt({"--reason"}); cs.opt({"--today"}); }
    { Parser& d = p.sub("draft"); d.help_line = "save a draft for the user to approve";
      d.opt({"--contact"}).required(); d.opt({"--channel"}).required().choices(CHANNELS);
      d.opt({"--role"}).help("the role id in the pipeline"); d.opt({"--subject"}); d.opt({"--text"}); d.opt({"--file"});
      d.opt({"--connected"}).store_true().help("already connected on LinkedIn (a message, not an invite)");
      d.opt({"--early"}).store_true().help("the user wants this follow-up before it is due");
      d.opt({"--today"}); }
    for (const char* name : {"approve", "sent", "reply", "book", "close", "reopen"}) {
        Parser& s = p.sub(name);
        const std::string n = name;
        s.opt({"--contact"}).required(); s.opt({"--channel"}).def("email").choices(CHANNELS);
        if (n == "approve") { s.opt({"--reply"}).required().help("the user's reply, word for word"); s.opt({"--today"}); }
        if (n == "sent") s.opt({"--date"}).help("YYYY-MM-DD it went out (default today)");
        if (n == "reply") {
            s.opt({"--text"}).required(); s.opt({"--date"});
            s.opt({"--declined"}).store_true().help("any sign of disinterest");
            s.opt({"--stop"}).store_true().help("they asked not to be contacted");
        }
        if (n == "book") {
            s.opt({"--when"}).required().help("'YYYY-MM-DD HH:MM'"); s.opt({"--minutes"}).integer().def(30); s.opt({"--where"}); s.opt({"--role"});
            s.opt({"--reply"}).required().help("the user's yes to sending the invite, word for word");
        }
        if (n == "close") s.opt({"--reason"});
    }
    { Parser& du = p.sub("due"); du.help_line = "what needs doing today";
      du.opt({"--today"}); du.opt({"--close"}).store_true().help("close the threads the stop rules end"); du.opt({"--json"}).store_true(); }
    { Parser& sh = p.sub("show"); sh.opt({"--contact"}); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty() || (cmd == "contact" && S(a, "action").empty())) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    const std::string path = S(a, "pipeline");
    Val pipe = load(path);
    try {
        const std::string action = S(a, "action");
        if (cmd == "contact" && action == "add") {
            std::vector<std::string> research; for (const Val& r : *get(a, "research").l) research.push_back(pystr(r));
            const auto [c, warns] = add_contact(pipe, S(a, "name"), get(a, "role"), get(a, "company"), get(a, "linkedin"), get(a, "email"),
                                                get(a, "email_confidence"), get(a, "source"), get(a, "notes"), research);
            save(path, pipe);
            print("added contact " + pystr(get(c, "id")));
            for (const auto& w : warns) print("warning: " + w);
            return 0;
        }
        if (cmd == "contact" && action == "stop") {
            const Val c = stop_contact(pipe, S(a, "contact"), get(a, "reason"), today_of(get(a, "today")));
            save(path, pipe);
            print(pystr(get(c, "name")) + " is marked do not contact, forever. Open threads closed.");
            return 0;
        }
        if (cmd == "draft") {
            Val text = get(a, "text");
            if (truthy(get(a, "file"))) text = Val::str(read_text(S(a, "file")));
            const auto [t, warns] = make_draft(pipe, S(a, "contact"), S(a, "channel"), text, get(a, "subject"), get(a, "role"), today_of(get(a, "today")),
                                               B(a, "connected"), B(a, "early"));
            save(path, pipe);
            const Val dr = get(t, "draft");
            print("Draft saved (" + S(a, "channel") + ", " + pystr(get(dr, "kind")) + "). Show it to the user exactly as below and ask: Send this exact message? yes or no");
            if (truthy(dget(dr, "subject"))) print("Subject: " + pystr(get(dr, "subject")));
            print(pystr(get(dr, "text")));
            for (const auto& w : warns) print("warning: " + w);
            return 0;
        }
        if (cmd == "approve") {
            approve(pipe, S(a, "contact"), S(a, "channel"), S(a, "reply"), today_of(get(a, "today")));
            save(path, pipe);
            print("Recorded your yes for this one message. Now send it from your own account (Gmail draft or LinkedIn in your browser), then run `sent`.");
            return 0;
        }
        if (cmd == "sent") {
            const Val t = mark_sent(pipe, S(a, "contact"), S(a, "channel"), today_of(get(a, "date")));
            save(path, pipe);
            if (truthy(dget(t, "invite"))) print("Logged as sent. A LinkedIn invitation gets no follow-up: if they accept, message them with draft --connected.");
            else print("Logged as sent. Next follow-up: " + (truthy(dget(t, "next_followup")) ? pystr(get(t, "next_followup")) : std::string("none (stop rules)")));
            return 0;
        }
        if (cmd == "reply") {
            const auto r = log_reply(pipe, S(a, "contact"), S(a, "channel"), S(a, "text"), today_of(get(a, "date")), B(a, "declined"), B(a, "stop"));
            save(path, pipe);
            if (r.stop) print("They asked to stop. Marked do not contact, forever. Thread closed.");
            else if (r.declined) print("That reads as a no. Thread marked declined: no more messages on it. If the user reads it differently (\"not now, but next month works\"), `reopen` undoes it.");
            else print("Reply logged. Thread is " + pystr(get(r.t, "status")) + ". Draft an answer for the user's yes.");
            return 0;
        }
        if (cmd == "book") {
            const Val t = book(pipe, S(a, "contact"), S(a, "channel"), S(a, "when"), get(a, "minutes").i, get(a, "where"), S(a, "reply"), get(a, "role"));
            save(path, pipe);
            print("Booked: " + pystr(get(get(t, "booked"), "when")) + ", " + std::to_string(get(a, "minutes").i) + " minutes, " +
                  (truthy(get(a, "where")) ? S(a, "where") : std::string("place in the invite")) + ". Create the invite in the user's calendar now (they said yes).");
            return 0;
        }
        if (cmd == "reopen") { reopen(pipe, S(a, "contact"), S(a, "channel")); save(path, pipe); print("Reopened as replied."); return 0; }
        if (cmd == "close") { close_thread(pipe, S(a, "contact"), S(a, "channel")); save(path, pipe); print("Closed."); return 0; }
        if (cmd == "due") {
            const long long today = today_of(get(a, "today"));
            const Val d = due(pipe, today);
            if (B(a, "close")) {
                for (const Val& k : *get(d, "close_keys").l) close_thread(pipe, pystr((*k.l)[0]), pystr((*k.l)[1]));
                save(path, pipe);
            }
            if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(d, o)); }
            else out(render_due(d, today));
            return 0;
        }
        if (cmd == "show") { out(show(pipe, S(a, "contact"))); return 0; }
    } catch (const OutreachError& e) {
        eprint(std::string("error: ") + e.what());
        return 1;
    }
    return 2;
}
PACKTOOL("hunter-connector/outreach", main);
}  // namespace packtools::hunter_outreach
