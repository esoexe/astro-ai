// console_abdiff.inc - console-tester/abdiff: a C++ port of console-skill/console-tester/abdiff.py (Jake Schincariol, MIT).
// `regs A B` compares two register dumps: JSON (name -> value, nested groups flattened) or name=value text (like the
// runtime's trace lines), up to 128 bits. `mem A.bin B.bin --base 0x..` compares two binary memory dumps and prints
// the first divergence and the differing ranges. Exit 0 when they match, 1 when they differ, 2 on bad input.
namespace packtools::console_abdiff {
using namespace pyrt;
using namespace packtools::console_common;

struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };

const char* const GPR_NAMES[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5",
                                   "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1",
                                   "gp", "sp", "fp", "ra"};
inline const std::vector<std::string>& order() {
    static const std::vector<std::string> o = [] { std::vector<std::string> v{"pc"}; for (const char* g : GPR_NAMES) v.push_back(g); for (const char* x : {"hi", "lo", "hi1", "lo1", "sa"}) v.push_back(x); return v; }();
    return o;
}
constexpr std::size_t CHUNK = 1u << 20;

// ---------------------------------------------------------------- 128-bit values (every value is masked to <= 128 bits)
struct U128 {
    uint64_t hi = 0, lo = 0;
    bool operator==(const U128& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const U128& o) const { return !(*this == o); }
    U128 operator|(const U128& o) const { return {hi | o.hi, lo | o.lo}; }
    U128 operator&(const U128& o) const { return {hi & o.hi, lo & o.lo}; }
    U128 operator^(const U128& o) const { return {hi ^ o.hi, lo ^ o.lo}; }
    U128 shl(unsigned n) const {
        if (n >= 128) return {};
        if (n >= 64) return {lo << (n - 64), 0};
        if (n == 0) return *this;
        return {(hi << n) | (lo >> (64 - n)), lo << n};
    }
    U128 neg() const { const uint64_t l = ~lo + 1; return {~hi + (l == 0 ? 1 : 0), l}; }
    void mul_add(unsigned m, unsigned d) {   // *this = *this * m + d (mod 2**128)
        const uint64_t l0 = (lo & 0xFFFFFFFFu) * m + d, l1 = (lo >> 32) * m + (l0 >> 32);
        hi = hi * m + (l1 >> 32);
        lo = (l1 << 32) | (l0 & 0xFFFFFFFFu);
    }
    static U128 mask(int width) { return width >= 128 ? U128{~0ULL, ~0ULL} : width > 64 ? U128{(1ULL << (width - 64)) - 1, ~0ULL} : U128{0, width == 64 ? ~0ULL : (1ULL << width) - 1}; }
    std::string hex(int digits) const {   // "%0*x" % (digits, v)
        std::string h = hi ? cfmt("%llx%016llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo)) : cfmt("%llx", static_cast<unsigned long long>(lo));
        while (static_cast<int>(h.size()) < digits) h = "0" + h;
        return h;
    }
};
inline U128 from_digits(const std::string& s, unsigned base) {   // low 128 bits of an unsigned digit string
    U128 v;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch); unsigned d;
        if (c < 128 && std::isxdigit(static_cast<int>(c))) d = static_cast<unsigned>(std::isdigit(static_cast<int>(c)) ? c - '0' : std::tolower(static_cast<int>(c)) - 'a' + 10);
        else { uint32_t k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k; d = k % 10; }   // a Unicode decimal digit
        v.mul_add(base, d);
    }
    return v;
}

// ---------------------------------------------------------------- registers
inline std::string canonical(const std::string& name) {
    std::string n = lower(strip(name));
    std::size_t k = 0; while (k < n.size() && n[k] == '$') ++k;
    n = n.substr(k);
    static const std::string RX = R"((?:r|gpr)\[?(\d{1,2})\]?)";
    if (const auto m = re::fullmatch(RX, n)) { const long long i = dec_int(m.group(1)); if (i < 32) return GPR_NAMES[i]; }
    if (n == "s8") return "fp";
    if (n == "r0") return "zero";
    return n;
}

