// seed_common.inc: helpers shared by the seed pack tools (group 8): dates the way datetime.date checks them, os.walk in
// Python's order, os.stat times in UTC, os.path.splitext, and small Val builders.
#include <chrono>
#include <filesystem>
namespace packtools::seed {
using namespace pyrt;

#ifdef _WIN32
inline constexpr char SEP = '\\';
#else
inline constexpr char SEP = '/';
#endif
inline std::string sep() { return std::string(1, SEP); }
inline std::string join_path(const std::string& a, const std::string& b) {   // os.path.join(a, b)
    if (a.empty()) return b;
    return (a.back() == '/' || a.back() == SEP) ? a + b : a + SEP + b;
}

inline Val sv(const std::string& s) { return Val::str(s); }
inline Val iv(long long v) { return Val::integer(v); }
inline Val bv(bool v) { return Val::boolean(v); }
inline Val obj(std::initializer_list<std::pair<const char*, Val>> kv) { Val d = Val::dict(); for (const auto& [k, v] : kv) d.d->set(k, v); return d; }
// Python's int(), Decimal() and strptime read any Unicode decimal digit (Arabic-Indic, Devanagari, ...): map them to
// ASCII first. Every Nd block is a run of ten starting at its zero, so the value is the offset from the range start.
inline std::string ascii_digits(const std::string& s) {
    std::string o; bool changed = false;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c >= 0x80) {
            const auto* r = std::upper_bound(std::begin(ucase::DECIMAL), std::end(ucase::DECIMAL), c, [](uint32_t x, const ucase::Range& g) { return x < g.lo; });
            if (r != std::begin(ucase::DECIMAL) && c <= (r - 1)->hi) { o += static_cast<char>('0' + (c - (r - 1)->lo) % 10); changed = true; continue; }
        }
        o += ch;
    }
    return changed ? o : s;
}
inline long long to_ll(const std::string& digits) { return std::stoll(ascii_digits(digits)); }

inline Val int_val(const BigInt& b) {   // a Python int for JSON: long long when it fits, else the digits
    if (b.fits_ll() && b >= BigInt(-9223372036854775807LL)) return Val::integer(b.to_ll());
    Val x; x.t = Val::INT; x.s = b.str(); return x;
}
inline BigInt big_of(const Val& v) { return v.big() ? BigInt::parse(v.s) : BigInt(v.i); }   // an INT Val
// Decimal(x) for a JSON value: int exact, float exact (its binary value), str parsed; else ValueError-like
inline Decimal dec_of(const Val& v) {
    if (v.t == Val::INT) return Decimal::from_int(big_of(v));
    if (v.t == Val::BOOL) return Decimal(v.b ? 1 : 0);
    if (v.t == Val::FLOAT) return Decimal::from_double(v.f);
    if (v.t == Val::STR) return Decimal::parse(ascii_digits(v.s));
    throw DecimalError("TypeError", "conversion from " + std::string(v.t == Val::NONE ? "NoneType" : v.t == Val::LIST ? "list" : "dict") + " to Decimal is not supported");
}
inline Val num_val(const Decimal& d) { return d.is_integral() ? int_val(d.to_int()) : Val::flt(d.to_double()); }   // int if whole, else float
inline Val strlist(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(sv(s)); return l; }

