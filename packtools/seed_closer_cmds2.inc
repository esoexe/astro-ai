// seed-closer, part 4: log (what happened: sent, denied, a reply, a note), due (which follow-ups are due), followup
// (draft the next touch, in the intro's thread) and reply (answer a conversation the investor started).
namespace packtools::seed_closer {

inline void pop_key(Val& d, const std::string& k) {   // d.pop(k, None)
    auto& it = d.d->items;
    it.erase(std::remove_if(it.begin(), it.end(), [&](const std::pair<std::string, Val>& p) { return p.first == k; }), it.end());
}
inline Val iso_or_none(const std::optional<long long>& d) { return d ? sv(iso(*d)) : Val(); }
inline Val list_need(const Val& rec, const std::string& k) {   // rec[k] used with .append
    const Val v = need(rec, k);
    if (v.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'append'");
    return v;
}
inline std::string first_word(const Val& name) {   // name.split()[0]
    const std::vector<std::string> w = split_ws(seed_plan::str_of(name, "split"));
    if (w.empty()) throw PyErr("IndexError", "list index out of range");
    return w[0];
}

inline std::string record_reply(Val& rec, const std::string& cls, long long day, const Rules& rules, const std::string& note = "",
                                std::optional<long long> back = std::nullopt, const std::string& excerpt = "") {
    const std::string rid = pystr(need(rec, "id"));
    if (out_touches(rec).empty()) throw CloserError("nothing was sent to " + rid + " yet, so there is no reply to record");
    const Val st = pget(rec, "status");
    if (in_v(TERMINAL, st)) throw CloserError(rid + " is " + pystr(st) + ": that thread ended for good, nothing more is recorded or sent");
    const std::string label = LABELS.at(cls);
    if (cls == "ooo") {
        std::string voided;
        if (back) {
            set_key(rec, "hold_until", sv(iso(*back)));
            const Val d = pget(rec, "draft");
            if (truthy(d) && py_eq(need(d, "kind"), sv("followup"))) {
                voided = " The waiting follow-up (" + pystr(need(d, "file")) + ") is void; it is drafted again when it is due.";
                history(rec, day, "voided", pystr(need(d, "file")));
                set_key(rec, "draft", Val());
            }
        }
        add_label(rec, label);
        set_key(rec, "next_due", iso_or_none(next_due(rec, rules)));
        history(rec, day, "out of office", !note.empty() ? note : back ? "back " + iso(*back) : std::string("no return date"));
        const Val nd = need(rec, "next_due");
        return rid + ": out of office" + (back ? " until " + iso(*back) : std::string()) + ". Status stays " + pystr(st) +
               (truthy(nd) ? ", next follow-up " + pystr(nd) : std::string()) + "." + voided;
    }
    std::string nw = NEW_STATUS.at(cls);
    if (RANK.count(nw) && st.t == Val::STR && RANK.count(st.s) && RANK.at(st.s) >= RANK.at(nw)) nw = st.s;
    set_status(rec, nw);
    if (cls != "bounce") {
        Val t = obj({{"date", sv(iso(day))}, {"kind", sv("reply")}, {"from", sv("them")}, {"class", sv(cls)}});
        if (!excerpt.empty()) set_key(t, "excerpt", sv(cpslice(excerpt, 0, 240)));
        list_need(rec, "touches").l->push_back(t);
    }
    add_label(rec, label);
    std::string voided;
    const Val d = pget(rec, "draft");
    if (truthy(d) && (py_eq(need(d, "kind"), sv("followup")) || in(TERMINAL, nw))) {
        voided = " The waiting " + pystr(need(d, "kind")) + " draft (" + pystr(need(d, "file")) + ") is void: it will not be sent.";
        history(rec, day, "voided", pystr(need(d, "file")));
        set_key(rec, "draft", Val());
    }
    set_key(rec, "next_due", Val());
    pop_key(rec, "hold_until");
    history(rec, day, cls, note);
    const std::string end = nw == "passed" ? " The thread is closed: no more follow-ups." : nw == "bounced" ? " The thread is closed for good."
                          : nw == "optout" ? " Never emailed again." : " Follow-ups stop: this is a conversation now.";
    return rid + ": " + label + ", status " + nw + "." + end + voided;
}

inline int cmd_log(const Val& a) {
    const long long today = get_today(a);
    const long long day = truthy(pget(a, "date")) ? parse_day(pget(a, "date"), "--date") : today;
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    Val pipe = load_pipe(dir);
    const std::vector<std::string> ids = ids_of(pystr(pget(a, "id")));
    if (ids.empty()) throw CloserError("say who: --id maya-chen (or several, comma separated)");
    const std::string event = pystr(pget(a, "event"));
    const Val note = pget(a, "note");
    if (event == "note" && !truthy(note)) throw CloserError("--event note needs --note \"...\"");
    std::vector<std::string> out;
    for (const std::string& rid : ids) {
        Val rec = *find(pipe, rid);
        const Val st = pget(rec, "status");
        if (event == "note") {
            set_key(rec, "notes", truthy(pget(rec, "notes")) ? sv(pystr(need(rec, "notes")) + "; " + pystr(note)) : note);
            history(rec, day, "note", pystr(note));
            out.push_back(rid + ": note added.");
            continue;
        }
        if (in_v(TERMINAL, st)) throw CloserError(rid + " is " + pystr(st) + ": that thread ended for good, nothing more is recorded or sent");
        if (event == "sent") {
            const Val d = pget(rec, "draft");
            if (!truthy(d) || !py_eq(pget(d, "state"), sv("approved")))
                throw CloserError(rid + " has no approved email to mark sent: the yes comes first (batch, then approve)");
            if (day < parse_day(need(d, "approved_on"), "approved_on"))
                throw CloserError(rid + ": the send date " + iso(day) + " is before the yes (" + pystr(need(d, "approved_on")) + ")");
            list_need(rec, "touches").l->push_back(obj({{"n", need(d, "n")}, {"date", sv(iso(day))}, {"kind", need(d, "kind")}, {"from", sv("us")},
                                                        {"subject", need(d, "subject")}, {"message_id", need(d, "message_id")}, {"file", need(d, "file")}}));
            set_key(rec, "draft", Val());
            if (truthy(pget(a, "thread_id"))) set_key(rec, "thread_id", pget(a, "thread_id"));
            const bool intro = py_eq(need(d, "kind"), sv("intro"));
            if (intro) set_status(rec, "sent");
            set_key(rec, "next_due", iso_or_none(next_due(rec, rules)));
            history(rec, day, "sent", pystr(need(d, "kind")) + " " + seed_plan::fmt_d(need(d, "n")));
            const Val nd = need(rec, "next_due");
            out.push_back(rid + ": " + (py_eq(need(d, "kind"), sv("followup")) ? "follow-up " + itos(int_of(need(d, "n")) - 1) : pystr(need(d, "kind"))) +
                          " sent " + short_day(day) + "." + (truthy(nd) ? " Next follow-up due " + pystr(nd) + "." : std::string()));
        } else if (event == "denied") {
            Val d = pget(rec, "draft");
            if (!truthy(d) || !py_eq(pget(d, "state"), sv("approved"))) throw CloserError(rid + " has no approved email to put back");
            set_key(d, "state", sv("drafted"));
            for (const char* k : {"approved_on", "reply", "batch", "sha"}) pop_key(d, k);
            if (py_eq(need(d, "kind"), sv("intro"))) set_status(rec, "drafted");
            history(rec, day, "denied", pystr(need(d, "file")));
            out.push_back(rid + ": back to drafted. It needs a new yes before it goes anywhere.");
        } else {
            std::string cls; for (const auto& [k, v] : EVENT_CLASS) if (k == event) cls = v;
            out.push_back(record_reply(rec, cls, day, rules, str_or(note)));
        }
    }
    save_pipe(dir, pipe, today);
    for (const std::string& line : out) print(line);
    return 0;
}

// ---------------------------------------------------------------- due and follow-ups
inline int cmd_due(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    const Val pipe = load_pipe(dir);
    using Row = std::pair<Val, long long>;
    std::vector<Row> due, later, waiting;
    for (const Val& rec : investors_of(pipe)) {
        const auto nd = next_due(rec, rules);
        if (!nd) continue;
        if (truthy(pget(rec, "draft"))) waiting.emplace_back(rec, *nd);
        else if (*nd <= today) due.emplace_back(rec, *nd);
        else later.emplace_back(rec, *nd);
    }
    std::stable_sort(due.begin(), due.end(), [](const Row& x, const Row& y) {
        if (x.second != y.second) return x.second < y.second;
        const double fx = -fit_of(x.first), fy = -fit_of(y.first);
        if (fx != fy) return fx < fy;
        return pystr(need(x.first, "id")) < pystr(need(y.first, "id"));
    });
    std::stable_sort(later.begin(), later.end(), [](const Row& x, const Row& y) {
        if (x.second != y.second) return x.second < y.second;
        return pystr(need(x.first, "id")) < pystr(need(y.first, "id"));
    });
    const bool workday = is_workday(today, rules);
    const auto row = [&](const Row& x) {
        const std::vector<Val> cold = out_touches(x.first, &COLD);
        return obj({{"id", need(x.first, "id")}, {"name", need(x.first, "name")}, {"firm", need(x.first, "firm")}, {"due", sv(iso(x.second))},
                    {"followup", iv(static_cast<long long>(cold.size()))}, {"of", iv(rules.max_touches - 1)}, {"intro_sent", need(cold.at(0), "date")}});
    };
    if (truthy(pget(a, "json"))) {
        Val d = Val::list(), w = Val::list(), l = Val::list();
        for (const Row& x : due) d.l->push_back(row(x));
        for (const Row& x : waiting) w.l->push_back(row(x));
        for (const Row& x : later) l.l->push_back(row(x));
        print(dumps(obj({{"today", sv(iso(today))}, {"business_day", bv(workday)}, {"due", d}, {"drafted", w}, {"later", l}}), json_opts(true)));
        return !due.empty() && workday ? 0 : 1;
    }
    const std::string kind = rules.business ? "business days" : "calendar days";
    if (!workday) print(short_day(today) + " is not a business day: nothing goes out today. Next business day: " + short_day(next_workday(today, rules)) + ".");
    if (!due.empty()) {
        print("Follow-ups due " + short_day(today) + " (" + kind + ", at most " + itos(rules.max_touches) + " touches, never after a reply):");
        for (const Row& x : due) {
            const Val r = row(x);
            print("  " + ljust(pystr(need(x.first, "id")), 18) + " " + pystr(need(x.first, "name")) + ", " + pystr(need(x.first, "firm")) +
                  "  follow-up " + seed_plan::fmt_d(need(r, "followup")) + " of " + seed_plan::fmt_d(need(r, "of")) + ", intro sent " +
                  short_day(parse_day(need(r, "intro_sent"))) + (x.second == today ? std::string() : " (due since " + short_day(x.second) + ")"));
        }
        if (workday) print("Next: closer.py followup --due, then batch and the user's yes.");
    } else {
        print("No follow-ups due " + short_day(today) + ".");
    }
    for (const Row& x : waiting)
        print("  waiting: " + pystr(need(x.first, "id")) + " has a " + pystr(need(need(x.first, "draft"), "kind")) + " draft (" + pystr(need(need(x.first, "draft"), "state")) + ")");
    for (std::size_t i = 0; i < later.size() && i < 10; ++i)
        print("  later: " + short_day(later[i].second) + " " + pystr(need(later[i].first, "id")) + ", follow-up " + itos(static_cast<long long>(out_touches(later[i].first, &COLD).size())));
    return !due.empty() && workday ? 0 : 1;
}

inline std::string clean_extra(const Val& text, const std::string& what, long long limit) {
    const std::string s = ws_join(str_or(text));
    if (s.empty()) return "";
    if (re::search(PLACEHOLDER_RE, s, re::I)) throw CloserError(what + " still has a placeholder in it: " + str_repr(s));
    if (words(s) > limit) throw CloserError(what + " is " + itos(words(s)) + " words; keep it under " + itos(limit) + " (follow-ups are short)");
    return s;
}

inline int cmd_followup(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    const Sender snd = sender_of(cfg);
    const Val base = base_values(cfg, snd);
    const Template tpl = load_template(template_path(a));
    Val pipe = load_pipe(dir);
    const std::string extra = clean_extra(pget(a, "extra"), "--extra", 30);
    std::string ask = clean_extra(pget(a, "ask"), "--ask", 25);
    if (ask.empty()) ask = DEFAULT_ASK;
    if (!ends(ask, "?")) throw CloserError("--ask is a question: end it with a question mark");
    std::vector<Val> targets;
    if (truthy(pget(a, "due"))) {
        if (!is_workday(today, rules)) { print(short_day(today) + " is not a business day: no follow-ups today."); return 1; }
        for (const Val& r : investors_of(pipe)) {
            if (truthy(pget(r, "draft"))) continue;
            const auto nd = next_due(r, rules);
            if (nd && *nd <= today) targets.push_back(r);
        }
        std::stable_sort(targets.begin(), targets.end(), [](const Val& x, const Val& y) {
            const double fx = -fit_of(x), fy = -fit_of(y);
            if (fx != fy) return fx < fy;
            return pystr(need(x, "id")) < pystr(need(y, "id"));
        });
        if (targets.empty()) { print("No follow-ups due " + short_day(today) + "."); return 1; }
    } else if (truthy(pget(a, "id"))) {
        targets.push_back(*find(pipe, pystr(pget(a, "id"))));
    } else {
        throw CloserError("say who: --id maya-chen, or --due for everyone due today");
    }
    struct Made { Val rec; long long k; std::string subject, body; };
    std::vector<Made> made;
    std::vector<std::pair<std::string, std::string>> writes;
    for (Val rec : targets) {
        const std::string rid = pystr(need(rec, "id"));
        const Val st = pget(rec, "status");
        if (in_v(TERMINAL, st)) throw CloserError(rid + " is " + pystr(st) + ": that thread ended for good");
        if (in_v(CONVERSATION, st)) throw CloserError(rid + " replied: follow-ups stop after a reply. Answer in the thread instead (closer.py reply)");
        if (!py_eq(st, sv("sent"))) throw CloserError(rid + ": the intro has not gone out yet (status " + pystr(st) + ")");
        if (truthy(pget(rec, "draft")))
            throw CloserError(rid + " already has a " + pystr(need(need(rec, "draft"), "kind")) + " draft waiting (" + pystr(need(need(rec, "draft"), "state")) + ")");
        const auto nd = next_due(rec, rules);
        if (!nd) throw CloserError(rid + " has had all " + itos(rules.max_touches) + " touches: the thread ends here unless they reply");
        if (*nd > today) throw CloserError(rid + ": the next follow-up is not due until " + short_day(*nd));
        const std::vector<Val> cold = out_touches(rec, &COLD);
        const long long k = static_cast<long long>(cold.size());
        const Val intro = cold[0];
        Val values = copy_dict(base);
        set_key(values, "first_name", sv(first_word(need(rec, "name"))));
        set_key(values, "name", need(rec, "name"));
        set_key(values, "firm", need(rec, "firm"));
        set_key(values, "personal_line", pget(rec, "personal_line", sv("")));
        set_key(values, "intro_subject", need(intro, "subject"));
        set_key(values, "intro_day", sv(spoken_day(parse_day(need(intro, "date")))));
        set_key(values, "extra", sv(extra));
        set_key(values, "ask", sv(ask));
        const std::string sec = "followup " + itos(k);
        const auto t = tpl.find(sec);
        if (t == tpl.end()) throw PyErr("KeyError", str_repr(sec));
        const std::string subject = fill(t->second.subject, values, "template " + sec + " subject");
        const std::string body = fill(t->second.body, values, "template " + sec);
        if (body.find(OPTOUT) == std::string::npos || body.find(snd.name) == std::string::npos)
            throw CloserError("the template's " + sec + " lost {optout} or {signature}");
        const long long n_words = words(body.substr(0, body.find(OPTOUT)));
        if (n_words > 80) throw CloserError(rid + ": this follow-up is " + itos(n_words) + " words before the opt-out line; keep follow-ups under 80");
        const std::vector<Val> outs = out_touches(rec);
        const long long n = static_cast<long long>(outs.size()) + 1;
        const std::string mid = message_id(rid, n, subject, today, snd);
        const Val last_mid = pget(outs.back(), "message_id");
        const std::string data = make_eml(snd, pystr(need(rec, "name")), pystr(need(rec, "email")), subject, body, today, mid, truthy(last_mid) ? pystr(last_mid) : "");
        const std::string rel = "outreach/" + rid + "-" + itos(n) + ".eml";
        set_key(rec, "draft", obj({{"n", iv(n)}, {"kind", sv("followup")}, {"file", sv(rel)}, {"subject", sv(subject)}, {"message_id", sv(mid)},
                                   {"in_reply_to", last_mid}, {"state", sv("drafted")}, {"drafted_on", sv(iso(today))}}));
        history(rec, today, "drafted", "follow-up " + itos(k));
        writes.emplace_back(path_join(dir, rel), data);
        made.push_back({rec, k, subject, body});
    }
    for (const auto& [path, data] : writes) write_bytes(path, data);
    save_pipe(dir, pipe, today);
    for (const Made& m : made)
        print("Drafted follow-up " + itos(m.k) + " of " + itos(rules.max_touches - 1) + " for " + pystr(need(m.rec, "name")) + " (" + pystr(need(m.rec, "id")) + ") -> " +
              path_join(dir, pystr(need(need(m.rec, "draft"), "file"))));
    const Made& m = made[0];
    print("  Subject: " + m.subject);
    print("");
    print_body(m.body);
    const Val tid = pget(m.rec, "thread_id");
    print("Nothing is sent. Next: closer.py batch, then the user's yes. Send it in the intro's thread" + (truthy(tid) ? " (" + pystr(tid) + ")" : std::string()) + ".");
    return 0;
}

inline int cmd_reply(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Sender snd = sender_of(cfg);
    const Val base = base_values(cfg, snd);
    Val pipe = load_pipe(dir);
    const std::string id = pystr(pget(a, "id"));
    Val rec = *find(pipe, id);
    const Val st = pget(rec, "status");
    if (!in_v(CONVERSATION, st)) throw CloserError(id + " is " + pystr(st) + ": a reply answers a conversation they started (it is not a follow-up)");
    if (truthy(pget(rec, "draft")))
        throw CloserError(id + " already has a " + pystr(need(need(rec, "draft"), "kind")) + " draft waiting (" + pystr(need(need(rec, "draft"), "state")) + ")");
    std::string text = truthy(pget(a, "body_file")) ? read_text_utf8(pystr(pget(a, "body_file"))) : str_or(pget(a, "body"));
    text = tidy(replace_all(text, "\r\n", "\n"));
    if (text.empty()) throw CloserError("the reply is empty: give --body or --body-file");
    if (re::search(PLACEHOLDER_RE, text, re::I)) throw CloserError("the reply still has a placeholder in it");
    if (words(text) > 250) throw CloserError("the reply is " + itos(words(text)) + " words: keep it under 250 and offer a call for the rest");
    const std::string body = text + "\n\n" + need(base, "signature").s;
    const std::vector<Val> outs = out_touches(rec);
    const std::vector<std::string> intro_kind = {"intro"};
    const std::vector<Val> intros = out_touches(rec, &intro_kind);
    if (intros.empty()) throw PyErr("IndexError", "list index out of range");
    const Val isub = need(intros[0], "subject");
    const std::string subject = starts(lower(seed_plan::str_of(isub, "lower")), "re:") ? isub.s : "Re: " + isub.s;
    const long long n = static_cast<long long>(outs.size()) + 1;
    const std::string mid = message_id(pystr(need(rec, "id")), n, subject, today, snd);
    const Val last_mid = pget(outs.back(), "message_id");
    const std::string data = make_eml(snd, pystr(need(rec, "name")), pystr(need(rec, "email")), subject, body, today, mid, truthy(last_mid) ? pystr(last_mid) : "");
    const std::string rel = "outreach/" + pystr(need(rec, "id")) + "-" + itos(n) + ".eml";
    write_bytes(path_join(dir, rel), data);
    set_key(rec, "draft", obj({{"n", iv(n)}, {"kind", sv("reply")}, {"file", sv(rel)}, {"subject", sv(subject)}, {"message_id", sv(mid)},
                               {"in_reply_to", last_mid}, {"state", sv("drafted")}, {"drafted_on", sv(iso(today))}}));
    history(rec, today, "drafted", "reply");
    save_pipe(dir, pipe, today);
    print("Drafted a reply to " + pystr(need(rec, "name")) + " -> " + path_join(dir, rel));
    print_body(body);
    print("Nothing is sent. Next: closer.py batch, then the user's yes; it goes into the same thread.");
    return 0;
}
}  // namespace packtools::seed_closer
