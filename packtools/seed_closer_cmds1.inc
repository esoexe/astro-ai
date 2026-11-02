// seed-closer, part 3: draft (the intros), batch (what the founder is shown), approve (their explicit yes, recorded
// word for word) and outbox (what is approved and not sent yet).
namespace packtools::seed_closer {

inline std::string a_dir(const Val& a) { return pystr(pget(a, "dir")); }
inline long long get_today(const Val& a) { return truthy(pget(a, "today")) ? parse_day(pget(a, "today")) : today_days(); }
inline std::optional<std::string> a_str(const Val& a, const std::string& k) { const Val v = pget(a, k); if (v.is_none()) return std::nullopt; return pystr(v); }
inline std::optional<long long> a_int(const Val& a, const std::string& k) { const Val v = pget(a, k); if (v.is_none()) return std::nullopt; return v.i; }
inline long long int_of(const Val& v, const char* op = "-") {   // a JSON value used in integer arithmetic
    if (v.t == Val::INT && !v.big()) return v.i;
    if (v.t == Val::BOOL) return v.b ? 1 : 0;
    throw PyErr("TypeError", std::string("unsupported operand type(s) for ") + op + ": '" + type_name(v) + "' and 'int'");
}
inline Val draft_of(const Val& rec) { return vor(pget(rec, "draft"), Val::dict()); }   // rec.get("draft") or {}
inline void print_body(const std::string& body) { for (const std::string& line : split(body, "\n")) print(rstrip("  " + line)); }
inline std::vector<std::string> ids_of(const std::string& s) {   // [x.strip() for x in s.split(",") if x.strip()]
    std::vector<std::string> o; for (const std::string& x : split(s, ",")) if (!strip(x).empty()) o.push_back(strip(x));
    return o;
}

// ---------------------------------------------------------------- draft
inline int cmd_draft(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    const Sender snd = sender_of(cfg);
    const Val base = base_values(cfg, snd);
    const Template tpl = load_template(template_path(a));
    const std::vector<Val> rows = load_investors(dir);
    Val pipe = load_pipe(dir);
    (void)rules;
    std::map<std::string, Val> by_id; for (const Val& r : rows) by_id[id_of(r)] = r;
    std::vector<std::pair<std::string, std::string>> refused;
    std::vector<std::string> skipped;
    std::vector<Val> picked;
    const auto ids = a_str(a, "ids");
    if (ids && !ids->empty()) {
        for (const std::string& i : ids_of(*ids)) {
            if (!by_id.count(i)) throw CloserError(i + " is not in investors.json");
            picked.push_back(by_id[i]);
        }
    } else {
        std::vector<Val> sorted_rows = rows;
        std::vector<double> fits; for (const Val& r : sorted_rows) (void)r;
        std::stable_sort(sorted_rows.begin(), sorted_rows.end(), [](const Val& x, const Val& y) {
            const double fx = -fit_of(x), fy = -fit_of(y);
            if (fx != fy) return fx < fy;
            return id_of(x) < id_of(y);
        });
        const Val min_fit = pget(a, "min_fit");
        for (const Val& r : sorted_rows) {
            const auto rec = find(pipe, id_of(r), false);
            if (rec && truthy(*rec) && !py_eq(pget(*rec, "status"), sv("drafted"))) { skipped.push_back(id_of(r)); continue; }
            if (!min_fit.is_none() && fit_of(r) < min_fit.num()) continue;
            picked.push_back(r);
        }
        const Val top = pget(a, "top");
        if (truthy(top)) {
            long long n = static_cast<long long>(picked.size()), k = top.i;
            if (k < 0) k = std::max(0LL, n + k);
            if (k < n) picked.resize(static_cast<std::size_t>(k));
        }
    }
    if (picked.empty()) {
        print("Nobody to draft" + (skipped.empty() ? std::string() : " (" + itos(static_cast<long long>(skipped.size())) + " already in the pipeline)") + ".");
        return 1;
    }
    std::map<std::string, Val> blocked;
    for (const Val& rec : investors_of(pipe))
        if (in_v(TERMINAL, pget(rec, "status"))) blocked[lower(pystr(pget(rec, "email", sv(""))))] = pget(rec, "status");
    std::map<std::string, Val> taken_email, taken_line;
    for (const Val& r : investors_of(pipe)) taken_email[lower(pystr(pget(r, "email", sv(""))))] = need(r, "id");
    for (const Val& r : investors_of(pipe))
        if (truthy(pget(r, "personal_line"))) taken_line[norm_line(pystr(pget(r, "personal_line", sv(""))))] = need(r, "id");
    struct Drafted { Val rec; std::string subject, body; };
    std::vector<Drafted> drafted;
    std::vector<std::string> notes;
    std::vector<std::pair<std::string, std::string>> writes;
    const bool dry = truthy(pget(a, "dry_run"));
    for (const Val& inv : picked) {
        const std::string rid = id_of(inv);
        std::optional<Val> rec = find(pipe, rid, false);
        const Val status = rec && truthy(*rec) ? pget(*rec, "status") : Val();
        if (in_v(TERMINAL, status)) { refused.emplace_back(rid, "the thread is closed for good (" + pystr(status) + "): never contacted again"); continue; }
        if (!(status.is_none() || py_eq(status, sv("drafted")) || py_eq(status, sv("approved")))) {
            refused.emplace_back(rid, "already contacted (status " + pystr(status) + "): use followup or reply, not a new intro"); continue;
        }
        if (const auto prob = row_problem(inv)) { refused.emplace_back(rid, *prob); continue; }
        const std::string addr = strip(pystr(need(inv, "email")));
        const std::string al = lower(addr);
        if (blocked.count(al)) { refused.emplace_back(rid, addr + " is on a closed thread (" + pystr(blocked[al]) + "): never emailed again"); continue; }
        if (taken_email.count(al) && !py_eq(taken_email[al], sv(rid))) {
            refused.emplace_back(rid, addr + " is already in the pipeline as " + pystr(taken_email[al])); continue;
        }
        const std::string line = ws_join(str_or(pget(inv, "personal_line")));
        const auto [ok, why] = check_personal_line(line, inv, need(base, "startup").s);
        if (!ok) { refused.emplace_back(rid, why); continue; }
        const std::string key = norm_line(line);
        if (taken_line.count(key) && !py_eq(taken_line[key], sv(rid))) {
            refused.emplace_back(rid, "the same personal line is already used for " + pystr(taken_line[key]) + ": a line written for one person, used twice, is a template");
            continue;
        }
        const Val values = intro_values(base, inv, line);
        const std::string subject = fill(tpl.at("intro").subject, values, "template intro subject");
        const std::string body = fill(tpl.at("intro").body, values, "template intro");
        for (const auto& [must, what] : std::vector<std::pair<std::string, std::string>>{{line, "{personal_line}"}, {OPTOUT, "{optout}"}, {snd.name, "{signature}"}})
            if (body.find(must) == std::string::npos)
                throw CloserError("the template's intro lost " + what + ": every intro keeps the personal line, the opt-out line and your name");
        const std::string mid = message_id(rid, 1, subject, today, snd);
        const std::string name = need(values, "name").s, firm = need(values, "firm").s;
        const std::string data = make_eml(snd, name, addr, subject, body, today, mid);
        const std::string rel = "outreach/" + rid + "-1.eml";
        if (py_eq(status, sv("approved"))) notes.push_back(rid + ": re-drafted, so the earlier yes no longer counts; it needs a new yes");
        if (personal_mailbox(addr)) notes.push_back(rid + ": " + addr + " is a personal mailbox; use the work address they publish if there is one");
        const long long before = words(body.substr(0, body.find(OPTOUT)));
        if (before > 130) notes.push_back(rid + ": the email is " + itos(before) + " words before the opt-out line; under 125 reads best");
        if (cplen(subject) > 60) notes.push_back(rid + ": the subject is " + itos(static_cast<long long>(cplen(subject))) + " characters; under 60 shows in full on a phone");
        const Val sources = Val::list(iter_of(vor(pget(inv, "sources"), Val::list())));
        if (!rec) {
            Val r = obj({{"id", sv(rid)}, {"email", sv(addr)}, {"name", sv(name)}, {"firm", sv(firm)}, {"fit", pget(inv, "fit")},
                         {"status", Val()}, {"touches", Val::list()}, {"next_due", Val()}, {"labels", Val::list()}, {"notes", sv("")},
                         {"personal_line", sv(line)}, {"sources", sources}, {"thread_id", Val()}, {"call", Val()}, {"draft", Val()},
                         {"history", Val::list()}});
            need(pipe, "investors").l->push_back(r);
            rec = r;
        }
        Val& R = *rec;
        set_key(R, "email", sv(addr)); set_key(R, "name", sv(name)); set_key(R, "firm", sv(firm)); set_key(R, "fit", pget(inv, "fit"));
        set_key(R, "personal_line", sv(line)); set_key(R, "sources", Val::list(iter_of(vor(pget(inv, "sources"), Val::list()))));
        set_key(R, "draft", obj({{"n", iv(1)}, {"kind", sv("intro")}, {"file", sv(rel)}, {"subject", sv(subject)}, {"message_id", sv(mid)},
                                 {"state", sv("drafted")}, {"drafted_on", sv(iso(today))}}));
        set_status(R, "drafted");
        history(R, today, "drafted", "intro");
        taken_email[al] = sv(rid);
        taken_line[key] = sv(rid);
        writes.emplace_back(path_join(dir, rel), data);
        drafted.push_back({R, subject, body});
    }
    if (snd.address.empty())
        notes.push_back("founder.company_address is empty: anti-spam laws such as CAN-SPAM expect a postal address in commercial email, and it reads more trustworthy");
    if (!drafted.empty() && !dry) {
        for (const auto& [path, data] : writes) write_bytes(path, data);
        save_pipe(dir, pipe, today);
    }
    const long long nd = static_cast<long long>(drafted.size());
    print(std::string(dry ? "Would draft" : "Drafted") + " " + itos(nd) + " intro" + plural(nd) + " from " + snd.name + " <" + snd.email + ">, " + short_day(today) + ". Nothing is sent.");
    for (const Drafted& d : drafted)
        print("  " + ljust(pystr(need(d.rec, "id")), 18) + " " + pystr(need(d.rec, "name")) + ", " + pystr(need(d.rec, "firm")) + " <" + pystr(need(d.rec, "email")) + ">" +
              (dry ? std::string() : "  -> " + path_join(dir, pystr(need(need(d.rec, "draft"), "file")))));
    if (!drafted.empty()) {
        print("The first one:");
        print("  Subject: " + drafted[0].subject);
        print("");
        print_body(drafted[0].body);
    }
    for (const auto& [rid, why] : refused) print("REFUSED " + rid + ": " + why);
    for (const std::string& n : notes) print("note: " + n);
    if (!skipped.empty() && !(ids && !ids->empty())) print(itos(static_cast<long long>(skipped.size())) + " already in the pipeline were left alone.");
    if (!drafted.empty() && !dry) print("Wrote " + pipe_path(dir) + ". Next: closer.py batch, show it to the user, and wait for a yes.");
    return !drafted.empty() && refused.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- batch and the yes
inline const std::set<std::string> DOUBT = {"no", "not", "dont", "don't", "wait", "hold", "stop", "cancel", "but", "except", "without", "only",
                                            "maybe", "later", "tomorrow", "tonight", "if", "unless", "change", "edit", "fix", "after", "before",
                                            "instead", "remove", "drop", "skip", "minus", "monday", "tuesday", "wednesday", "thursday", "friday",
                                            "saturday", "sunday"};
inline const std::map<std::string, int> COUNT_WORDS = {{"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8},
                                                       {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"fifteen", 15}, {"twenty", 20}};
inline const std::set<std::string> FIRM_SOFT = {"ventures", "venture", "capital", "partners", "partner", "fund", "funds", "seed", "vc", "investor",
                                                "investors", "angel", "group", "labs", "holdings", "the", "and", "of", "llc", "lp", "inc",
                                                "management", "advisors", "growth", "equity"};

inline std::pair<bool, std::string> parse_batch_yes(const std::string& reply, const std::vector<Val>& items) {
    const std::string raw = strip(reply);
    if (raw.empty()) return {false, "the reply is empty"};
    if (raw.find('?') != std::string::npos) return {false, "the reply is a question, not a yes"};
    const std::vector<std::string> ws = re::findall("[a-z']+", replace_all(lower(raw), "\xE2\x80\x99", "'"));
    if (ws.empty() || (ws[0] != "yes" && ws[0] != "y")) return {false, "only a reply that starts with \"yes\" counts (not ok, sure, go, send it)"};
    std::set<std::string> bad; for (std::size_t i = 1; i < ws.size(); ++i) if (DOUBT.count(ws[i])) bad.insert(ws[i]);
    if (!bad.empty()) return {false, "the reply has a condition or a change in it (" + join(std::vector<std::string>(bad.begin(), bad.end()), ", ") + "): ask again"};
    std::set<std::string> names;
    for (const Val& rec : items) {
        for (const std::string& w : re::findall("[a-z']+", lower(pystr(pget(rec, "name", sv("")))))) if (w.size() > 1) names.insert(w);
        for (const std::string& w : re::findall("[a-z']+", lower(pystr(pget(rec, "firm", sv("")))))) if (!FIRM_SOFT.count(w) && w.size() > 2) names.insert(w);
    }
    const std::set<std::string> wset(ws.begin(), ws.end());
    std::vector<std::string> named; for (const std::string& w : names) if (wset.count(w)) named.push_back(w);
    if (!named.empty())
        return {false, "the reply names " + join(named, ", ") + ": a yes covers the whole batch you were shown. To send to fewer people, show a smaller batch (batch --ids ...)"};
    const long long n = static_cast<long long>(items.size());
    std::vector<std::string> nums;
    for (const std::string& d : re::findall(R"(\d+)", raw)) nums.push_back(int_str(BigInt::parse(ascii_digits(d))));
    for (const std::string& w : ws) { const auto c = COUNT_WORDS.find(w); if (c != COUNT_WORDS.end()) nums.push_back(itos(c->second)); }
    for (const std::string& x : nums)
        if (x != itos(n)) return {false, "the reply says " + x + ", but this batch is " + itos(n) + " email" + plural(n)};
    return {true, "explicit yes for " + itos(n) + " email" + plural(n)};
}

struct Shown { Eml msg; std::string data, body; };
inline Shown check_draft(const std::string& dir, const Val& rec, const Sender& snd) {
    const Val d = need(rec, "draft");
    const Val file = need(d, "file");
    ReadEml r = read_eml(dir, file);
    const std::string remail = lower(seed_plan::str_of(need(rec, "email"), "lower"));
    const std::vector<Addr> to = r.msg.addresses("To");
    if (to.size() != 1 || lower(to[0].addr_spec()) != remail)
        throw CloserError(pystr(file) + " is not addressed to " + need(rec, "email").s + " alone: draft it again");
    const std::vector<Addr> frm = r.msg.addresses("From");
    if (frm.size() != 1 || lower(frm[0].addr_spec()) != lower(snd.email))
        throw CloserError(pystr(file) + " is not from " + snd.email + " (founder.email): draft it again");
    if (r.msg.has("Cc") || r.msg.has("Bcc")) throw CloserError(pystr(file) + " has a Cc or Bcc: one investor per email");
    const std::string body = eml_body(r.msg);
    if (in_v(COLD, need(d, "kind")) && body.find(OPTOUT) == std::string::npos)
        throw CloserError(pystr(file) + " lost the opt-out line (" + OPTOUT + "): put it back or draft it again");
    if (body.find(snd.name) == std::string::npos) throw CloserError(pystr(file) + " lost your name: every email is signed by you");
    return Shown{r.msg, r.data, body};
}

struct Batch { std::vector<Val> items, held; long long used = 0; };
inline Batch select_batch(const Val& pipe, const Rules& rules, long long today, const std::optional<std::string>& ids, const std::optional<long long>& limit) {
    std::vector<Val> cands;
    for (const Val& r : investors_of(pipe)) if (py_eq(pget(draft_of(r), "state"), sv("drafted"))) cands.push_back(r);
    if (ids && !ids->empty()) {
        const std::vector<std::string> want = ids_of(*ids);
        for (const std::string& i : want) {
            const Val rec = *find(pipe, i);
            if (!py_eq(pget(draft_of(rec), "state"), sv("drafted"))) throw CloserError(i + " has no draft waiting for a yes");
        }
        std::vector<Val> keep; for (const Val& r : cands) if (in_v(want, need(r, "id"))) keep.push_back(r);
        cands = keep;
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Val& x, const Val& y) {
        const int kx = py_eq(need(need(x, "draft"), "kind"), sv("reply")) ? 0 : 1, ky = py_eq(need(need(y, "draft"), "kind"), sv("reply")) ? 0 : 1;
        if (kx != ky) return kx < ky;
        const double fx = -fit_of(x), fy = -fit_of(y);
        if (fx != fy) return fx < fy;
        return pystr(need(x, "id")) < pystr(need(y, "id"));
    });
    Batch b;
    b.used = cold_used(pipe, today);
    long long room = std::max(0LL, rules.cap - b.used);
    if (limit) room = std::min(room, *limit);
    long long n = 0;
    for (const Val& rec : cands) {
        if (in_v(COLD, need(need(rec, "draft"), "kind"))) {
            if (n >= room) { b.held.push_back(rec); continue; }
            n++;
        }
        b.items.push_back(rec);
    }
    return b;
}
inline std::string batch_id(const std::string& dir, std::vector<Val> items) {
    std::stable_sort(items.begin(), items.end(), [](const Val& x, const Val& y) { return pystr(need(x, "id")) < pystr(need(y, "id")); });
    std::string all;
    for (const Val& rec : items) {
        const Val d = need(rec, "draft");
        const ReadEml r = read_eml(dir, need(d, "file"));
        all += pystr(need(rec, "id")) + "|" + seed_plan::fmt_d(need(d, "n")) + "|" + pystr(need(d, "kind")) + "|" + sha(r.data) + "\n";
    }
    return sha256_hex(all).substr(0, 8);
}
inline std::string touch_label(const Val& rec, const Rules& rules) {
    const Val d = need(rec, "draft");
    if (py_eq(need(d, "kind"), sv("reply"))) return "reply in the conversation";
    if (py_eq(need(d, "kind"), sv("intro"))) return "intro (touch 1 of " + itos(rules.max_touches) + ")";
    return "follow-up " + itos(int_of(need(d, "n")) - 1) + " of " + itos(rules.max_touches - 1);
}
inline std::string batch_flags(const Val& a) {
    std::string out;
    if (truthy(pget(a, "ids"))) out += " --ids " + pystr(pget(a, "ids"));
    if (!pget(a, "limit").is_none()) out += " --limit " + seed_plan::fmt_d(pget(a, "limit"));
    return out;
}
inline long long count_cold(const std::vector<Val>& items) {
    long long n = 0; for (const Val& r : items) if (in_v(COLD, need(need(r, "draft"), "kind"))) n++;
    return n;
}

inline int cmd_batch(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    const Sender snd = sender_of(cfg);
    const Val pipe = load_pipe(dir);
    const Batch b = select_batch(pipe, rules, today, a_str(a, "ids"), a_int(a, "limit"));
    const long long nh = static_cast<long long>(b.held.size()), ni = static_cast<long long>(b.items.size());
    if (b.items.empty()) {
        if (!b.held.empty())
            print("Today's cap of " + itos(rules.cap) + " cold emails is used (" + itos(b.used) + " sent or approved). " + itos(nh) + " draft" + plural(nh) + " wait for the next day.");
        else
            print("Nothing is waiting for a yes. Draft first: closer.py draft, followup or reply.");
        return 1;
    }
    std::vector<Shown> shown; for (const Val& r : b.items) shown.push_back(check_draft(dir, r, snd));
    const std::string bid = batch_id(dir, b.items);
    const long long cold = count_cold(b.items);
    const std::string approve = "closer.py approve --batch " + bid + batch_flags(a) + " --reply \"<the reply, word for word>\"";
    if (truthy(pget(a, "json"))) {
        Val held = Val::list(); for (const Val& r : b.held) held.l->push_back(need(r, "id"));
        Val recips = Val::list();
        for (const Val& r : b.items)
            recips.l->push_back(obj({{"id", need(r, "id")}, {"name", need(r, "name")}, {"firm", need(r, "firm")}, {"email", need(r, "email")},
                                     {"touch", sv(touch_label(r, rules))},
                                     {"personal_line", py_eq(need(need(r, "draft"), "kind"), sv("intro")) ? pget(r, "personal_line") : Val()}}));
        const Val doc = obj({{"batch", sv(bid)}, {"date", sv(iso(today))}, {"emails", iv(ni)}, {"cold", iv(cold)}, {"cap", iv(rules.cap)},
                             {"used_today", iv(b.used)}, {"held", held}, {"recipients", recips},
                             {"first", obj({{"to", sv(shown[0].msg.str("To"))}, {"subject", sv(shown[0].msg.str("Subject"))}, {"body", sv(shown[0].body)}})},
                             {"approve", sv(approve)}});
        print(dumps(doc, json_opts()));
        return 0;
    }
    print("BATCH " + bid + ": " + itos(ni) + " email" + plural(ni) + " from " + snd.name + " <" + snd.email + ">, " + short_day(today));
    print("Daily cap: " + itos(rules.cap) + " cold emails. " + itos(b.used) + " sent or approved today, " + itos(cold) + " in this batch.");
    long long i = 0;
    for (const Val& r : b.items) {
        print("  " + itos(++i) + ". " + pystr(need(r, "name")) + ", " + pystr(need(r, "firm")) + " <" + pystr(need(r, "email")) + ">  " + touch_label(r, rules));
        if (py_eq(need(need(r, "draft"), "kind"), sv("intro"))) print("     \"" + pystr(pget(r, "personal_line", sv(""))) + "\"");
    }
    print("The first email, in full:");
    print("  To: " + shown[0].msg.str("To"));
    print("  Subject: " + shown[0].msg.str("Subject"));
    print("");
    print_body(shown[0].body);
    print("");
    if (!b.held.empty()) print(itos(nh) + " more draft" + plural(nh) + " wait for the next day (the daily cap).");
    print("Every email is in " + path_join(dir, "outreach") + " (open any of them before you answer).");
    print("Ask: \"Send these " + itos(ni) + " email" + plural(ni) + " from your Gmail? Reply yes or no.\"");
    print("Then: " + approve);
    return 0;
}

inline Val outbox_row(const std::string& dir, const Val& rec, const Sender& snd) {
    const Val d = need(rec, "draft");
    const Shown s = check_draft(dir, rec, snd);
    if (truthy(pget(d, "sha")) && !py_eq(sv(sha(s.data)), need(d, "sha")))
        throw CloserError(pystr(need(d, "file")) + " changed after the yes: show it again (batch) before it goes anywhere");
    return obj({{"id", need(rec, "id")}, {"kind", need(d, "kind")}, {"touch", need(d, "n")}, {"to", sv(s.msg.str("To"))}, {"to_email", need(rec, "email")},
                {"subject", sv(s.msg.str("Subject"))}, {"body", sv(s.body)}, {"thread_id", pget(rec, "thread_id")},
                {"in_reply_to", pget(d, "in_reply_to")}, {"eml", sv(path_join(dir, pystr(need(d, "file"))))}});
}

inline int cmd_approve(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Rules rules = outreach_rules(cfg);
    const Sender snd = sender_of(cfg);
    Val pipe = load_pipe(dir);
    const Batch b = select_batch(pipe, rules, today, a_str(a, "ids"), a_int(a, "limit"));
    if (b.items.empty()) throw CloserError("nothing is waiting for a yes (or today's cap is used): run batch");
    for (const Val& r : b.items) check_draft(dir, r, snd);
    const std::string bid = batch_id(dir, b.items);
    const std::string asked = pystr(pget(a, "batch"));
    if (asked != bid)
        throw CloserError("batch " + asked + " is not the batch waiting now (" + bid + "): it changed since it was shown. Run batch again and show it to the user");
    const std::string reply = str_or(pget(a, "reply"));
    const auto [ok, why] = parse_batch_yes(reply, b.items);
    if (!ok) { print("Not recorded: " + why + ". Show the batch again and ask."); return 1; }
    const long long cold = count_cold(b.items);
    if (b.used + cold > rules.cap) throw CloserError("this batch has " + itos(cold) + " cold emails and the daily cap leaves " + itos(rules.cap - b.used));
    for (const Val& r0 : b.items) {
        Val r = r0;
        Val d = need(r, "draft");
        const ReadEml e = read_eml(dir, need(d, "file"));
        set_key(d, "state", sv("approved")); set_key(d, "approved_on", sv(iso(today))); set_key(d, "reply", pget(a, "reply"));
        set_key(d, "batch", sv(bid)); set_key(d, "sha", sv(sha(e.data)));
        if (py_eq(need(d, "kind"), sv("intro"))) set_status(r, "approved");
        history(r, today, "approved", "batch " + bid + ", reply " + repr(pget(a, "reply")));
    }
    save_pipe(dir, pipe, today);
    Val rows = Val::list(); for (const Val& r : b.items) rows.l->push_back(outbox_row(dir, r, snd));
    const long long ni = static_cast<long long>(b.items.size());
    print("Recorded your yes for batch " + bid + ": " + itos(ni) + " email" + plural(ni) + " (" + itos(b.used + cold) + " of today's " + itos(rules.cap) + " cold emails).");
    print("Now send each one through the Gmail MCP server the user added in A.S.T.R.O AI's MCP settings, one tool call per email. A follow-up or a reply goes into its thread_id. In Ask mode A.S.T.R.O AI's card asks Allow or Deny every time (answer each one; never Allow all for sends):");
    print(dumps(rows, json_opts()));
    print("After each send: closer.py log --id <id> --event sent --thread-id <the Gmail thread id>.");
    print("If one is denied on the card: closer.py log --id <id> --event denied (it goes back to drafted).");
    return 0;
}

inline int cmd_outbox(const Val& a) {
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Sender snd = sender_of(cfg);
    const Val pipe = load_pipe(dir);
    Val rows = Val::list();
    for (const Val& r : investors_of(pipe)) if (py_eq(pget(draft_of(r), "state"), sv("approved"))) rows.l->push_back(outbox_row(dir, r, snd));
    const long long n = static_cast<long long>(rows.l->size());
    if (truthy(pget(a, "json"))) { print(dumps(rows, json_opts())); return n ? 0 : 1; }
    if (!n) { print("Nothing is approved and waiting to be sent."); return 1; }
    print(itos(n) + " approved email" + plural(n) + ", not sent yet:");
    for (const Val& r : *rows.l)
        print("  " + ljust(pystr(need(r, "id")), 18) + " " + pystr(need(r, "to")) + "  " + pystr(need(r, "subject")) +
              (truthy(need(r, "thread_id")) ? "  (thread " + pystr(need(r, "thread_id")) + ")" : std::string()));
    return 0;
}
}  // namespace packtools::seed_closer
