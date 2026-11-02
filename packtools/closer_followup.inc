// follow-up/followup: keeps following up until a call is booked and stops the moment anyone says no. It drafts; it
// never sends. A C++ port of closer-skill/follow-up/followup.py (MIT, Jake Schincariol); templates.json ships with
// the skill (--skill-dir or $ASTRO_SKILL_DIR).
namespace packtools::closer_followup {
using namespace pyrt;
using namespace packtools::closer;

struct FollowupError : std::runtime_error { using std::runtime_error::runtime_error; };
inline constexpr int MAX_FOLLOWUPS = 3;
inline constexpr int GAPS[3] = {2, 2, 4};
inline constexpr int SILENCE_DAYS = 30;
inline const std::vector<std::string> BOOKED_STAGES = {"call-booked", "proposal", "contract-sent", "won"};

inline long long weekday_on(long long day) { while (weekday(day) >= 5) day += 1; return day; }   // a weekend date moves to Monday
inline long long day_of(const Val& stamp) { return date_fromiso(sv(head(stamp, 10))); }
inline long long day_of(const std::string& stamp) { return day_of(sv(stamp)); }

struct State { std::string contact_id; Val name, company; long long followups_sent = 0; std::optional<long long> followup; std::optional<std::string> due; std::string reason, state; };
// What is true for this person today: due, waiting, your-turn, booked, stopped or not-started.
inline State contact_state(const Val& data, const std::string& contact_id, long long today) {
    const Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) throw FollowupError("No contact " + contact_id);
    const Val c = *cp; const Val cid = sv(contact_id);
    const List acts = history_contact(data, cid);
    List deals; for (const Val& d : lstk(data, "deals")) if (repr(at(d, "contact_id")) == repr(cid)) deals.push_back(d);
    State st; st.contact_id = contact_id; st.name = at(c, "name"); st.company = g(c, "company");
    const auto ret = [&](const std::string& state, const std::string& reason) { State s = st; s.state = state; s.reason = reason; return s; };
    if (truthy(g(c, "do_not_contact")) || suppressed(data, g(c, "email")) || str_of(orv(g(c, "notes"), sv(""))).find("DO NOT CONTACT") != std::string::npos)
        return ret("stopped", "do not contact");
    for (const Val& a : acts) if (is(g(a, "direction"), "in") && says_no(g(a, "text", sv("")))) return ret("stopped", "they said no on " + head(at(a, "date"), 10));
    bool lost = false, open = false, won = false, booked = false;
    for (const Val& d : deals) { const Val s = at(d, "stage"); lost |= is(s, "lost"); open |= in(s, OPEN); won |= is(s, "won"); booked |= in(s, BOOKED_STAGES); }
    if (lost && !open) return ret("stopped", "deal lost: /win-back decides if and when to try again");
    if (won) return ret("booked", "customer: deal won, nothing to chase");
    for (const Val& a : acts) booked |= is(at(a, "type"), "meeting");
    if (booked) return ret("booked", "call booked: no more follow-ups");
    List openings; for (const Val& a : acts) if (is(at(a, "type"), "opening") && is(g(a, "direction"), "out")) openings.push_back(a);
    if (openings.empty()) return ret("not-started", "no opening email yet (/prospector sends it)");
    const long long opened = day_of(at(openings[0], "date"));
    const std::string first = str_of(at(openings[0], "date"));
    List after, last_in, last_out, sent;
    for (const Val& a : acts) if (str_of(at(a, "date")) >= first) after.push_back(a);
    for (const Val& a : after) { if (is(g(a, "direction"), "in")) last_in.push_back(a); if (is(g(a, "direction"), "out")) last_out.push_back(a); }
    if (!last_in.empty() && (last_out.empty() || str_of(at(last_in.back(), "date")) > str_of(at(last_out.back(), "date"))))
        return ret("your-turn", "they replied " + head(at(last_in.back(), "date"), 10) + ": " + head(g(last_in.back(), "text", sv("")), 80));
    for (const Val& a : after) if (is(at(a, "type"), "followup") && is(g(a, "direction"), "out")) sent.push_back(a);
    st.followups_sent = static_cast<long long>(sent.size());
    if (sent.size() >= static_cast<std::size_t>(MAX_FOLLOWUPS)) return ret("stopped", std::to_string(MAX_FOLLOWUPS) + " follow-ups sent, no reply: that is the limit");
    const long long base = sent.empty() ? opened : day_of(at(sent.back(), "date"));
    const long long due = weekday_on(base + GAPS[sent.size()]);
    if (due - opened > SILENCE_DAYS || today - opened > SILENCE_DAYS) return ret("stopped", std::to_string(SILENCE_DAYS) + " days since the opening with no reply");
    st.followup = static_cast<long long>(sent.size()) + 1; st.due = iso(due);
    const std::string n = std::to_string(sent.size() + 1) + " of " + std::to_string(MAX_FOLLOWUPS);
    if (today >= due) return ret("due", "follow-up " + n);
    return ret("waiting", "follow-up " + n + " is due " + iso(due));
}

