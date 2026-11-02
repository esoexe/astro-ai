// seed-closer, part 5: inbox. Each reply is sorted by rules (interested, question, not now, no, out of office, opt-out,
// bounce, unclear), matched to its investor, and, with --write, recorded in the pipeline.
namespace packtools::seed_closer {

inline const std::vector<std::string> BOUNCE = {
    R"x(delivery status notification)x", R"x(undeliverable)x", R"x(delivery has failed)x", R"x(address not found)x",
    R"x(could ?n[o']t be delivered)x", R"x(was ?n[o']t delivered)x", R"x(wasn't delivered)x", R"x(mailbox (unavailable|not found|full))x",
    R"x(user unknown)x", R"x(no such user)x", R"x(recipient address rejected)x", R"x(\b550[ -]5\.1\.\d\b)x",
    R"x(message not delivered)x", R"x(returned mail)x", R"x(delivery failure)x"};
inline const std::vector<std::string> OOO = {
    R"x(out of (the )?office)x", R"x(\booo\b)x", R"x(\bon (vacation|holiday|leave|parental leave|paternity leave|maternity leave|pto)\b)x",
    R"x(automatic reply)x", R"x(auto[- ]?reply)x", R"x(\baway (from (my desk|the office) )?until\b)x",
    R"x(limited access to (my )?(email|e-mail))x", R"x(currently travel)x", R"x(\breturning on\b)x", R"x(\bback in the office\b)x"};
inline const std::vector<std::string> OPTOUT_PAT = {
    R"x(unsubscribe)x", R"x(\bremove me\b)x", R"x(\btake me off\b)x", R"x(\bstop (emailing|contacting|sending|writing)\b)x",
    R"x(\b(do not|don't|dont) (contact|email|e-mail|reach out to|write to) me\b)x", R"x(\bno more emails\b)x",
    R"x(\bopt(ing)?[- ]?out\b)x", R"x(\bremove my (email|address)\b)x", R"x(\boff your (list|mailing list)\b)x"};
inline const std::vector<std::string> NO_PAT = {
    R"x(\bnot (a|the right) (fit|match)\b)x", R"x(\b(is ?n[o']t|is not|isn't) a (fit|match)\b)x", R"x(\bno,? thank(s| you)\b)x",
    R"x(\bpass on (this|it)\b)x", R"x(\b(we|i)('ll| will| are going to| have to|'re going to| must) pass\b)x",
    R"x(\b(we're|we are|i'm|i am) passing\b)x", R"x(\bnot for (us|me)\b)x", R"x(\bnot interested\b)x",
    R"x(\boutside (of )?(our|my) (thesis|focus|scope|mandate|remit)\b)x", R"x(\b(don't|do not|dont) invest in\b)x",
    R"x(\b(aren't|are not|not) investing in\b)x", R"x(\b(doesn't|does not|dont|don't) fit\b)x",
    R"x(\bnot something (we|i)\b)x", R"x(\bdecline\b)x", R"x(\bgoing to pass\b)x"};
inline const std::vector<std::string> NOT_NOW = {
    R"x(\bnot (right )?now\b)x", R"x(\bfor now\b)x", R"x(\bnot this (quarter|year|round|time)\b)x", R"x(\bnot at this time\b)x",
    R"x(\b(too|a bit|a little) early\b)x", R"x(\bcome back\b)x", R"x(\bcircle back\b)x", R"x(\bkeep me (posted|updated|in the loop)\b)x",
    R"x(\b(keep|stay) in touch\b)x", R"x(\bcheck back\b)x", R"x(\breach out again\b)x", R"x(\brevisit\b)x",
    R"x(\bin a (few|couple of) months\b)x", R"x(\bnext (quarter|year)\b)x", R"x(\b(when|once) you have (more )?traction\b)x",
    R"x(\b(once|after) you('ve| have)? launch)x", R"x(\btiming (isn't|is not|isnt) right\b)x", R"x(\bbandwidth\b)x",
    R"x(\bnot ready\b)x", R"x(\bupdate me\b)x", R"x(\bsend (me )?updates\b)x"};
inline const std::vector<std::string> INTERESTED = {
    R"x(\bhappy to (chat|talk|connect|meet|hop on|jump on|take a look)\b)x",
    R"x(\b(love|like) to (chat|talk|learn|hear|see|meet|connect)\b)x",
    R"x(\bwould (like|love) to (chat|talk|learn|meet|see|hear)\b)x",
    R"x(\blet's (chat|talk|meet|connect|find|set|grab|do|schedule|get)\b)x",
    R"x(\b(set|setting) up (a |some )?(call|time|meeting|chat)\b)x",
    R"x(\b(grab|find|book|schedule) (some )?(time|a time|a call|a slot|a meeting)\b)x",
    R"x(\bsend (me |over |us )?(the |your )?deck\b)x", R"x(\bdeck over\b)x", R"x(\bshare (the |your )?deck\b)x",
    R"x(\bworks for (me|us)\b)x", R"x(\bthat works\b)x", R"x(\b(monday|tuesday|wednesday|thursday|friday|tomorrow|next week)[^.?!]{0,20}\bworks\b)x",
    R"x(\bsend (me |us )?(an |the )?invite\b)x", R"x(calendly|cal\.com)x",
    R"x(\bi'm in\b)x", R"x(\binterested in (learning|hearing|chatting|talking|seeing)\b)x",
    R"x(\b(i am|i'm|we're|we are) (very |really )?interested\b)x", R"x(\bcount (me|us) in\b)x",
    R"x(\bsounds (great|good|interesting)\b)x", R"x(\blove this\b)x", R"x(\bintrigued\b)x", R"x(\btell me more\b)x",
    R"x(\bkeen to\b)x"};
inline const char* const QUESTION_START = R"(^(what|how|who|when|why|which|where|can|could|do|does|is|are|have|has|would|will)\b)";
inline const std::map<std::string, int> MONTH_KEYS = {{"jan", 1}, {"feb", 2}, {"mar", 3}, {"apr", 4}, {"may", 5}, {"jun", 6}, {"jul", 7},
                                                      {"aug", 8}, {"sep", 9}, {"sept", 9}, {"oct", 10}, {"nov", 11}, {"dec", 12}};
inline const std::string MONTH_RE = R"((january|february|march|april|may|june|july|august|september|october|november|december|jan|feb|mar|apr|jun|jul|aug|sept|sep|oct|nov|dec)\.?)";
inline const std::string RETURN_LEAD = R"((?:until|till|back|return|returning|through|thru|on leave until|away until))";

inline std::string strip_quoted(const std::string& text) {   // the new part of a reply: quotes, "On ... wrote:" and signatures dropped
    const std::vector<std::string> lines = split(replace_all(text, "\r\n", "\n"), "\n");
    std::vector<std::string> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::string s = strip(line);
        if (starts(s, ">")) continue;
        const std::string low = lower(s);
        if (starts(low, "on ") && (low.find("wrote:") != std::string::npos || (i + 1 < lines.size() && lower(lines[i + 1]).find("wrote:") != std::string::npos))) break;
        if (re::match(R"(^-{2,}\s*(original message|forwarded message))", low)) break;
        if (re::match(R"(^(from|sent|date):\s)", low) && !out.empty()) {
            bool hdr = false;
            for (std::size_t j = i + 1; j < lines.size() && j < i + 4; ++j) if (re::match(R"(^\s*(sent|to|subject|date):\s)", lower(lines[j]))) hdr = true;
            if (hdr) break;
        }
        if (s == "--" || s == "-- ") break;
        out.push_back(line);
    }
    return strip(join(out, "\n"));
}