inline U128 parse_value(const Val& value, const std::string& where) {
    switch (value.t) {
        case Val::BOOL: throw UsageError(where + ": true/false is not a register value");
        case Val::INT: {
            if (value.big()) {   // an int past 64 bits: its low 128 bits (negatives wrap, as & ((1 << 128) - 1) does)
                const bool negative = !value.s.empty() && value.s[0] == '-';
                const U128 v = from_digits(negative ? value.s.substr(1) : value.s, 10);
                return negative ? v.neg() : v;
            }
            return {value.i < 0 ? ~0ULL : 0ULL, static_cast<uint64_t>(value.i)};   // sign-extended to 128 bits
        }
        case Val::FLOAT: {   // struct.unpack("<I", struct.pack("<f", value))
            const float f = static_cast<float>(value.f);
            if (std::isinf(f) && !std::isinf(value.f)) throw PyErr("OverflowError", "float too large to pack with f format");
            uint32_t bits; std::memcpy(&bits, &f, 4);
            return {0, bits};
        }
        case Val::LIST: {
            U128 total; unsigned i = 0;
            for (const auto& word : *value.l) { total = total | (parse_value(word, where) & U128{0, 0xFFFFFFFFULL}).shl(32 * i); ++i; }
            return total;
        }
        case Val::STR: {
            std::string text = replace_all(replace_all(strip(value.s), "_", ""), " ", "");
            if (lower(text.substr(0, 2)) == "0x") text = text.substr(2);
            static const std::string HEX = "[0-9a-fA-F]+";
            if (text.empty() || !re::fullmatch(HEX, text)) throw UsageError(where + ": " + repr(value) + " is not a hex value");
            return from_digits(text, 16);
        }
        default: break;
    }
    throw UsageError(where + ": unsupported value " + repr(value));
}

struct Item { std::string short_, full; Val value; };
inline void flatten(const Val& obj, std::vector<Item>& out, const std::string& prefix = "") {
    if (obj.t == Val::DICT) {
        for (const auto& [key, value] : obj.d->items) {
            if (value.t == Val::DICT) flatten(value, out, prefix + key + ".");
            else if (value.t == Val::LIST && !value.l->empty() && (*value.l)[0].t == Val::DICT) flatten(value, out, prefix);
            else out.push_back({key, prefix + key, value});
        }
    } else if (obj.t == Val::LIST) {
        for (const auto& item : *obj.l) {
            if (item.t != Val::DICT) continue;
            if (item.d->get("name") && item.d->get("value")) { const std::string nm = pystr(*item.d->get("name")); out.push_back({nm, prefix + nm, *item.d->get("value")}); }
            else flatten(item, out, prefix);
        }
    }
}

using Regs = std::vector<std::pair<std::string, U128>>;   // insertion order, like a dict
inline U128* find(Regs& r, const std::string& n) { for (auto& [k, v] : r) if (k == n) return &v; return nullptr; }
inline const U128* find(const Regs& r, const std::string& n) { for (const auto& [k, v] : r) if (k == n) return &v; return nullptr; }

inline std::string read_text_file(const std::string& path) {
    if (is_dir(path)) throw UsageError("cannot read " + path + ": " + OSError(EISDIR, path).what());
    std::FILE* f = open_file(path, "rb");
    if (!f) {
        if (errno == ENOENT) throw UsageError("no such file: " + path);
        throw UsageError("cannot read " + path + ": " + OSError(errno, path).what());
    }
    std::string raw; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    return text_u(raw);
}

inline Regs load_regs(const std::string& path) {
    const std::string text = read_text_file(path);
    Regs regs;
    const std::string stripped = strip(text);
    std::optional<Val> data;
    if (!stripped.empty() && (stripped[0] == '{' || stripped[0] == '[')) {
        try { data = loads(stripped); }
        catch (const JSONDecodeError& e) {
            if (stripped[0] == '{') {
                const std::string upto = cpslice(stripped, 0, static_cast<long long>(e.pos));
                const long long lineno = static_cast<long long>(std::count(upto.begin(), upto.end(), '\n')) + 1;
                throw UsageError(cfmt("%s looks like JSON but is not valid (line %lld: %s)", path.c_str(), lineno, e.msg.c_str()));
            }
        }
        if (data && data->is_none()) data.reset();
    }
    if (data) {
        std::vector<Item> items; flatten(*data, items);
        std::map<std::string, int> short_counts;
        for (const auto& it : items) ++short_counts[canonical(it.short_)];
        for (const auto& it : items) {
            const std::string c = canonical(it.short_);
            const std::string name = short_counts[c] == 1 ? c : lower(it.full);
            const U128 v = parse_value(it.value, path + ": " + it.full);
            if (U128* slot = find(regs, name)) *slot = v; else regs.emplace_back(name, v);
        }
    } else {
        static const std::string PAIR = R"((?<![\w\[])\$?([A-Za-z][A-Za-z0-9]*)\s*[=:]\s*(0x[0-9a-fA-F_]+|[0-9a-fA-F]{8,32}\b|\d+\b))";
        for (const auto& m : re::compile(PAIR).finditer(text)) {
            const std::string raw = m.group(2), key = canonical(m.group(1));
            bool digits = true; std::size_t len = 0;
            for (const auto& ch : cps(raw)) { ++len; if (!uprop::decimal(cp_of(ch))) digits = false; }
            const U128 v = digits && len < 8 ? U128{0, static_cast<uint64_t>(dec_int(raw))} : parse_value(Val::str(raw), path + ": " + m.group(1));
            if (!find(regs, key)) regs.emplace_back(key, v);
        }
    }
    if (regs.empty()) throw UsageError(path + " has no registers (JSON name -> value, or name=0x... text)");
    return regs;
}