inline std::vector<State> due_report(const Val& data, long long today) {
    static const std::map<std::string, int> order = {{"due", 0}, {"your-turn", 1}, {"waiting", 2}, {"booked", 3}, {"not-started", 4}, {"stopped", 5}};
    std::vector<State> people;
    for (const Val& c : lstk(data, "contacts")) people.push_back(contact_state(data, str_of(at(c, "id")), today));
    std::stable_sort(people.begin(), people.end(), [](const State& a, const State& b) {
        const int x = order.at(a.state), y = order.at(b.state); if (x != y) return x < y; return str_of(a.name) < str_of(b.name); });
    return people;
}

inline Val load_templates() { return loads(read_text(packtools::skill_file("templates.json"))); }

inline Val opening_subject(const Val& data, const std::string& contact_id) {
    for (const Val& a : history_contact(data, sv(contact_id))) if (is(at(a, "type"), "opening") && truthy(g(a, "subject"))) return at(a, "subject");
    return Val();
}

struct Draft { Val to; std::string subject, body, tmpl; std::vector<std::string> missing; };
inline Draft draft(const Val& data, const Val& templates, const std::string& contact_id, long long today, Val tmpl, const Val& hook) {
    const State st = contact_state(data, contact_id, today);
    if (st.state == "stopped") throw FollowupError("No draft: " + ps(st.name) + " is stopped (" + st.reason + ").");
    if (st.state == "booked") throw FollowupError("No draft: " + ps(st.name) + " already booked a call.");
    if (tmpl.is_none()) {
        if (st.state == "your-turn") tmpl = sv("confirm-time");
        else if (st.followup) tmpl = sv("followup-" + std::to_string(*st.followup));
        else throw FollowupError("Nothing to follow up with " + ps(st.name) + " yet (" + st.reason + ").");
    }
    if (!has(templates, tmpl.s)) { std::vector<std::string> ks; for (const auto& kv : templates.d->items) ks.push_back(kv.first); throw FollowupError("No template " + tmpl.s + ". Try: " + join(ks, ", ")); }
    const Val c = *find(at(data, "contacts"), contact_id);
    const Val me = g(data, "me", Val::dict());
    std::map<std::string, Val> fields;
    { const auto w = split_ws(str_of(at(c, "name"))); if (w.empty()) throw PyErr("IndexError", "list index out of range"); fields["first"] = sv(w[0]); }
    fields["company"] = orv(g(c, "company"), sv("")); fields["hook"] = hook;
    if (truthy(g(me, "name"))) { const auto w = split_ws(str_of(orv(g(me, "name"), sv("")))); if (w.empty()) throw PyErr("IndexError", "list index out of range"); fields["me"] = sv(w[0]); }
    fields["my_company"] = g(me, "company");
    std::set<std::string> missing;
    const auto fill = [&](const Val& text) {
        return re::compile(R"(\{(\w+)\})").sub_fn([&](const re::Match& m) {
            const auto it = fields.find(m.group(1)); const Val v = it == fields.end() ? Val() : it->second;
            if (!truthy(v)) { missing.insert(m.group(1)); return "[fill: " + m.group(1) + "]"; }
            return str_of(v);
        }, str_of(text));
    };
    const Val t = at(templates, tmpl.s);
    const Val subj = opening_subject(data, contact_id);
    Draft d; d.tmpl = tmpl.s; d.to = g(c, "email");
    d.subject = truthy(subj) ? "Re: " + str_of(subj) : fill(at(t, "subject"));
    d.body = fill(at(t, "body"));
    d.missing.assign(missing.begin(), missing.end());
    return d;
}