// ---- strings
inline std::string strip_chars(const std::string& s, const std::string& chars) {
    const std::size_t a = s.find_first_not_of(chars);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(chars) - a + 1);
}
inline std::string rstrip_chars(const std::string& s, const std::string& chars) {
    const std::size_t b = s.find_last_not_of(chars);
    return b == std::string::npos ? "" : s.substr(0, b + 1);
}
inline std::string squash(const std::string& s) { return strip(re::sub(R"(\s+)", " ", s)); }   // re.sub(r"\s+", " ", s).strip()
inline std::size_t nwords(const std::string& s) {   // len(s.split()) of a squashed string
    std::size_t n = 0; for (const std::string& w : split(squash(s), " ")) if (!w.empty()) ++n;
    return n;
}
inline bool starts(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
// for x in v: list items, a string's characters, a dict's keys (Python iteration); other values raise TypeError
inline List items_of(const Val& v) {
    if (v.t == Val::LIST) return *v.l;
    List o;
    if (v.t == Val::NONE) return o;
    if (v.t == Val::STR) { for (const auto& ch : cps(v.s)) o.push_back(Val::str(ch)); return o; }
    if (v.t == Val::DICT) { for (const auto& kv : v.d->items) o.push_back(Val::str(kv.first)); return o; }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
}

// ---- number formatting
inline std::string group3(const std::string& digits) {   // "1234567" -> "1,234,567"
    std::string g; const std::size_t n = digits.size();
    for (std::size_t k = 0; k < n; ++k) { if (k && (n - k) % 3 == 0) g += ','; g += digits[k]; }
    return g;
}
inline std::string commas(const BigInt& n) { return (n.negative() ? "-" : "") + group3(n.digits()); }   // "{:,}".format(int)
inline std::string float_commas(double x) {   // "{:,}".format(float): repr with the integer part grouped
    std::string r = float_repr(x);
    std::string sign; if (!r.empty() && r[0] == '-') { sign = "-"; r.erase(0, 1); }
    std::size_t k = 0; while (k < r.size() && r[k] >= '0' && r[k] <= '9') ++k;
    return sign + group3(r.substr(0, k)) + r.substr(k);
}
inline BigInt int_of(const Val& v) {   // int(v) for a JSON number (floats truncate)
    if (v.t == Val::INT) return big_of(v);
    if (v.t == Val::BOOL) return BigInt(v.b ? 1 : 0);
    if (v.t == Val::FLOAT) return Decimal::from_double(v.f).to_int();
    if (v.t == Val::STR) return BigInt::parse(ascii_digits(v.s));
    throw PyErr("TypeError", "int() argument must be a string, a bytes-like object or a real number, not '" + std::string(v.t == Val::NONE ? "NoneType" : v.t == Val::LIST ? "list" : "dict") + "'");
}
// Python comparison of two JSON numbers (exact, like int/float in Python)
inline int num_cmp(const Val& a, const Val& b) { return cmp(dec_of(a), dec_of(b)); }

// ---- dates (days since 1970-01-01, like the closer tools)
inline bool leap(long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int month_days(long long y, int m) { static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return m == 2 && leap(y) ? 29 : D[m - 1]; }
inline bool valid_date(long long y, int m, int d) { return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= month_days(y, m); }
inline long long make_day(long long y, int m, int d) { return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) - 719468; }   // days since 1970-01-01
inline std::string iso_day(long long days) { return date_iso(days); }
inline long long today_days() { const std::string t = today_iso(); return make_day(std::stoll(t.substr(0, 4)), std::stoi(t.substr(5, 2)), std::stoi(t.substr(8, 2))); }

// ---- files
inline std::string splitext_root(const std::string& name) {   // os.path.splitext(name)[0] on a base name
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return name;
    std::size_t k = 0; while (k < dot && name[k] == '.') ++k;   // leading dots are not an extension
    return k == dot ? name : name.substr(0, dot);
}
inline long long mtime_utc_day(const std::string& p) {   // datetime.fromtimestamp(os.stat(p).st_mtime, utc).date()
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(fspath(p), ec);
    if (ec) throw OSError(ec.value() ? ec.value() : ENOENT, p);
    const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::file_clock::to_sys(ft).time_since_epoch()).count();
    const long long sec = ns >= 0 ? ns / 1000000000LL : -((-ns + 999999999LL) / 1000000000LL);
    return sec >= 0 ? sec / 86400 : -((-sec + 86399) / 86400);
}
inline unsigned long long file_size(const std::string& p) {
    std::error_code ec; const auto n = std::filesystem::file_size(fspath(p), ec);
    if (ec) throw OSError(ec.value() ? ec.value() : ENOENT, p);
    return n;
}
inline std::string u8name(const std::filesystem::path& p) { const auto s = p.filename().u8string(); return std::string(s.begin(), s.end()); }
inline std::string abspath(const std::string& p) {   // os.path.abspath: absolute and normalized, no trailing separator
    std::error_code ec;
    const auto u = std::filesystem::absolute(fspath(p.empty() ? std::string(".") : p), ec).lexically_normal().u8string();
    std::string s(u.begin(), u.end());
    while (s.size() > (SEP == '/' ? 1u : 3u) && s.back() == SEP) s.pop_back();
    return s;
}
// os.listdir split into (dirs, files), each sorted the way Python sorts str (code points = UTF-8 bytes)
inline void list_dir(const std::string& dir, std::vector<std::string>& dirs, std::vector<std::string>& files) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(fspath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) dirs.push_back(u8name(it->path()));   // a link to a folder is listed; walkers do not enter it
        else files.push_back(u8name(it->path()));
    }
    std::sort(dirs.begin(), dirs.end()); std::sort(files.begin(), files.end());
}

}  // namespace packtools::seed