inline std::vector<std::string> sort_names(std::vector<std::string> names) {
    const auto& o = order();
    auto rank = [&](const std::string& n) { const auto it = std::find(o.begin(), o.end(), n); return static_cast<std::size_t>(it - o.begin()); };
    std::sort(names.begin(), names.end(), [&](const std::string& x, const std::string& y) { const auto rx = rank(x), ry = rank(y); return rx != ry ? rx < ry : x < y; });
    return names;
}

inline Val diff_regs(const Regs& a, const Regs& b, int width, const std::optional<std::vector<std::string>>& only, const std::vector<std::string>& ignore) {
    const U128 mask = U128::mask(width);
    auto in = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    std::vector<std::string> common, only_a, only_b;
    for (const auto& [n, v] : a) {
        (void)v;
        if (find(b, n)) { if ((!only || in(*only, n)) && !in(ignore, n)) common.push_back(n); }
        else if (!in(ignore, n)) only_a.push_back(n);
    }
    for (const auto& [n, v] : b) { (void)v; if (!find(a, n) && !in(ignore, n)) only_b.push_back(n); }
    const int digits = std::max(1, width / 4);
    List diffs;
    for (const auto& name : sort_names(common)) {
        const U128 va = *find(a, name) & mask, vb = *find(b, name) & mask;
        if (va != vb) diffs.push_back(dict_of({{"name", Val::str(name)}, {"a", Val::str("0x" + va.hex(digits))}, {"b", Val::str("0x" + vb.hex(digits))}, {"xor", Val::str("0x" + (va ^ vb).hex(digits))}}));
    }
    return dict_of({{"compared", Val::integer(static_cast<long long>(common.size()))}, {"width", Val::integer(width)}, {"differences", Val::list(diffs)},
        {"first", diffs.empty() ? Val::none() : diffs[0]}, {"only_in_a", str_list(sort_names(only_a))}, {"only_in_b", str_list(sort_names(only_b))}});
}

inline int cmd_regs(const Val& a) {
    const long long width = get(a, "width").i;
    if (width != 32 && width != 64 && width != 128) throw UsageError("--width must be 32, 64 or 128");
    const Regs ra = load_regs(S(a, "a")), rb = load_regs(S(a, "b"));
    std::optional<std::vector<std::string>> only; std::vector<std::string> ignore;
    if (!get(a, "only").is_none() && !S(a, "only").empty()) { only.emplace(); for (const auto& n : split(S(a, "only"), ",")) only->push_back(canonical(n)); }
    if (!get(a, "ignore").is_none() && !S(a, "ignore").empty()) for (const auto& n : split(S(a, "ignore"), ",")) ignore.push_back(canonical(n));
    const Val r = diff_regs(ra, rb, static_cast<int>(width), only, ignore);
    if (get(r, "compared").i == 0) throw UsageError(std::string("no register appears in both dumps") + (only || !ignore.empty() ? " (after --only/--ignore)" : ""));
    if (B(a, "json")) out(dumps2(r));
    else {
        out(cfmt("registers: %lld compared at %lld bits (A = %s, B = %s)", get(r, "compared").i, width, S(a, "a").c_str(), S(a, "b").c_str()));
        if (get(r, "differences").l->empty()) out("  identical");
        else {
            const Val f = get(r, "first");
            out("  first divergence: " + get(f, "name").s + "  A=" + get(f, "a").s + "  B=" + get(f, "b").s);
            if (get(f, "name").s == "pc") out("  pc differs: the dumps are not from the same point. Break at the same address on both sides first.");
            for (const auto& d : *get(r, "differences").l) {
                std::string nm = get(d, "name").s; for (std::size_t w = cps(nm).size(); w < 6; ++w) nm += ' ';
                out("  " + nm + " A=" + get(d, "a").s + "  B=" + get(d, "b").s + "  xor=" + get(d, "xor").s);
            }
        }
        std::vector<std::string> oa, ob;
        for (const auto& v : *get(r, "only_in_a").l) oa.push_back(v.s);
        for (const auto& v : *get(r, "only_in_b").l) ob.push_back(v.s);
        if (!oa.empty()) out("  only in A: " + join(oa, ", "));
        if (!ob.empty()) out("  only in B: " + join(ob, ", "));
    }
    return get(r, "differences").l->empty() ? 0 : 1;
}