inline std::pair<bool, std::string> record(Val& data, const std::string& contact_id, const std::string& text, const Val& approval, const std::string& when, const Val& subject) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + "."};
    const long long today = day_of(when);
    const State st = contact_state(data, contact_id, today);
    if (st.state != "due" && st.state != "your-turn") return {false, "Not recorded: a stop rule applies (" + st.state + ": " + st.reason + ")."};
    if (text.find("[fill:") != std::string::npos) return {false, "Not recorded: the message still has a [fill: ...] in it."};
    Val deal_id; for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id) && in(at(d, "stage"), OPEN)) { deal_id = at(d, "id"); break; }
    const std::string kind = st.state == "due" ? "followup" : "reply";
    const Val os = opening_subject(data, contact_id);
    const Val subj = truthy(subject) ? subject : sv(strip("Re: " + (truthy(os) ? str_of(os) : std::string())));
    add_activity(data, {{"date", sv(when)}, {"contact_id", sv(contact_id)}, {"deal_id", deal_id}, {"channel", sv("email")}, {"type", sv(kind)},
                        {"direction", sv("out")}, {"subject", subj}, {"text", sv(text)}, {"approval", approval}});
    if (kind == "reply") return {true, "Logged your reply to " + ps(st.name) + "."};
    const State nxt = contact_state(data, contact_id, today);
    const std::string n = std::to_string(*st.followup) + " of " + std::to_string(MAX_FOLLOWUPS);
    if (nxt.state == "stopped") return {true, "Follow-up " + n + " logged. That was the last one."};
    return {true, "Follow-up " + n + " logged. Next is due " + (nxt.due ? *nxt.due : std::string("None")) + "."};
}

inline const char* TIME_PAT = R"(\b(mon|tue|wed|thu|fri|sat|sun)[a-z]*\b.*?\b(at\s+)?\d{1,2}(:\d{2})?\s*(am|pm)?\b|\b\d{1,2}(:\d{2})?\s*(am|pm)\b.*\bworks\b|\bworks\b)";

inline std::string mark_stop(Val& data, const std::string& contact_id, long long today, const Val& said) {
    Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) throw FollowupError("No contact " + contact_id);
    Val c = *cp;
    c.d->set("do_not_contact", Val::boolean(true));
    const std::string note = "DO NOT CONTACT since " + iso(today) + (truthy(said) ? ": " + head(said, 80) : "");
    c.d->set("notes", sv(strip(str_of(orv(g(c, "notes"), sv(""))) + " " + note)));
    if (truthy(g(c, "email")) && !suppressed(data, at(c, "email"))) lstk(data, "suppression").push_back(sv(lower(str_of(at(c, "email")))));
    std::vector<std::string> ids;
    for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id) && in(at(d, "stage"), OPEN)) ids.push_back(str_of(at(d, "id")));
    for (const auto& id : ids) move(data, id, "lost", today, sv("unsubscribed"));
    return ps(at(c, "name")) + " is do not contact, forever.";
}