inline std::optional<long long> year_for(long long m, long long d, long long today) {
    const long long y = year_of(today);
    if (!valid_date(y, static_cast<int>(m), static_cast<int>(d))) return std::nullopt;
    long long cand = make_day(y, static_cast<int>(m), static_cast<int>(d));
    if (cand < today) {
        if (!valid_date(y + 1, static_cast<int>(m), static_cast<int>(d))) return std::nullopt;
        cand = make_day(y + 1, static_cast<int>(m), static_cast<int>(d));
    }
    return cand;
}
inline std::optional<long long> find_return_date(const std::string& text, long long today) {
    const std::string low = ws_join(lower(text));
    re::Match m = re::search(R"(\b(20\d\d)-(\d\d)-(\d\d)\b)", low);
    if (m) {
        const long long y = to_ll(m.group(1)), mo = to_ll(m.group(2)), d = to_ll(m.group(3));
        if (valid_date(y, static_cast<int>(mo), static_cast<int>(d))) return make_day(y, static_cast<int>(mo), static_cast<int>(d));
    }
    m = re::search(RETURN_LEAD + R"([^.\n]{0,30}?\b)" + MONTH_RE + R"(\s+(\d{1,2})(?:st|nd|rd|th)?\b)", low);
    if (m) return year_for(MONTH_KEYS.at(m.group(1).substr(0, 3)), to_ll(m.group(2)), today);
    m = re::search(RETURN_LEAD + R"([^.\n]{0,30}?\b(\d{1,2})(?:st|nd|rd|th)?\s+(?:of\s+)?)" + MONTH_RE, low);
    if (m) return year_for(MONTH_KEYS.at(m.group(2).substr(0, 3)), to_ll(m.group(1)), today);
    m = re::search(RETURN_LEAD + R"([^.\n]{0,20}?\b(\d{1,2})/(\d{1,2})\b)", low);
    if (m) return year_for(to_ll(m.group(1)), to_ll(m.group(2)), today);
    m = re::search(RETURN_LEAD + R"(\s+(?:on\s+)?(?:next\s+)?(monday|tuesday|wednesday|thursday|friday|saturday|sunday))", low);
    if (m) {
        int want = 0; for (int i = 0; i < 7; ++i) if (lower(DAY_NAMES[i]) == m.group(1)) want = i;
        int ahead = ((want - weekday(today)) % 7 + 7) % 7; if (!ahead) ahead = 7;
        return today + ahead;
    }
    return std::nullopt;
}
inline std::optional<std::string> any_of(const std::vector<std::string>& patterns, const std::string& text) {
    for (const std::string& p : patterns) { const re::Match m = re::search(p, text); if (m) return m.group(0); }
    return std::nullopt;
}