// ---------------------------------------------------------------- memory
// int(text, 0): optional sign, 0x/0o/0b prefixes, underscores between digits, no leading zeros in decimal
inline std::optional<long long> int0(const std::string& s0) {
    std::string s = strip(s0); bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s = s.substr(1); }
    unsigned base = 10;
    if (s.size() >= 2 && s[0] == '0' && std::strchr("xXoObB", s[1])) { base = std::tolower(static_cast<unsigned char>(s[1])) == 'x' ? 16 : std::tolower(static_cast<unsigned char>(s[1])) == 'o' ? 8 : 2; s = s.substr(2); if (!s.empty() && s[0] == '_') s = s.substr(1); }
    if (s.empty() || s[0] == '_' || s.back() == '_' || s.find("__") != std::string::npos) return std::nullopt;
    unsigned long long v = 0; bool nonzero = false, first_zero = false; std::size_t count = 0;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c == '_') continue;
        unsigned d;
        if (c < 128 && std::isxdigit(static_cast<int>(c))) d = static_cast<unsigned>(std::isdigit(static_cast<int>(c)) ? c - '0' : std::tolower(static_cast<int>(c)) - 'a' + 10);
        else if (base == 10 && uprop::decimal(c)) { uint32_t k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k; d = k % 10; }
        else return std::nullopt;
        if (d >= base) return std::nullopt;
        if (count++ == 0) first_zero = d == 0;
        if (d) nonzero = true;
        if (v > (static_cast<unsigned long long>(LLONG_MAX) - d) / base) return std::nullopt;   // past 63 bits: refused (Python would take it)
        v = v * base + d;
    }
    if (base == 10 && first_zero && nonzero) return std::nullopt;   // "010" is not a base-0 int
    return neg ? -static_cast<long long>(v) : static_cast<long long>(v);
}
inline long long parse_int(const std::string& text, const std::string& what) {
    const auto v = int0(text);
    if (!v) throw UsageError(what + " " + str_repr(text) + " is not a number (use hex like 0x00100000)");
    return *v;
}
inline std::pair<long long, long long> parse_range(const std::string& text) {
    static const std::string RX = R"(\s*(0x[0-9a-fA-F]+|\d+)\s*[-:]\s*(0x[0-9a-fA-F]+|\d+)\s*)";
    const auto m = re::fullmatch(RX, text);
    if (!m) throw UsageError("--ignore-range " + str_repr(text) + " should look like 0x00200000-0x00200100");
    auto num = [](const std::string& g) {
        const auto v = int0(g);
        if (!v) throw PyErr("ValueError", "invalid literal for int() with base 0: " + str_repr(g));
        return *v;
    };
    const long long start = num(m.group(1)), end = num(m.group(2));
    if (end <= start) throw UsageError("--ignore-range " + str_repr(text) + " ends before it starts");
    return {start, end};
}

struct File {
    std::FILE* f = nullptr;
    explicit File(const std::string& path) {
        if (is_dir(path)) throw UsageError(path + " is a folder, not a memory dump");
        f = open_file(path, "rb");
        if (!f) {
            if (errno == ENOENT) throw UsageError("no such file: " + path);
            throw UsageError("cannot read " + path + ": " + OSError(errno, path).what());
        }
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (f) std::fclose(f); }
    std::string read(std::size_t n) { std::string b(n, '\0'); const std::size_t got = std::fread(b.data(), 1, n, f); b.resize(got); return b; }
    std::string read_at(long long off, std::size_t n) {
#if defined(_WIN32)
        _fseeki64(f, off, SEEK_SET);
#else
        fseeko(f, static_cast<off_t>(off), SEEK_SET);
#endif
        return read(n);
    }
};

inline std::string hexs(const std::string& b) { std::string h; for (unsigned char c : b) h += cfmt("%02x", c); return h; }

