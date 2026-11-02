// console_widescreen.inc - console-graphics/widescreen: a C++ port of console-skill/console-graphics/widescreen.py
// (Jake Schincariol, MIT). scan lists a PS2 game's 4:3 aspect and FOV candidates (data floats and lui/ori/mtc1
// constants) with the pnach lines that set each one's 16:9 value; values prints the 4:3 -> 16:9 table and the Hor+
// FOV conversion. The ELF reader, code analysis and value search are tune.py's, byte for byte (console_tune.inc).
namespace packtools::console_widescreen {
using namespace pyrt;
using namespace packtools::console_common;
using console_tune::Elf;
using console_tune::Hit;
using console_tune::Target;
using console_tune::f2u;
using console_tune::u2f;
using console_tune::fmt;
using console_override::hex32;

const char* const PROG = "widescreen";
const double WIDE = (16.0 / 9.0) / (4.0 / 3.0);   // how much wider 16:9 is than 4:3
struct Aspect { const char* label; double value, factor; };
// width/height ratios widen by 4/3, height/width ratios shrink by 3/4
const Aspect ASPECTS[] = {
    {"4:3 width/height", 4.0 / 3.0, WIDE}, {"3:4 height/width", 3.0 / 4.0, 1.0 / WIDE},
    {"640/448 frame", 640.0 / 448.0, WIDE}, {"448/640 frame", 448.0 / 640.0, 1.0 / WIDE},
    {"512/448 frame", 512.0 / 448.0, WIDE}, {"448/512 frame", 448.0 / 512.0, 1.0 / WIDE},
    {"640/512 PAL frame", 640.0 / 512.0, WIDE}, {"512/640 PAL frame", 512.0 / 640.0, 1.0 / WIDE}};
constexpr double REL_TOL = 5e-4;
const char* const HONEST[] = {
    "These are candidates. The tool cannot know which number is the camera's: many games use 0.75 or",
    "1.3333334 for other things too. Try ONE at a time in PCSX2 (each --out section is one try): the right",
    "one widens the view (more on the sides) instead of stretching or squashing it. Some games need two",
    "values changed together (projection and culling), and 2D menus and HUDs may need their own fix."};
inline std::string honest_joined() { std::vector<std::string> h(std::begin(HONEST), std::end(HONEST)); return join(h, " "); }

inline std::optional<std::string> serial_from_name(const std::string& name) {
    const auto m = re::match(R"(^([A-Za-z]{4})[_-](\d{3})\.?(\d{2}))", name);
    if (!m) return std::nullopt;
    return upper(m.group(1)) + "-" + m.group(2) + m.group(3);
}
inline long long pcsx2_crc(const std::string& data) {
    uint32_t crc = 0;
    for (std::size_t i = 0; i + 4 <= data.size(); i += 4) crc ^= static_cast<uint32_t>(u32le(data, i));
    return crc;
}

struct Cand { Hit h; double wide_value; long long wide_bits; std::vector<std::string> notes; std::vector<std::pair<long long, long long>> words; std::vector<std::string> pnach; };
// instruction words that make a code constant equal `bits`, and a note when a lone lui has to round
inline std::pair<std::vector<std::pair<long long, long long>>, std::string> new_words(const Elf& elf, const Hit& h, long long bits) {
    const long long lui_w = *elf.word(h.lui);
    if (h.lo >= 0) {
        const long long lo_w = *elf.word(h.lo);
        const auto [hi, lo] = console_tune::split_hi_lo(bits, h.lo_op);
        return {{{h.lui, (lui_w & 0xFFFF0000LL) | hi}, {h.lo, (lo_w & 0xFFFF0000LL) | lo}}, ""};
    }
    std::string note;
    if (bits & 0xFFFF) {
        const long long nearest_bits = (bits + 0x8000) & 0xFFFF0000LL;
        note = "a lone lui cannot hold " + fmt(u2f(bits)) + " exactly: uses " + fmt(u2f(nearest_bits));
        bits = nearest_bits;
    }
    return {{{h.lui, (lui_w & 0xFFFF0000LL) | (bits >> 16)}}, note};
}
inline std::vector<Cand> candidates(const Elf& elf, long long limit) {
    std::vector<Target> targets; for (const auto& a : ASPECTS) targets.push_back({a.label, a.value, a.value * REL_TOL});
    std::vector<Cand> o;
    for (const auto& h : console_tune::search(elf, targets, limit)) {
        double factor = 1; for (const auto& a : ASPECTS) if (h.target == a.label) factor = a.factor;
        Cand c{h, 0, f2u(h.value * factor), {}, {}, {}};
        c.wide_value = u2f(c.wide_bits);
        if (h.kind == "data") c.words = {{h.addr, c.wide_bits}};
        else {
            const auto [ws, note] = new_words(elf, h, c.wide_bits);
            for (const auto& [x, w] : ws) { const auto cur = elf.word(x); if (!cur || static_cast<long long>(*cur) != w) c.words.emplace_back(x, w); }
            if (!note.empty()) { c.notes.push_back(note); c.wide_value = u2f((ws[0].second & 0xFFFF) << 16); }
        }
        for (const auto& [a, w] : c.words) c.pnach.push_back(console_tune::pnach_line(1, a, w));
        o.push_back(std::move(c));
    }
    return o;
}
inline std::string how_of(const Cand& c, const char* lui_sep) { return c.h.lo < 0 ? std::string("lui") : std::string("lui") + lui_sep + c.h.lo_op; }
inline std::string pnach_text(const Elf& elf, const std::vector<Cand>& cands) {
    std::vector<std::string> lines{"gametitle=" + elf.name + " widescreen candidates (console-graphics widescreen.py)",
                                   "comment=Enable ONE section at a time. Each changes one 4:3 value to its 16:9 value.", ""};
    int i = 0;
    for (const auto& c : cands) {
        ++i;
        const std::string how = c.h.kind == "data" ? "data, " + c.h.where : "code, " + how_of(c, "+");
        lines.push_back(cfmt("[Widescreen try %02d: ", i) + fmt(c.h.value) + " at " + hex32(c.h.addr) + " (" + how + ")]");
        lines.push_back("author=console-graphics widescreen.py");
        lines.push_back("description=" + c.h.target + " " + fmt(c.h.value) + " -> " + fmt(c.wide_value) + ". Right if the view widens without stretching.");
        lines.push_back("gsaspectratio=16:9");
        lines.insert(lines.end(), c.pnach.begin(), c.pnach.end());
        lines.push_back("");
    }
    return join(lines, "\n");
}

inline int cmd_scan(const Val& a) {
    const Elf elf(S(a, "elf"));
    const auto cands = candidates(elf, get(a, "limit").i);
    const auto serial = serial_from_name(elf.name);
    const std::string target = (serial ? *serial : std::string("<SERIAL>")) + cfmt("_%08llX.pnach", static_cast<unsigned long long>(pcsx2_crc(elf.data)));
    const Val outv = get(a, "out");
    if (!outv.is_none()) {
        const std::string o = outv.s;
        if (exists(o) && !B(a, "force")) throw InputError(o + " exists: pass --force to overwrite it");
        makedirs(dirname(abspath(o)));
        std::FILE* f = open_file(o, "wb");
        if (!f) throw OSError(errno, o);
        const std::string t = pnach_text(elf, cands);
        std::fwrite(t.data(), 1, t.size(), f); std::fclose(f);
    }
    if (B(a, "json")) {
        List rows;
        for (const auto& c : cands) {
            Val d = console_tune::hit_json(c.h);
            List ws; for (const auto& [x, w] : c.words) ws.push_back(dict_of({{"addr", Val::str(hex32(x))}, {"new", Val::str(hex32(w))}}));
            d.d->set("wide_value", Val::str(fmt(c.wide_value))); d.d->set("wide_bits", Val::str(hex32(c.wide_bits)));
            d.d->set("notes", str_list(c.notes)); d.d->set("words", Val::list(std::move(ws))); d.d->set("pnach", str_list(c.pnach));
            rows.push_back(d);
        }
        out(dumps2(dict_of({{"elf", Val::str(elf.name)}, {"pcsx2_pnach", Val::str(target)}, {"candidates", Val::list(std::move(rows))}, {"honest", Val::str(honest_joined())}, {"out", outv}})));
        return cands.empty() ? 1 : 0;
    }
    out(cfmt("Widescreen candidates in %s: %zu", elf.name.c_str(), cands.size()));
    int i = 0;
    for (const auto& c : cands) {
        ++i;
        const std::string head = c.h.kind == "data" ? "DATA " + hex32(c.h.addr) + "  " + c.h.where
                                                    : "CODE " + hex32(c.h.addr) + "  " + how_of(c, "+") + cfmt(" -> mtc1 $f%d", c.h.freg);
        out(cfmt("\ntry %02d  ", i) + head + "   " + fmt(c.h.value) + " (" + c.h.target + ") -> " + fmt(c.wide_value));
        if (c.h.kind == "data" && !c.h.read_by.empty()) {
            std::vector<std::string> r; for (std::size_t k = 0; k < c.h.read_by.size() && k < 4; ++k) r.push_back(c.h.read_by[k].insn + " at " + hex32(c.h.read_by[k].at));
            out("        read by: " + join(r, ", "));
        }
        for (const auto& l : c.pnach) out("        " + l);
        for (const auto& n : c.notes) out("        note: " + n);
    }
    out("");
    for (const char* l : HONEST) out(l);
    if (!outv.is_none()) out("\nwrote " + outv.s + " (one [section] per try). Copy the sections into PCSX2's " + target);
    else out("\nWrite them as a pnach with --out console/patches/widescreen-try.pnach (PCSX2 wants " + target + ").");
    out("Found it? Bake it in with /console-mod (pnach.py toml / override), then stretch the port's output to 16:9 (see reference/widescreen.md).");
    return cands.empty() ? 1 : 0;
}

inline double radians(double x) { return x * (3.141592653589793 / 180.0); }   // CPython: x * degToRad
inline double degrees(double x) { return x * (180.0 / 3.141592653589793); }
inline int cmd_values(const Val& a) {
    struct Row { std::string label, value, bits, wide_value, wide_bits, rule; };
    std::vector<Row> rows;
    for (const auto& s : ASPECTS)
        rows.push_back({s.label, fmt(u2f(f2u(s.value))), hex32(f2u(s.value)), fmt(u2f(f2u(s.value * s.factor))), hex32(f2u(s.value * s.factor)), s.factor > 1 ? "x4/3" : "x3/4"});
    Val fov = Val::none();
    double fov_deg = 0, wide = 0;
    if (!get(a, "fov").is_none()) {
        fov_deg = get(a, "fov").f;
        if (!(0 < fov_deg && fov_deg < 180)) throw InputError("--fov must be an angle between 0 and 180 degrees");
        wide = degrees(2 * std::atan(std::tan(radians(fov_deg) / 2) * WIDE));
        fov = dict_of({{"horizontal_4_3", Val::flt(fov_deg)}, {"horizontal_16_9", Val::flt(round_n(wide, 4))},
                       {"radians_4_3", Val::flt(round_n(radians(fov_deg), 7))}, {"radians_16_9", Val::flt(round_n(radians(wide), 7))}});
    }
    if (B(a, "json")) {
        List rl;
        for (const auto& r : rows) rl.push_back(dict_of({{"label", Val::str(r.label)}, {"value", Val::str(r.value)}, {"bits", Val::str(r.bits)}, {"wide_value", Val::str(r.wide_value)}, {"wide_bits", Val::str(r.wide_bits)}, {"rule", Val::str(r.rule)}}));
        out(dumps2(dict_of({{"aspects", Val::list(std::move(rl))}, {"fov", fov}})));
        return 0;
    }
    auto line = [](const std::string& a1, const std::string& a2, const std::string& a3, const std::string& a4, const std::string& a5, const std::string& a6) {
        return ljust(a1, 18) + " " + ljust(a2, 13) + " " + ljust(a3, 10) + "  ->  " + ljust(a4, 12) + " " + ljust(a5, 10) + "  " + a6;
    };
    out(line("4:3 value", "", "bits", "16:9 value", "bits", "rule"));
    for (const auto& r : rows) out(line(r.label, r.value, r.bits, r.wide_value, r.wide_bits, r.rule));
    out("\nWidth/height ratios get x4/3, height/width ratios get x3/4. A horizontal projection scale (focal");
    out("length in x) gets x0.75; a vertical one stays. A horizontal FOV angle a becomes 2*atan(tan(a/2)*4/3).");
    if (!fov.is_none())
        out("\nHorizontal FOV " + fmt(fov_deg) + " deg at 4:3 -> " + float_repr(round_n(wide, 4)) + " deg at 16:9 (" + float_repr(round_n(radians(fov_deg), 7)) + " -> " + float_repr(round_n(radians(wide), 7)) + " radians)");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Find a PS2 game's 4:3 aspect and FOV constants and print 16:9 pnach lines to try in PCSX2.");
    p.epilog = "Exit codes: 0 ok, 1 no candidates, 2 bad input.";
    p.subcommands("cmd", false);
    { Parser& s = p.sub("scan"); s.help_line = "list aspect/FOV candidates with 16:9 pnach lines";
      s.pos("elf").help("the game's main ELF");
      s.opt({"--out"}).help("write a .pnach with one [section] per candidate");
      s.opt({"--force"}).store_true().help("overwrite --out if it exists");
      s.opt({"--limit"}).integer().def(Val::integer(60)).help("most candidates (default 60)");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("values"); s.help_line = "the 4:3 -> 16:9 values and the FOV rule";
      s.opt({"--fov"}).number().help("a horizontal FOV in degrees to convert to 16:9");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(std::string(PROG) + ": error", [&]() -> int { return cmd == "scan" ? cmd_scan(a) : cmd_values(a); });
}
PACKTOOL("console-graphics/widescreen", main);
}  // namespace packtools::console_widescreen