inline std::string log_reply(Val& data, const std::string& contact_id, const std::string& text, const std::string& when) {
    const Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) throw FollowupError("No contact " + contact_id);
    const Val c = *cp;
    List deals; for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id) && in(at(d, "stage"), OPEN)) deals.push_back(d);
    add_activity(data, {{"date", sv(when)}, {"contact_id", sv(contact_id)}, {"deal_id", deals.empty() ? Val() : at(deals[0], "id")}, {"channel", sv("email")},
                        {"type", sv("reply")}, {"direction", sv("in")}, {"subject", Val()}, {"text", sv(text)}});
    if (says_unsub(text)) { mark_stop(data, contact_id, day_of(when), sv(text)); return ps(at(c, "name")) + " asked to be removed. Stopped forever and added to suppression."; }
    if (says_no(text)) {
        for (const Val& d : deals) move(data, str_of(at(d, "id")), "lost", day_of(when), sv("said no: " + head(text, 80)));
        return ps(at(c, "name")) + " said no. Every follow-up to them is stopped.";
    }
    for (const Val& d : deals)
        if (in(at(d, "stage"), {"lead", "contacted"})) { d.d->set("stage", sv("replied")); d.d->set("next_step", sv("Answer " + split_ws(str_of(at(c, "name")))[0])); }
    if (re::compile(TIME_PAT, re::I).search(text)) return ps(at(c, "name")) + " replied with a time. Your turn: confirm it, then /scheduler books it.";
    return ps(at(c, "name")) + " replied. Your turn: answer them (template confirm-time, or write it).";
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("followup.py", "Follow up until a call is booked.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("due"); s.opt({"--today"}); }
    { Parser& s = p.sub("draft"); s.opt({"--contact"}).required(); s.opt({"--template"}); s.opt({"--hook"}); s.opt({"--today"}); }
    { Parser& s = p.sub("record"); s.opt({"--contact"}).required(); s.opt({"--text"}); s.opt({"--text-file"}); s.opt({"--subject"});
      s.opt({"--approval"}).required(); s.opt({"--at"}).help("YYYY-MM-DDTHH:MM it went out (default now)"); s.opt({"--today"}); }
    { Parser& s = p.sub("reply"); s.opt({"--contact"}).required(); s.opt({"--text"}).required(); s.opt({"--at"}); }
    { Parser& s = p.sub("stop"); s.opt({"--contact"}).required(); s.opt({"--said"}); s.opt({"--today"}); }
    p.sub("templates");
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        if (cmd == "templates") {
            const Val t = load_templates();
            for (const auto& [k, v] : t.d->items) print(ljust(k, 14) + " " + ps(at(v, "use")));
            return 0;
        }
        Val data = load(file);
        const std::string now = now_iso().substr(0, 16);
        if (cmd == "due") {
            const long long today = today_arg(get(a, "today"));
            std::map<std::string, std::vector<State>> groups;
            for (const State& st : due_report(data, today)) groups[st.state].push_back(st);
            static const std::vector<std::pair<std::string, std::string>> titles = {{"due", "DUE NOW"}, {"your-turn", "YOUR TURN (they replied)"}, {"waiting", "WAITING"},
                {"booked", "BOOKED"}, {"not-started", "NOT STARTED"}, {"stopped", "STOPPED"}};
            for (const auto& [key, title] : titles) {
                if (!groups.count(key)) continue;
                print(title);
                for (const State& st : groups[key]) print("  " + ps(st.name) + " (" + ps(st.company) + "): " + st.reason);
                print("");
            }
        } else if (cmd == "draft") {
            const Val templates = load_templates();
            const Draft d = draft(data, templates, S(a, "contact"), today_arg(get(a, "today")), get(a, "template"), get(a, "hook"));
            print("To: " + ps(d.to) + "\nSubject: " + d.subject + "\n\n" + d.body);
            if (!d.missing.empty()) print("\nFill before it can go: " + join(d.missing, ", "));
        } else if (cmd == "record") {
            Val text = get(a, "text");
            if (truthy(get(a, "text_file"))) text = sv(strip(read_text(S(a, "text_file"))));
            if (!truthy(text)) throw FollowupError("Give --text or --text-file: exactly what went out.");
            const std::string when = truthy(get(a, "at")) ? S(a, "at") : truthy(get(a, "today")) ? S(a, "today") + "T09:00" : now;
            const auto [ok, msg] = record(data, S(a, "contact"), text.s, get(a, "approval"), when, get(a, "subject"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        } else if (cmd == "reply") {
            const std::string msg = log_reply(data, S(a, "contact"), S(a, "text"), truthy(get(a, "at")) ? S(a, "at") : now);
            save(data, file);
            print(msg);
        } else if (cmd == "stop") {
            const std::string msg = mark_stop(data, S(a, "contact"), today_arg(get(a, "today")), get(a, "said"));
            save(data, file);
            print(msg);
        }
    } catch (const FollowupError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("follow-up/followup", main);
}  // namespace packtools::closer_followup