inline Val diff_mem(const std::string& path_a, const std::string& path_b, long long base, long long gap, long long max_ranges,
                    const std::vector<std::pair<long long, long long>>& ignore, long long context = 16) {
    File fa(path_a), fb(path_b);
    std::vector<std::pair<long long, long long>> ranges;
    long long total_ranges = 0, differing = 0, offset = 0, size_a = 0, size_b = 0;
    std::optional<std::pair<long long, long long>> current;
    std::optional<long long> first;
    auto ignored = [&](long long off) { const long long addr = base + off; for (const auto& [s, e] : ignore) if (s <= addr && addr < e) return true; return false; };
    auto close_current = [&] {
        if (!current) return;
        ++total_ranges;
        if (static_cast<long long>(ranges.size()) < max_ranges) ranges.push_back(*current);
        current.reset();
    };
    auto mark = [&](long long off) {
        if (!ignore.empty() && ignored(off)) return;
        ++differing;
        if (current && off - current->second <= gap) current->second = off + 1;
        else { close_current(); current = std::make_pair(off, off + 1); }
    };
    while (true) {
        const std::string ca = fa.read(CHUNK), cb = fb.read(CHUNK);
        size_a += static_cast<long long>(ca.size()); size_b += static_cast<long long>(cb.size());
        if (ca.empty() && cb.empty()) break;
        const std::size_t n = std::min(ca.size(), cb.size());
        if (std::memcmp(ca.data(), cb.data(), n) != 0) {
            for (std::size_t i = 0; i < n;) {
                const std::size_t end = std::min(i + 64, n);
                if (std::memcmp(ca.data() + i, cb.data() + i, end - i) == 0) { i = end; continue; }   // equal 64-byte blocks skipped
                for (std::size_t j = i; j < end; ++j) {
                    if (ca[j] == cb[j]) continue;
                    const long long off = offset + static_cast<long long>(j);
                    if (!first && !(!ignore.empty() && ignored(off))) first = off;
                    mark(off);
                }
                i = end;
            }
        }
        const std::size_t longer = std::max(ca.size(), cb.size());
        const long long on = offset + static_cast<long long>(n), ol = offset + static_cast<long long>(longer);
        if (longer > n && ignore.empty()) {   // one file is longer: its tail is one difference, marked in one step
            if (!first) first = on;
            differing += static_cast<long long>(longer - n);
            if (current && on - current->second <= gap) current->second = ol;
            else { close_current(); current = std::make_pair(on, ol); }
        } else {
            for (long long j = on; j < ol; ++j) {
                if (!first && !ignored(j)) first = j;
                mark(j);
            }
        }
        offset = ol;
    }
    close_current();

    Val detail = Val::none();
    if (first) {
        const long long start = std::max(0LL, *first - context);
        const std::string blob_a = fa.read_at(start, static_cast<std::size_t>(2 * context)), blob_b = fb.read_at(start, static_cast<std::size_t>(2 * context));
        const long long word_off = *first & ~3LL;
        const std::string wa = fa.read_at(word_off, 4), wb = fb.read_at(word_off, 4);
        const std::size_t at = static_cast<std::size_t>(*first - start);
        detail = dict_of({{"address", Val::str("0x" + hex08(base + *first))}, {"offset", Val::integer(*first)},
            {"a_byte", at < blob_a.size() ? Val::str(cfmt("0x%02x", static_cast<unsigned char>(blob_a[at]))) : Val::none()},
            {"b_byte", at < blob_b.size() ? Val::str(cfmt("0x%02x", static_cast<unsigned char>(blob_b[at]))) : Val::none()},
            {"a_word", wa.size() == 4 ? Val::str("0x" + hex08(u32le(wa, 0))) : Val::none()},
            {"b_word", wb.size() == 4 ? Val::str("0x" + hex08(u32le(wb, 0))) : Val::none()},
            {"word_address", Val::str("0x" + hex08(base + word_off))}, {"context_address", Val::str("0x" + hex08(base + start))},
            {"a_context", Val::str(hexs(blob_a))}, {"b_context", Val::str(hexs(blob_b))}});
    }
    List rl;
    for (const auto& [s, e] : ranges) rl.push_back(dict_of({{"start", Val::str("0x" + hex08(base + s))}, {"end", Val::str("0x" + hex08(base + e))}, {"length", Val::integer(e - s)}}));
    return dict_of({{"a", Val::str(path_a)}, {"b", Val::str(path_b)}, {"base", Val::str("0x" + hex08(base))}, {"size_a", Val::integer(size_a)}, {"size_b", Val::integer(size_b)},
        {"differing_bytes", Val::integer(differing)}, {"range_count", Val::integer(total_ranges)}, {"ranges", Val::list(rl)},
        {"truncated", Val::boolean(total_ranges > static_cast<long long>(ranges.size()))}, {"first", detail}});
}

