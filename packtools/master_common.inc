// Shared helpers for the master-skills ports (master-virality, master-build, master-edit).
// C++ ports of Jakeschincariol/master-skills (Jake Schincariol, MIT), same output and exit codes as the originals.
//
// -h / --help text: each tool prints the original docstring or argparse help unchanged, except that
//   "python3 X.py"  becomes  "astro-server tool master-<skill>/X",
//   a bare "X.py"   becomes  "X",
//   "hooks.json"    becomes  "hook-formulas.json" (the library renames that data file).
// Data files the originals read next to themselves are read from the skill folder at use time with
// skill_file("tools/<name>"), so an edited data file takes effect without a rebuild.
namespace packtools::master {
using namespace pyrt;

// str.strip(chars): drop any of the given code points from both ends
inline std::string strip_chars(const std::string& s, const std::string& chars) {
    const auto set = ucase_detail::u32(chars); const auto cs = cps(s);
    auto in = [&](const std::string& c) { return std::find(set.begin(), set.end(), cp_of(c)) != set.end(); };
    std::size_t a = 0, b = cs.size();
    while (a < b && in(cs[a])) ++a;
    while (b > a && in(cs[b - 1])) --b;
    std::string o; for (std::size_t k = a; k < b; ++k) o += cs[k];
    return o;
}
inline std::string lstrip_ws(const std::string& s) {   // str.lstrip()
    const auto cs = cps(s); std::size_t a = 0;
    while (a < cs.size() && py_space(cp_of(cs[a]))) ++a;
    std::string o; for (std::size_t k = a; k < cs.size(); ++k) o += cs[k];
    return o;
}
inline std::string universal_newlines(const std::string& t) {   // text-mode reading: \r\n and \r become \n
    std::string o; o.reserve(t.size());
    for (std::size_t k = 0; k < t.size(); ++k) {
        if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') ++k; }
        else o += t[k];
    }
    return o;
}
inline std::string read_stdin_text() {   // sys.stdin.read()
    std::string t; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) t.append(buf, n);
    return universal_newlines(t);
}
// os.path.splitext: leading dots of the file name do not start an extension
inline std::pair<std::string, std::string> splitext(const std::string& p) {
    std::size_t sep = p.rfind('/');
#ifdef _WIN32
    const std::size_t bs = p.rfind('\\');
    if (bs != std::string::npos && (sep == std::string::npos || bs > sep)) sep = bs;
#endif
    const std::size_t dot = p.rfind('.');
    const std::size_t base = sep == std::string::npos ? 0 : sep + 1;
    if (dot != std::string::npos && dot >= base) {
        for (std::size_t k = base; k < dot; ++k)
            if (p[k] != '.') return {p.substr(0, dot), p.substr(dot)};
    }
    return {p, ""};
}
// Python iteration over a JSON value (for x in v)
inline List iterate(const Val& v) {
    if (v.is_none()) throw PyErr("TypeError", "'NoneType' object is not iterable");
    return iter(v);
}
// d.get(k) on something that may not be a dict: AttributeError like Python
inline const Dict& as_dict(const Val& v) {
    if (v.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'get'");
    return *v.d;
}
// re.search(p, x) needs a str
inline const std::string& re_text(const Val& v) {
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("expected string or bytes-like object, got '") + type_name(v) + "'");
    return v.s;
}
// dict-key equality and hashability (1 == 1.0 == True; NaN keys never meet)
inline void need_hashable(const Val& v) {
    if (v.t == Val::LIST || v.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(v) + "'");
}
inline bool key_eq(const Val& a, const Val& b) {
    if (a.t == Val::STR || b.t == Val::STR) return a.t == b.t && a.s == b.s;
    if (a.is_none() || b.is_none()) return a.is_none() && b.is_none();
    if (a.big() || b.big()) return a.big() && b.big() && a.s == b.s;
    if (a.t == Val::FLOAT || b.t == Val::FLOAT) return a.num() == b.num();
    const long long x = a.t == Val::BOOL ? a.b : a.i, y = b.t == Val::BOOL ? b.b : b.i;
    return x == y;
}
// value of a Unicode decimal digit (Nd characters come in aligned runs of ten)
inline int digit_value(uint32_t c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    uint32_t s = c; while (s > 0 && uprop::decimal(s - 1)) --s;
    return static_cast<int>((c - s) % 10);
}
inline std::string ascii_digits(const std::string& s) {   // int() of a \d+ match, as a decimal string
    std::string o; for (uint32_t c : ucase_detail::u32(s)) o += static_cast<char>('0' + digit_value(c));
    return o;
}
inline double py_float(const std::string& s) {   // float(str): Unicode decimal digits too, and Python's error text
    std::string t;
    for (uint32_t c : ucase_detail::u32(s)) {
        if (c > 127 && uprop::decimal(c)) t += static_cast<char>('0' + digit_value(c));
        else t += utf8(c);
    }
    const auto r = parse_float(t);
    if (!r) throw PyErr("ValueError", "could not convert string to float: " + str_repr(s));
    return *r;
}
inline double py_mod(double x, double y) {   // float x % y: the remainder takes the divisor's sign
    double m = std::fmod(x, y);
    if (m != 0) {
        if ((y < 0) != (m < 0)) m += y;
    } else m = std::copysign(0.0, y);
    return m;
}
// float divmod(x, y), as CPython computes it
inline double py_divmod(double vx, double wx, double& mod) {
    mod = std::fmod(vx, wx); double div = (vx - mod) / wx;
    if (mod != 0.0) { if ((wx < 0) != (mod < 0)) { mod += wx; div -= 1.0; } }
    else mod = std::copysign(0.0, wx);
    double fl;
    if (div != 0.0) { fl = std::floor(div); if (div - fl > 0.5) fl += 1.0; }
    else fl = std::copysign(0.0, vx / wx);
    return fl;
}
inline void print_help_stderr(const Parser& p) {
    flush(); const std::string t = ap::help_text(ap::norm(p, p.prog));
    std::fwrite(t.data(), 1, t.size(), stderr); std::fflush(stderr);
}
inline int index_of(const std::vector<std::string>& a, const std::string& x) {
    for (std::size_t k = 0; k < a.size(); ++k) if (a[k] == x) return static_cast<int>(k);
    return -1;
}
inline const std::string& arg_after(const std::vector<std::string>& a, int k) {   // a[a.index(flag) + 1]
    if (k + 1 >= static_cast<int>(a.size())) throw PyErr("IndexError", "list index out of range");
    return a[static_cast<std::size_t>(k + 1)];
}
// statistics.mean() of floats: the exact sum divided by n, rounded once. The sum is kept as a Shewchuk expansion
// (non-overlapping doubles, exact), and the quotient is corrected by the exact remainder.
inline std::vector<double> grow_expansion(const std::vector<double>& e, double b) {
    std::vector<double> o; double q = b;
    for (const double x : e) {
        const double s = q + x, bb = s - q, err = (q - (s - bb)) + (x - bb);
        if (err != 0.0) o.push_back(err);
        q = s;
    }
    o.push_back(q);
    return o;
}
inline double expansion_value(const std::vector<double>& e) { double s = 0; for (auto it = e.rbegin(); it != e.rend(); ++it) s += *it; return s; }
inline double stat_mean(const std::vector<double>& xs) {
    if (xs.empty()) throw PyErr("statistics.StatisticsError", "mean requires at least one data point");
    std::vector<double> e; for (const double x : xs) e = grow_expansion(e, x);
    const double n = static_cast<double>(xs.size());
    const double q = expansion_value(e) / n;
    const double p = q * n, perr = std::fma(q, n, -p);
    e = grow_expansion(e, -p); e = grow_expansion(e, -perr);
    return q + expansion_value(e) / n;
}
inline double int_mean(const std::vector<long long>& v) {   // statistics.mean() of ints
    if (v.empty()) throw PyErr("statistics.StatisticsError", "mean requires at least one data point");
    long long s = 0; for (const long long x : v) s += x;
    return static_cast<double>(s) / static_cast<double>(v.size());
}
inline double int_pstdev(const std::vector<long long>& v) {   // statistics.pstdev() of ints (exact sum of squares)
    if (v.empty()) throw PyErr("statistics.StatisticsError", "pstdev requires at least one data point");
    long long s = 0, q = 0; for (const long long x : v) { s += x; q += x * x; }   // word counts: far from overflow
    const long long n = static_cast<long long>(v.size());
    const long long ss = n * q - s * s;   // n^2 * variance, exact
    return std::sqrt(static_cast<double>(ss) / static_cast<double>(n * n));
}
// a slop.json word or phrase as a pattern: \b + re.escape(find) with escaped spaces as \s+ + \b (used with IGNORECASE)
inline std::string slop_pattern(const std::string& find) { return "\\b" + replace_all(re::escape(find), "\\ ", "\\s+") + "\\b"; }
// the fused hook library (tools/hook-formulas.json in the skill folder; hooks.json upstream)
inline Val load_hooks() { return loads(read_text(skill_file("tools/hook-formulas.json"))); }
}  // namespace packtools::master