struct Info { std::string cls, why; std::optional<long long> back; bool deck = false; };
inline std::string text_arg(const Val& v, const char* method) {   // (v or "") followed by a str method
    if (!truthy(v)) return "";
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + method + "'");
    return v.s;
}
inline Info classify(const Val& text, const Val& subject, const Val& sender, long long today) {
    const std::string raw = replace_all(text_arg(text, "replace"), "\xE2\x80\x99", "'");
    if (truthy(subject) && subject.t != Val::STR) throw PyErr("TypeError", std::string("can only concatenate str (not \"") + type_name(subject) + "\") to str");
    const std::string sub = truthy(subject) ? subject.s : "";
    const std::string whole = ws_join(lower(raw + " " + sub));
    const std::string from = text_arg(sender, "lower");
    if (re::search("mailer-daemon|postmaster", lower(from)) || any_of(BOUNCE, whole)) {
        const auto w = any_of(BOUNCE, whole);
        return Info{"bounce", w ? *w : "from the mail system", std::nullopt, false};
    }
    const std::string body = strip_quoted(raw);
    const std::string low = ws_join(lower(body));
    const std::string subj = ws_join(lower(sub));
    auto hit = any_of(OOO, low); if (!hit) hit = any_of(OOO, subj);
    if (hit) return Info{"ooo", *hit, find_return_date(body, today), false};
    hit = any_of(OPTOUT_PAT, low);
    if (hit) return Info{"optout", *hit, std::nullopt, false};
    const auto later = any_of(NOT_NOW, low);
    const std::string bare = strip(re::sub("[^a-z ]", "", low));
    auto no = any_of(NO_PAT, low);
    if (!no && (bare == "no" || bare == "nope" || bare == "pass" || bare == "no thanks" || bare == "no thank you")) no = bare;
    const bool deck = static_cast<bool>(re::search(R"(\bdeck\b)", low));
    if (later) return Info{"not_now", *later, std::nullopt, false};
    if (no) return Info{"no", *no, std::nullopt, false};
    hit = any_of(INTERESTED, low);
    if (hit) return Info{"interested", *hit, std::nullopt, deck};
    if (body.find('?') != std::string::npos || re::match(QUESTION_START, low)) return Info{"question", "asks a question", std::nullopt, deck};
    return Info{"unclear", "no rule matched", std::nullopt, deck};
}