inline std::string hexdump_line(const std::string& hexstr, long long address) {
    std::vector<std::string> b; for (std::size_t i = 0; i + 1 < hexstr.size(); i += 2) b.push_back(hexstr.substr(i, 2));
    return "0x" + hex08(address) + "  " + join(b, " ");
}

inline int cmd_mem(const Val& a) {
    const long long base = parse_int(S(a, "base"), "--base");
    if (base < 0) throw UsageError("--base must not be negative");
    if (get(a, "gap").i < 0 || get(a, "max_ranges").i < 1) throw UsageError("--gap must be 0 or more and --max-ranges at least 1");
    std::vector<std::pair<long long, long long>> ignore;
    for (const auto& r : *get(a, "ignore_range").l) ignore.push_back(parse_range(r.s));
    const Val r = diff_mem(S(a, "a"), S(a, "b"), base, get(a, "gap").i, get(a, "max_ranges").i, ignore);
    if (B(a, "json")) out(dumps2(r));
    else {
        out(cfmt("memory: A %s (%lld bytes), B %s (%lld bytes), base %s", S(a, "a").c_str(), get(r, "size_a").i, S(a, "b").c_str(), get(r, "size_b").i, get(r, "base").s.c_str()));
        if (get(r, "size_a").i != get(r, "size_b").i) out("  sizes differ: the extra tail counts as different. Dump the same range on both sides.");
        const Val f = get(r, "first");
        if (f.is_none()) out(std::string("  identical") + (ignore.empty() ? "" : " (outside the ignored ranges)"));
        else {
            out("  first divergence at " + pystr(get(f, "address")) + ": A=" + pystr(get(f, "a_byte")) + " B=" + pystr(get(f, "b_byte")) + " (word at " + pystr(get(f, "word_address"))
                + ": A=" + pystr(get(f, "a_word")) + " B=" + pystr(get(f, "b_word")) + ")");
            const long long ctx = std::stoll(get(f, "context_address").s.substr(2), nullptr, 16);
            out("  A " + hexdump_line(get(f, "a_context").s, ctx));
            out("  B " + hexdump_line(get(f, "b_context").s, ctx));
            out(cfmt("  %lld differing byte(s) in %lld range(s)", get(r, "differing_bytes").i, get(r, "range_count").i)
                + (truthy(get(r, "truncated")) ? cfmt(" (first %zu shown)", get(r, "ranges").l->size()) : std::string()) + ":");
            for (const auto& g : *get(r, "ranges").l) out("    " + get(g, "start").s + " - " + get(g, "end").s + cfmt("  %lld bytes", get(g, "length").i));
        }
    }
    return get(r, "first").is_none() ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("abdiff", "A/B compare PCSX2 (A, the original running) against the port (B): register dumps or binary memory dumps. The first divergence is where to look.");
    p.subcommands("cmd", false); p.sub_metavar = "command";
    { Parser& s = p.sub("regs"); s.help_line = "compare two register dumps (JSON or name=value text)";
      s.pos("a").help("side A: PCSX2 (ground truth)"); s.pos("b").help("side B: the port");
      s.opt({"--width"}).integer().def(128).help("compare only the low 32, 64 or 128 bits (default 128; use 32 when the port logs u32)");
      s.opt({"--only"}).help("comma list of registers to compare, like v0,a0,sp,ra");
      s.opt({"--ignore"}).help("comma list of registers to skip, like k0,k1");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("mem"); s.help_line = "compare two binary memory dumps";
      s.pos("a"); s.pos("b");
      s.opt({"--base"}).def("0").help("guest address of the first byte (like 0x00100000)");
      s.opt({"--gap"}).integer().def(16).help("merge differences closer than this many bytes");
      s.opt({"--max-ranges"}).integer().def(50);
      s.opt({"--ignore-range"}).append().def(Val::list({})).metavar("START-END").help("guest address range to ignore (timers, counters); repeatable");
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        return cmd == "regs" ? cmd_regs(a) : cmd_mem(a);
    } catch (const UsageError& e) {
        flush(); std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
PACKTOOL("console-tester/abdiff", main);
}  // namespace packtools::console_abdiff