inline std::string next_action(const std::string& cls, const Info& info, const Val& rec_id) {
    const std::string rid = truthy(rec_id) ? pystr(rec_id) : "<id>";
    if (cls == "interested") {
        std::string s = "Offer two times from the user's calendar (or ask what suits them). When they agree on one: closer.py book --id " + rid +
                        " --start \"YYYY-MM-DD HH:MM\" --tz ... --link <video link>.";
        if (info.deck) s += " They asked for the deck: reply with the deck (seed/deck.html printed to PDF, or a link), after the yes.";
        return s;
    }
    if (cls == "question")
        return "Answer it in the thread. Astro drafts the answer from seed/plan.json and seed/model.json, never a number that is not in them: closer.py reply --id " + rid +
               " --body-file answer.txt, then batch and the yes.";
    if (cls == "not_now") return "Soft pass: no more follow-ups. If they asked to be kept posted, seed-updates --to prospects sends them the monthly update.";
    if (cls == "no") return "A no. The thread is closed for good: no follow-ups, nothing more is sent.";
    if (cls == "optout") return "Opted out. Never emailed again, from any batch.";
    if (cls == "bounce") return "Bounced. The thread stops. Find their published address with seed-investors; never guess one.";
    if (cls == "ooo") {
        if (info.back) return "An auto-reply, not an answer. The next follow-up waits until after " + iso(*info.back) + ".";
        return "An auto-reply, not an answer. No return date found, so the follow-up keeps its date.";
    }
    return "The rules could not sort it. Read it yourself and tell Astro what to answer; nothing goes out without a yes.";
}

inline std::vector<Val> load_replies(const Val& a) {
    const auto one = [&](const Val& text) {
        return obj({{"id", pget(a, "id")}, {"from", vor(pget(a, "sender"), sv(""))}, {"subject", vor(pget(a, "subject"), sv(""))},
                    {"text", text}, {"date", pget(a, "date")}});
    };
    if (!pget(a, "text").is_none()) return {one(pget(a, "text"))};
    if (!truthy(pget(a, "file"))) throw CloserError("give the replies: --file replies.json (or a .txt with --id), or --text \"...\"");
    const std::string file = pystr(pget(a, "file"));
    if (!exists(file)) throw CloserError("no such file: " + file);
    const std::string raw = read_text_utf8(file);
    if (ends(lower(file), ".json")) {
        Val doc;
        try { doc = loads(raw); }
        catch (const PyErr& e) { if (e.type != "json.decoder.JSONDecodeError" && e.type != "ValueError") throw; throw CloserError(file + " is not valid JSON: " + e.what()); }
        const Val rows = doc.t == Val::DICT ? pget(doc, "replies") : doc;
        if (rows.t != Val::LIST) throw CloserError(file + " needs a list of replies: [{\"id\" or \"from\", \"text\", ...}]");
        std::vector<Val> o; for (const Val& r : *rows.l) if (r.t == Val::DICT) o.push_back(r);
        return o;
    }
    return {one(sv(raw))};
}

inline std::optional<Val> find_v(const Val& pipe, const Val& rec_id) {   // find(pipe, rec_id) for any JSON id
    for (const Val& rec : investors_of(pipe)) if (py_eq(pget(rec, "id"), rec_id)) return rec;
    throw CloserError(pystr(rec_id) + " is not in the pipeline (draft them first)");
}
inline std::optional<Val> match_reply(const Val& pipe, const Val& r, const std::string& cls) {
    if (truthy(pget(r, "id"))) return find_v(pipe, need(r, "id"));
    Val from = pget(r, "from"); if (!truthy(from)) from = pget(r, "email");
    const std::string addr = lower(parseaddr_email(str_or(from)));
    for (const Val& rec : investors_of(pipe))
        if (!addr.empty() && lower(seed_plan::str_of(pget(rec, "email", sv("")), "lower")) == addr) return rec;
    if (cls == "bounce") {
        const std::string text = lower(str_or(pget(r, "text")));
        for (const Val& rec : investors_of(pipe))
            if (truthy(pget(rec, "email")) && text.find(lower(seed_plan::str_of(need(rec, "email"), "lower"))) != std::string::npos) return rec;
    }
    return std::nullopt;
}

inline int cmd_inbox(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const bool write = truthy(pget(a, "write"));
    const Val cfg = write ? load_config(dir) : vor(read_json(path_join(dir, "config.json"), "config", false), Val::dict());
    const Rules rules = outreach_rules(cfg);
    Val pipe = load_pipe(dir);
    const std::vector<Val> rows = load_replies(a);
    if (rows.empty()) throw CloserError("no replies to sort");
    Val results = Val::list();
    for (const Val& r : rows) {
        const Info info = classify(pget(r, "text", sv("")), pget(r, "subject", sv("")), pget(r, "from", sv("")), today);
        const std::string& cls = info.cls;
        std::optional<Val> rec = match_reply(pipe, r, cls);
        const std::string body = ws_join(strip_quoted(pystr(pget(r, "text", sv("")))));
        const Val rid = rec ? need(*rec, "id") : Val();
        Val res = obj({{"id", rid}, {"name", rec ? need(*rec, "name") : vor(pget(r, "from"), sv("unknown sender"))}, {"class", sv(cls)},
                       {"label", sv(LABELS.at(cls))}, {"why", sv(info.why)}, {"back", info.back ? sv(iso(*info.back)) : Val()},
                       {"excerpt", sv(cpslice(body, 0, 160))}, {"next", sv(next_action(cls, info, rid))}});
        if (write && rec) {
            const long long day = truthy(pget(r, "date")) ? parse_day(need(r, "date"), "reply date") : today;
            set_key(res, "recorded", sv(record_reply(*rec, cls, day, rules, info.why, info.back, body)));
        }
        results.l->push_back(res);
    }
    if (write) save_pipe(dir, pipe, today);
    if (truthy(pget(a, "json"))) { print(dumps(results, json_opts())); return 0; }
    for (const Val& res : *results.l) {
        const Val id = need(res, "id");
        print(pystr(need(res, "name")) + (truthy(id) ? " [" + pystr(id) + "]" : std::string(" [not in the pipeline]")) + ": " + upper(need(res, "label").s) +
              " (" + need(res, "why").s + ")");
        print("  \"" + need(res, "excerpt").s + "\"");
        print("  next: " + need(res, "next").s);
        if (truthy(pget(res, "recorded"))) print("  recorded: " + need(res, "recorded").s);
    }
    if (!write) print("Nothing recorded yet. Check the labels with the user, then run it again with --write.");
    return 0;
}
}  // namespace packtools::seed_closer
