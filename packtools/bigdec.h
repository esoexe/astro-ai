// bigdec.h: exact numbers for the pack tools, self-contained (no GMP, no __int128).
//   BigInt    arbitrary-precision integer, Python int semantics (floor division and modulo)
//   Decimal   Python's decimal.Decimal in the default context (28 digits, ROUND_HALF_EVEN), following the
//             algorithms of CPython's _pydecimal (add, mul, true division with its ideal exponent, quantize,
//             to_integral_value, remainder, __str__ and __format__ for the 'f' and default types)
//   Fraction  fractions.Fraction: always reduced, positive denominator
// Exponent limits (Emax/Emin) and signals other than the errors Python raises by default are not modelled:
// the pack tools only see money, counts and percentages.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pyrt {

class BigInt {
public:
    static constexpr std::uint32_t BASE = 1000000000u;   // 9 decimal digits per limb
    BigInt() = default;
    BigInt(long long v) {   // NOLINT: implicit like Python ints
        unsigned long long m = v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
        neg_ = v < 0;
        while (m) { d_.push_back(static_cast<std::uint32_t>(m % BASE)); m /= BASE; }
    }
    static BigInt from_digits(const std::string& s, bool negative = false) {   // s: decimal digits only
        BigInt r;
        std::size_t i = 0; while (i + 1 < s.size() && s[i] == '0') ++i;
        for (std::size_t end = s.size(); end > i;) {
            const std::size_t start = end >= i + 9 ? end - 9 : i;
            r.d_.push_back(static_cast<std::uint32_t>(std::strtoul(s.substr(start, end - start).c_str(), nullptr, 10)));
            end = start;
        }
        r.trim(); r.neg_ = negative && !r.is_zero();
        return r;
    }
    static BigInt parse(const std::string& s) {   // int(s) for plain integers: optional sign, digits, underscores
        std::size_t i = 0; bool n = false;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) n = s[i++] == '-';
        std::string digits;
        for (; i < s.size(); ++i) {
            if (s[i] >= '0' && s[i] <= '9') digits += s[i];
            else if (s[i] == '_' && !digits.empty() && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') continue;
            else break;
        }
        std::size_t j = i; while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r')) ++j;
        if (digits.empty() || j != s.size()) throw std::invalid_argument("invalid literal for int() with base 10: '" + s + "'");
        return from_digits(digits, n);
    }
    static BigInt pow10(long n) {
        BigInt r(1);
        while (n >= 9) { r = r.mul_small(BASE); n -= 9; }
        static const std::uint32_t P[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
        if (n > 0) r = r.mul_small(P[n]);
        return r;
    }

    bool is_zero() const { return d_.empty(); }
    bool negative() const { return neg_; }
    int sign() const { return d_.empty() ? 0 : (neg_ ? -1 : 1); }
    BigInt abs() const { BigInt r = *this; r.neg_ = false; return r; }
    BigInt operator-() const { BigInt r = *this; if (!r.is_zero()) r.neg_ = !r.neg_; return r; }
    bool is_even() const { return d_.empty() || (d_[0] % 2 == 0); }
    std::uint32_t mod_small(std::uint32_t m) const {   // |x| mod m
        unsigned long long r = 0;
        for (std::size_t i = d_.size(); i-- > 0;) r = (r * BASE + d_[i]) % m;
        return static_cast<std::uint32_t>(r);
    }
    std::size_t ndigits() const {   // decimal digits of |x| (0 has 1)
        if (d_.empty()) return 1;
        std::size_t n = (d_.size() - 1) * 9; std::uint32_t top = d_.back();
        while (top) { ++n; top /= 10; }
        return n;
    }
    std::string str() const {
        if (d_.empty()) return "0";
        std::string s = std::to_string(d_.back());
        for (std::size_t i = d_.size() - 1; i-- > 0;) { std::string p = std::to_string(d_[i]); s += std::string(9 - p.size(), '0') + p; }
        return neg_ ? "-" + s : s;
    }
    std::string digits() const { std::string s = str(); return neg_ ? s.substr(1) : s; }
    bool fits_ll() const { return cmp_abs(*this, BigInt(9223372036854775807LL)) <= 0; }
    long long to_ll() const {
        if (!fits_ll()) throw std::overflow_error("Python int too large to convert to C long");
        unsigned long long m = 0;
        for (std::size_t i = d_.size(); i-- > 0;) m = m * BASE + d_[i];
        return neg_ ? -static_cast<long long>(m) : static_cast<long long>(m);
    }
    double to_double() const { return std::strtod(str().c_str(), nullptr); }

    friend int cmp(const BigInt& a, const BigInt& b) {
        if (a.neg_ != b.neg_) return a.neg_ ? -1 : 1;
        const int c = cmp_abs(a, b);
        return a.neg_ ? -c : c;
    }
    friend bool operator==(const BigInt& a, const BigInt& b) { return cmp(a, b) == 0; }
    friend bool operator!=(const BigInt& a, const BigInt& b) { return cmp(a, b) != 0; }
    friend bool operator<(const BigInt& a, const BigInt& b) { return cmp(a, b) < 0; }
    friend bool operator<=(const BigInt& a, const BigInt& b) { return cmp(a, b) <= 0; }
    friend bool operator>(const BigInt& a, const BigInt& b) { return cmp(a, b) > 0; }
    friend bool operator>=(const BigInt& a, const BigInt& b) { return cmp(a, b) >= 0; }

    friend BigInt operator+(const BigInt& a, const BigInt& b) {
        if (a.neg_ == b.neg_) { BigInt r = add_abs(a, b); r.neg_ = a.neg_ && !r.is_zero(); return r; }
        if (cmp_abs(a, b) >= 0) { BigInt r = sub_abs(a, b); r.neg_ = a.neg_ && !r.is_zero(); return r; }
        BigInt r = sub_abs(b, a); r.neg_ = b.neg_ && !r.is_zero(); return r;
    }
    friend BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }
    friend BigInt operator*(const BigInt& a, const BigInt& b) {
        if (a.is_zero() || b.is_zero()) return BigInt();
        BigInt r; r.d_.assign(a.d_.size() + b.d_.size(), 0);
        for (std::size_t i = 0; i < a.d_.size(); ++i) {
            unsigned long long carry = 0;
            for (std::size_t j = 0; j < b.d_.size() || carry; ++j) {
                unsigned long long cur = r.d_[i + j] + carry + (j < b.d_.size() ? static_cast<unsigned long long>(a.d_[i]) * b.d_[j] : 0ull);
                r.d_[i + j] = static_cast<std::uint32_t>(cur % BASE); carry = cur / BASE;
            }
        }
        r.trim(); r.neg_ = (a.neg_ != b.neg_) && !r.is_zero();
        return r;
    }
    BigInt& operator+=(const BigInt& b) { return *this = *this + b; }
    BigInt& operator-=(const BigInt& b) { return *this = *this - b; }
    BigInt& operator*=(const BigInt& b) { return *this = *this * b; }

    // truncating division of magnitudes: |a| = q*|b| + r, 0 <= r < |b|
    static std::pair<BigInt, BigInt> divmod_abs(const BigInt& a, const BigInt& b) {
        if (b.is_zero()) throw std::domain_error("division by zero");
        if (cmp_abs(a, b) < 0) return {BigInt(), a.abs()};
        if (b.d_.size() == 1) {
            BigInt q; q.d_.assign(a.d_.size(), 0); unsigned long long r = 0;
            for (std::size_t i = a.d_.size(); i-- > 0;) { const unsigned long long cur = r * BASE + a.d_[i]; q.d_[i] = static_cast<std::uint32_t>(cur / b.d_[0]); r = cur % b.d_[0]; }
            q.trim(); return {q, BigInt(static_cast<long long>(r))};
        }
        const BigInt bb = b.abs();
        BigInt q; q.d_.assign(a.d_.size(), 0); BigInt r;
        for (std::size_t i = a.d_.size(); i-- > 0;) {
            r.d_.insert(r.d_.begin(), a.d_[i]); r.trim();   // r = r*BASE + limb
            std::uint32_t lo = 0, hi = BASE - 1;            // largest x with bb*x <= r
            while (lo < hi) {
                const std::uint32_t mid = lo + (hi - lo + 1) / 2;
                if (cmp_abs(bb.mul_small(mid), r) <= 0) lo = mid; else hi = mid - 1;
            }
            q.d_[i] = lo;
            if (lo) r = sub_abs(r, bb.mul_small(lo));
        }
        q.trim();
        return {q, r};
    }
    // Python's divmod: floor quotient, remainder with the divisor's sign
    friend std::pair<BigInt, BigInt> divmod(const BigInt& a, const BigInt& b) {
        auto [q, r] = divmod_abs(a, b);
        if (a.neg_ != b.neg_) { q = -q; if (!r.is_zero()) { q = q - BigInt(1); r = b.abs() - r; } }
        if (b.neg_ && !r.is_zero()) r = -r;
        return {q, r};
    }
    friend BigInt operator/(const BigInt& a, const BigInt& b) { return divmod(a, b).first; }   // floor, like //
    friend BigInt operator%(const BigInt& a, const BigInt& b) { return divmod(a, b).second; }
    friend BigInt gcd(BigInt a, BigInt b) {
        a = a.abs(); b = b.abs();
        while (!b.is_zero()) { BigInt t = divmod_abs(a, b).second; a = std::move(b); b = std::move(t); }
        return a;
    }
    BigInt mul_small(std::uint32_t m) const {
        if (m == 0 || is_zero()) return BigInt();
        BigInt r; r.neg_ = neg_; r.d_.reserve(d_.size() + 1); unsigned long long carry = 0;
        for (std::uint32_t x : d_) { const unsigned long long cur = static_cast<unsigned long long>(x) * m + carry; r.d_.push_back(static_cast<std::uint32_t>(cur % BASE)); carry = cur / BASE; }
        while (carry) { r.d_.push_back(static_cast<std::uint32_t>(carry % BASE)); carry /= BASE; }
        return r;
    }

private:
    bool neg_ = false;
    std::vector<std::uint32_t> d_;   // little-endian limbs; empty means zero
    void trim() { while (!d_.empty() && d_.back() == 0) d_.pop_back(); if (d_.empty()) neg_ = false; }
    static int cmp_abs(const BigInt& a, const BigInt& b) {
        if (a.d_.size() != b.d_.size()) return a.d_.size() < b.d_.size() ? -1 : 1;
        for (std::size_t i = a.d_.size(); i-- > 0;) if (a.d_[i] != b.d_[i]) return a.d_[i] < b.d_[i] ? -1 : 1;
        return 0;
    }
    static BigInt add_abs(const BigInt& a, const BigInt& b) {
        BigInt r; const std::size_t n = std::max(a.d_.size(), b.d_.size()); r.d_.reserve(n + 1); std::uint32_t carry = 0;
        for (std::size_t i = 0; i < n || carry; ++i) {
            std::uint64_t cur = carry; if (i < a.d_.size()) cur += a.d_[i]; if (i < b.d_.size()) cur += b.d_[i];
            carry = cur >= BASE; if (carry) cur -= BASE;
            r.d_.push_back(static_cast<std::uint32_t>(cur));
        }
        r.trim(); return r;
    }
    static BigInt sub_abs(const BigInt& a, const BigInt& b) {   // |a| >= |b|
        BigInt r; r.d_ = a.d_; long long borrow = 0;
        for (std::size_t i = 0; i < r.d_.size(); ++i) {
            long long cur = static_cast<long long>(r.d_[i]) - borrow - (i < b.d_.size() ? static_cast<long long>(b.d_[i]) : 0);
            borrow = cur < 0; if (borrow) cur += BASE;
            r.d_[i] = static_cast<std::uint32_t>(cur);
        }
        r.trim(); return r;
    }
};

// ---------------------------------------------------------------- Decimal
enum class Rounding { HALF_EVEN, HALF_UP, HALF_DOWN, UP, DOWN, CEILING, FLOOR };

struct DecimalError : std::runtime_error {   // decimal.InvalidOperation / DivisionByZero, as Python names them
    std::string type;
    DecimalError(std::string t, const std::string& m) : std::runtime_error(m), type(std::move(t)) {}
};

class Decimal {
public:
    enum class Kind { Finite, Inf, NaN };
    static constexpr long PREC = 28;

    Decimal() = default;
    Decimal(long long v) : neg_(v < 0), c_(BigInt(v).abs()) {}   // NOLINT: Decimal(int) is exact
    static Decimal from_int(const BigInt& v) { Decimal d; d.neg_ = v.negative(); d.c_ = v.abs(); return d; }
    static Decimal triple(bool neg, BigInt coeff, long exp) { Decimal d; d.neg_ = neg; d.c_ = std::move(coeff); d.e_ = exp; return d; }
    static Decimal inf(bool neg = false) { Decimal d; d.kind_ = Kind::Inf; d.neg_ = neg; return d; }
    static Decimal nan() { Decimal d; d.kind_ = Kind::NaN; return d; }

    // Decimal(str): exact, with Python's syntax (sign, digits with an optional point, exponent, Infinity, NaN,
    // underscores between digits, surrounding whitespace). Bad text raises InvalidOperation like Python.
    static Decimal parse(const std::string& text) {
        auto bad = [&]() -> Decimal { throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.ConversionSyntax'>]"); };
        std::size_t a = 0, b = text.size();
        auto sp = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'; };
        while (a < b && sp(text[a])) ++a;
        while (b > a && sp(text[b - 1])) --b;
        std::string s;
        for (std::size_t i = a; i < b; ++i) {   // underscores only between digits
            if (text[i] == '_') { if (i == a || i + 1 >= b || !isdig(text[i - 1]) || !isdig(text[i + 1])) return bad(); continue; }
            s += text[i];
        }
        std::size_t i = 0; bool n = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) n = s[i++] == '-';
        std::string rest = lower(s.substr(i));
        if (rest == "inf" || rest == "infinity") return inf(n);
        if (rest == "nan" || rest == "snan") { Decimal d = nan(); d.neg_ = n; return d; }   // payloads are not used
        std::string digits; long frac = 0; bool point = false, any = false;
        for (; i < s.size(); ++i) {
            if (isdig(s[i])) { digits += s[i]; any = true; if (point) ++frac; }
            else if (s[i] == '.' && !point) point = true;
            else break;
        }
        if (!any) return bad();
        long ex = 0;
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i; bool en = false;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) en = s[i++] == '-';
            if (i >= s.size() || !isdig(s[i])) return bad();
            for (; i < s.size() && isdig(s[i]); ++i) { ex = ex * 10 + (s[i] - '0'); if (ex > 999999999L) return bad(); }
            if (en) ex = -ex;
        }
        if (i != s.size()) return bad();
        return triple(n, BigInt::from_digits(digits), ex - frac);
    }

    // Decimal(float): the exact binary value (Decimal.from_float: n/d = as_integer_ratio(), coefficient n*5**k, exponent -k)
    static Decimal from_double(double x) {
        if (x != x) return nan();
        if (x == __builtin_inf() || x == -__builtin_inf()) return inf(x < 0);
        const bool n = std::signbit(x);
        int e2 = 0; double m = std::frexp(n ? -x : x, &e2);
        if (m == 0) return triple(n, BigInt(), 0);
        long long mi = static_cast<long long>(std::ldexp(m, 53)); e2 -= 53;   // x = mi * 2**e2, exactly
        while (e2 < 0 && (mi & 1) == 0) { mi >>= 1; ++e2; }
        if (e2 >= 0) { BigInt c(mi); for (int k = 0; k < e2; ++k) c = c.mul_small(2); return triple(n, c, 0); }
        BigInt c(mi); for (int k = 0; k < -e2; ++k) c = c.mul_small(5);
        return triple(n, c, e2);
    }
    // normalize(): rounded to the context, trailing zeros stripped; a zero becomes 0 with its sign
    Decimal normalize() const {
        if (!is_finite()) return *this;
        Decimal d = fix();
        if (d.c_.is_zero()) return triple(d.neg_, BigInt(), 0);
        while (d.c_.mod_small(10) == 0) { d.c_ = BigInt::divmod_abs(d.c_, BigInt(10)).first; ++d.e_; }
        return d;
    }
    bool is_integral() const { return is_finite() && (e_ >= 0 || to_integral_value() == *this); }

    Kind kind() const { return kind_; }
    bool is_finite() const { return kind_ == Kind::Finite; }
    bool is_nan() const { return kind_ == Kind::NaN; }
    bool is_infinite() const { return kind_ == Kind::Inf; }
    bool is_zero() const { return kind_ == Kind::Finite && c_.is_zero(); }
    bool is_signed() const { return neg_; }
    explicit operator bool() const { return !is_zero(); }
    const BigInt& coeff() const { return c_; }
    long exponent() const { return e_; }
    long adjusted() const { return e_ + static_cast<long>(c_.ndigits()) - 1; }

    std::string str() const {   // Decimal.__str__ (scientific notation when Python uses it)
        if (kind_ == Kind::Inf) return neg_ ? "-Infinity" : "Infinity";
        if (kind_ == Kind::NaN) return neg_ ? "-NaN" : "NaN";
        const std::string ci = c_.digits();
        const long leftdigits = e_ + static_cast<long>(ci.size());
        const long dotplace = (e_ <= 0 && leftdigits > -6) ? leftdigits : 1;
        std::string ip, fp;
        if (dotplace <= 0) { ip = "0"; fp = "." + std::string(static_cast<std::size_t>(-dotplace), '0') + ci; }
        else if (dotplace >= static_cast<long>(ci.size())) { ip = ci + std::string(static_cast<std::size_t>(dotplace - static_cast<long>(ci.size())), '0'); }
        else { ip = ci.substr(0, static_cast<std::size_t>(dotplace)); fp = "." + ci.substr(static_cast<std::size_t>(dotplace)); }
        std::string ex;
        if (leftdigits != dotplace) { const long v = leftdigits - dotplace; ex = std::string("E") + (v >= 0 ? "+" : "-") + std::to_string(v < 0 ? -v : v); }
        return (neg_ ? "-" : "") + ip + fp + ex;
    }
    std::string repr() const { return "Decimal('" + str() + "')"; }
    double to_double() const {
        if (kind_ == Kind::NaN) return neg_ ? -__builtin_nan("") : __builtin_nan("");
        if (kind_ == Kind::Inf) return neg_ ? -__builtin_inf() : __builtin_inf();
        return std::strtod(str().c_str(), nullptr);
    }
    BigInt to_int() const {   // int(d): truncate toward zero
        if (kind_ == Kind::NaN) throw std::invalid_argument("cannot convert NaN to integer");
        if (kind_ == Kind::Inf) throw std::overflow_error("cannot convert Infinity to integer");
        BigInt v = e_ >= 0 ? c_ * BigInt::pow10(e_) : BigInt::divmod_abs(c_, BigInt::pow10(-e_)).first;
        return neg_ ? -v : v;
    }

    Decimal operator-() const {   // __neg__: a zero becomes +0
        if (kind_ == Kind::NaN) return *this;
        Decimal r = *this; r.neg_ = is_zero() ? false : !neg_;
        return r.fix();
    }
    Decimal abs() const { Decimal r = *this; r.neg_ = false; return kind_ == Kind::Finite ? r.fix() : r; }

    friend Decimal operator+(const Decimal& a, const Decimal& b) {
        if (a.is_nan() || b.is_nan()) return nan();
        if (a.is_infinite() || b.is_infinite()) {
            if (a.is_infinite() && b.is_infinite() && a.neg_ != b.neg_) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
            return a.is_infinite() ? a : b;
        }
        const long e = std::min(a.e_, b.e_);
        const BigInt x = signed_coeff(a, e), y = signed_coeff(b, e);
        const BigInt s = x + y;
        Decimal r = triple(s.negative(), s.abs(), e);
        if (s.is_zero()) r.neg_ = a.neg_ && b.neg_;   // ROUND_HALF_EVEN: -0 only from -0 + -0
        return r.fix();
    }
    friend Decimal operator-(const Decimal& a, const Decimal& b) { Decimal nb = b; if (!nb.is_nan()) nb.neg_ = !nb.neg_; return a + nb; }
    friend Decimal operator*(const Decimal& a, const Decimal& b) {
        if (a.is_nan() || b.is_nan()) return nan();
        const bool n = a.neg_ != b.neg_;
        if (a.is_infinite() || b.is_infinite()) {
            if (a.is_zero() || b.is_zero()) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
            return inf(n);
        }
        return triple(n, a.c_ * b.c_, a.e_ + b.e_).fix();
    }
    friend Decimal operator/(const Decimal& a, const Decimal& b) {   // __truediv__
        if (a.is_nan() || b.is_nan()) return nan();
        const bool n = a.neg_ != b.neg_;
        if (a.is_infinite()) { if (b.is_infinite()) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]"); return inf(n); }
        if (b.is_infinite()) return triple(n, BigInt(), -1000000000L);   // 0E-Etiny; never printed by the tools
        if (b.is_zero()) {
            if (a.is_zero()) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.DivisionUndefined'>]");
            throw DecimalError("decimal.DivisionByZero", "[<class 'decimal.DivisionByZero'>]");
        }
        BigInt coeff; long exp;
        if (a.is_zero()) { exp = a.e_ - b.e_; }
        else {
            const long shift = static_cast<long>(b.c_.ndigits()) - static_cast<long>(a.c_.ndigits()) + PREC + 1;
            exp = a.e_ - b.e_ - shift;
            std::pair<BigInt, BigInt> qr = shift >= 0 ? BigInt::divmod_abs(a.c_ * BigInt::pow10(shift), b.c_)
                                                      : BigInt::divmod_abs(a.c_, b.c_ * BigInt::pow10(-shift));
            coeff = std::move(qr.first);
            if (!qr.second.is_zero()) { if (coeff.mod_small(5) == 0) coeff = coeff + BigInt(1); }
            else {
                const long ideal = a.e_ - b.e_;
                while (exp < ideal && coeff.mod_small(10) == 0) { coeff = BigInt::divmod_abs(coeff, BigInt(10)).first; ++exp; }
            }
        }
        return triple(n, std::move(coeff), exp).fix();
    }
    // a % b: Decimal's remainder has the dividend's sign (truncated division), unlike int
    friend Decimal operator%(const Decimal& a, const Decimal& b) {
        if (a.is_nan() || b.is_nan()) return nan();
        if (a.is_infinite()) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
        if (b.is_infinite()) return a.fix();
        if (b.is_zero()) throw DecimalError("decimal.InvalidOperation", a.is_zero() ? "[<class 'decimal.DivisionUndefined'>]" : "[<class 'decimal.InvalidOperation'>]");
        const long e = std::min(a.e_, b.e_);
        const BigInt x = a.c_ * BigInt::pow10(a.e_ - e), y = b.c_ * BigInt::pow10(b.e_ - e);
        const BigInt r = BigInt::divmod_abs(x, y).second;
        return triple(a.neg_, r, e).fix();
    }
    Decimal& operator+=(const Decimal& b) { return *this = *this + b; }
    Decimal& operator-=(const Decimal& b) { return *this = *this - b; }
    Decimal& operator*=(const Decimal& b) { return *this = *this * b; }
    Decimal& operator/=(const Decimal& b) { return *this = *this / b; }

    friend int cmp(const Decimal& a, const Decimal& b) {   // NaN compares as unordered: callers check is_nan first
        if (a.is_nan() || b.is_nan()) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
        if (a.is_infinite() || b.is_infinite()) {
            const int va = a.is_infinite() ? (a.neg_ ? -1 : 1) : 0, vb = b.is_infinite() ? (b.neg_ ? -1 : 1) : 0;
            if (va || vb) { if (va == vb) return 0; if (va) return va; return -vb; }
        }
        const long e = std::min(a.e_, b.e_);
        return cmp(signed_coeff(a, e), signed_coeff(b, e));
    }
    friend bool operator==(const Decimal& a, const Decimal& b) { return !a.is_nan() && !b.is_nan() && cmp(a, b) == 0; }
    friend bool operator!=(const Decimal& a, const Decimal& b) { return !(a == b); }
    friend bool operator<(const Decimal& a, const Decimal& b) { return cmp(a, b) < 0; }
    friend bool operator<=(const Decimal& a, const Decimal& b) { return cmp(a, b) <= 0; }
    friend bool operator>(const Decimal& a, const Decimal& b) { return cmp(a, b) > 0; }
    friend bool operator>=(const Decimal& a, const Decimal& b) { return cmp(a, b) >= 0; }

    Decimal quantize(const Decimal& q, Rounding r = Rounding::HALF_EVEN) const {
        if (is_nan() || q.is_nan()) return nan();
        if (is_infinite() || q.is_infinite()) {
            if (is_infinite() && q.is_infinite()) return *this;
            throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
        }
        Decimal ans = rescale(q.e_, r);
        if (static_cast<long>(ans.c_.ndigits()) > PREC) throw DecimalError("decimal.InvalidOperation", "[<class 'decimal.InvalidOperation'>]");
        return ans;
    }
    Decimal to_integral_value(Rounding r = Rounding::HALF_EVEN) const {
        if (!is_finite() || e_ >= 0) return *this;
        return rescale(0, r);
    }

    // format(d, spec) for specs like "", ",", "f", ",f", ".2f", ",.1f" (and a trailing '%'); others throw
    std::string format(const std::string& spec) const {
        std::size_t i = 0; bool comma = false; long precision = -1; char type = 0;
        if (i < spec.size() && spec[i] == ',') { comma = true; ++i; }
        if (i < spec.size() && spec[i] == '.') {
            ++i; long p = 0; bool got = false;
            while (i < spec.size() && isdig(spec[i])) { p = p * 10 + (spec[i++] - '0'); got = true; }
            if (!got) throw std::invalid_argument("Format specifier missing precision");
            precision = p;
        }
        if (i < spec.size()) type = spec[i++];
        if (i != spec.size() || (type && type != 'f' && type != 'F' && type != '%')) throw std::invalid_argument("unsupported Decimal format: " + spec);
        if (kind_ != Kind::Finite) { std::string s = kind_ == Kind::Inf ? "Infinity" : "NaN"; if (type == 'F') s = kind_ == Kind::Inf ? "INFINITY" : "NAN"; return (neg_ ? "-" : "") + s + (type == '%' ? "%" : ""); }
        Decimal self = *this;
        if (type == '%') { self.e_ += 2; }
        if (type && precision >= 0) self = self.rescale(-precision, Rounding::HALF_EVEN);
        else if (!type && precision >= 0 && static_cast<long>(self.c_.ndigits()) > precision) {   // 'g' with a precision: round to that many digits
            const long p = precision ? precision : 1;
            self = self.round_digits(p);
        }
        if (type && self.is_zero() && self.e_ > 0) self = self.rescale(0, Rounding::HALF_EVEN);
        const std::string ci = self.c_.digits();
        const long leftdigits = self.e_ + static_cast<long>(ci.size());
        long dotplace;
        if (type) dotplace = leftdigits;
        else dotplace = (self.e_ <= 0 && leftdigits > -6) ? leftdigits : 1;
        std::string ip, fp;
        if (dotplace < 0) { ip = "0"; fp = std::string(static_cast<std::size_t>(-dotplace), '0') + ci; }
        else if (dotplace > static_cast<long>(ci.size())) { ip = ci + std::string(static_cast<std::size_t>(dotplace - static_cast<long>(ci.size())), '0'); }
        else { ip = ci.substr(0, static_cast<std::size_t>(dotplace)); if (ip.empty()) ip = "0"; fp = ci.substr(static_cast<std::size_t>(dotplace)); }
        const long ex = leftdigits - dotplace;
        if (comma) { std::string g; const std::size_t n = ip.size(); for (std::size_t k = 0; k < n; ++k) { if (k && (n - k) % 3 == 0) g += ','; g += ip[k]; } ip = g; }
        std::string out = (self.neg_ ? "-" : "") + ip + (fp.empty() ? "" : "." + fp);
        if (!type && ex) out += std::string("E") + (ex >= 0 ? "+" : "-") + std::to_string(ex < 0 ? -ex : ex);
        if (type == '%') out += "%";
        return out;
    }

private:
    Kind kind_ = Kind::Finite;
    bool neg_ = false;
    BigInt c_;      // coefficient, >= 0
    long e_ = 0;    // exponent

    static bool isdig(char ch) { return ch >= '0' && ch <= '9'; }
    static std::string lower(std::string s) { for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a'); return s; }
    static BigInt signed_coeff(const Decimal& d, long e) { BigInt v = d.c_ * BigInt::pow10(d.e_ - e); return d.neg_ ? -v : v; }

    // |c| / 10^shift rounded with mode r; sign matters for CEILING/FLOOR
    static BigInt round_div(const BigInt& c, long shift, Rounding r, bool neg) {
        if (shift <= 0) return c * BigInt::pow10(-shift);
        const BigInt p = BigInt::pow10(shift);
        auto [q, rem] = BigInt::divmod_abs(c, p);
        if (rem.is_zero()) return q;
        const int half = cmp(rem.mul_small(2), p);   // rem vs p/2
        bool up = false;
        switch (r) {
            case Rounding::HALF_EVEN: up = half > 0 || (half == 0 && !q.is_even()); break;
            case Rounding::HALF_UP: up = half >= 0; break;
            case Rounding::HALF_DOWN: up = half > 0; break;
            case Rounding::UP: up = true; break;
            case Rounding::DOWN: up = false; break;
            case Rounding::CEILING: up = !neg; break;
            case Rounding::FLOOR: up = neg; break;
        }
        return up ? q + BigInt(1) : q;
    }
    Decimal rescale(long exp, Rounding r) const {   // _rescale
        if (!is_finite()) return *this;
        if (c_.is_zero()) return triple(neg_, BigInt(), exp);
        if (e_ >= exp) return triple(neg_, c_ * BigInt::pow10(e_ - exp), exp);
        return triple(neg_, round_div(c_, exp - e_, r, neg_), exp);
    }
    Decimal round_digits(long p) const {   // keep p significant digits, ROUND_HALF_EVEN
        const long nd = static_cast<long>(c_.ndigits());
        if (nd <= p) return *this;
        Decimal d = triple(neg_, round_div(c_, nd - p, Rounding::HALF_EVEN, neg_), e_ + (nd - p));
        if (static_cast<long>(d.c_.ndigits()) > p) d = triple(neg_, BigInt::divmod_abs(d.c_, BigInt(10)).first, d.e_ + 1);
        return d;
    }
    Decimal fix() const { return is_finite() ? round_digits(PREC) : *this; }   // _fix without the exponent limits
};

inline Decimal operator+(const Decimal& a, long long b) { return a + Decimal(b); }
inline Decimal operator+(long long a, const Decimal& b) { return Decimal(a) + b; }
inline Decimal operator-(const Decimal& a, long long b) { return a - Decimal(b); }
inline Decimal operator-(long long a, const Decimal& b) { return Decimal(a) - b; }
inline Decimal operator*(const Decimal& a, long long b) { return a * Decimal(b); }
inline Decimal operator*(long long a, const Decimal& b) { return Decimal(a) * b; }
inline Decimal operator/(const Decimal& a, long long b) { return a / Decimal(b); }
inline Decimal operator/(long long a, const Decimal& b) { return Decimal(a) / b; }

// ---------------------------------------------------------------- Fraction
class Fraction {
public:
    Fraction() : n_(0), d_(1) {}
    Fraction(long long n) : n_(n), d_(1) {}   // NOLINT
    Fraction(const BigInt& n) : n_(n), d_(1) {}   // NOLINT
    Fraction(BigInt n, BigInt d) : n_(std::move(n)), d_(std::move(d)) {
        if (d_.is_zero()) throw std::domain_error("Fraction(" + n_.str() + ", 0)");
        norm();
    }
    static Fraction from_decimal(const Decimal& x) {   // Fraction(Decimal): exact
        if (!x.is_finite()) throw std::invalid_argument("cannot convert " + x.str() + " to Fraction");
        BigInt c = x.coeff(); if (x.is_signed()) c = -c;
        return x.exponent() >= 0 ? Fraction(c * BigInt::pow10(x.exponent())) : Fraction(c, BigInt::pow10(-x.exponent()));
    }
    const BigInt& numerator() const { return n_; }
    const BigInt& denominator() const { return d_; }
    bool is_zero() const { return n_.is_zero(); }
    explicit operator bool() const { return !n_.is_zero(); }
    std::string str() const { return d_ == BigInt(1) ? n_.str() : n_.str() + "/" + d_.str(); }
    double to_double() const {   // float(Fraction): n/d correctly rounded through a long decimal expansion
        if (n_.is_zero()) return 0.0;
        const long nd = static_cast<long>(n_.ndigits()), dd = static_cast<long>(d_.ndigits());
        const long shift = 40 - (nd - dd);   // ~40 significant digits, plenty for a double
        BigInt q = shift >= 0 ? (n_.abs() * BigInt::pow10(shift)) / d_ : n_.abs() / (d_ * BigInt::pow10(-shift));
        std::string s = q.str() + "e" + std::to_string(-shift);
        const double v = std::strtod(s.c_str(), nullptr);
        return n_.negative() ? -v : v;
    }
    BigInt floor() const { return n_ / d_; }
    BigInt trunc() const { BigInt q = BigInt::divmod_abs(n_, d_).first; return n_.negative() ? -q : q; }
    BigInt ceil() const { return -((-n_) / d_); }
    BigInt round_half_even() const {   // round(Fraction)
        auto [fl, rem] = divmod(n_, d_);
        const int c = cmp(rem.mul_small(2), d_);
        if (c < 0) return fl;
        if (c > 0) return fl + BigInt(1);
        return fl.is_even() ? fl : fl + BigInt(1);
    }
    Fraction operator-() const { return Fraction(-n_, d_, true); }
    Fraction abs() const { return Fraction(n_.abs(), d_, true); }
    friend Fraction operator+(const Fraction& a, const Fraction& b) { return Fraction(a.n_ * b.d_ + b.n_ * a.d_, a.d_ * b.d_); }
    friend Fraction operator-(const Fraction& a, const Fraction& b) { return Fraction(a.n_ * b.d_ - b.n_ * a.d_, a.d_ * b.d_); }
    friend Fraction operator*(const Fraction& a, const Fraction& b) { return Fraction(a.n_ * b.n_, a.d_ * b.d_); }
    friend Fraction operator/(const Fraction& a, const Fraction& b) {
        if (b.n_.is_zero()) throw std::domain_error("Fraction(" + (a.n_ * b.d_).str() + ", 0)");
        return Fraction(a.n_ * b.d_, a.d_ * b.n_);
    }
    Fraction& operator+=(const Fraction& b) { return *this = *this + b; }
    Fraction& operator-=(const Fraction& b) { return *this = *this - b; }
    Fraction& operator*=(const Fraction& b) { return *this = *this * b; }
    Fraction& operator/=(const Fraction& b) { return *this = *this / b; }
    friend int cmp(const Fraction& a, const Fraction& b) { return cmp(a.n_ * b.d_, b.n_ * a.d_); }
    friend bool operator==(const Fraction& a, const Fraction& b) { return a.n_ == b.n_ && a.d_ == b.d_; }
    friend bool operator!=(const Fraction& a, const Fraction& b) { return !(a == b); }
    friend bool operator<(const Fraction& a, const Fraction& b) { return cmp(a, b) < 0; }
    friend bool operator<=(const Fraction& a, const Fraction& b) { return cmp(a, b) <= 0; }
    friend bool operator>(const Fraction& a, const Fraction& b) { return cmp(a, b) > 0; }
    friend bool operator>=(const Fraction& a, const Fraction& b) { return cmp(a, b) >= 0; }

private:
    BigInt n_, d_;
    Fraction(BigInt n, BigInt d, bool) : n_(std::move(n)), d_(std::move(d)) {}   // already reduced
    void norm() {
        if (d_.negative()) { n_ = -n_; d_ = -d_; }
        const BigInt g = gcd(n_, d_);
        if (!(g == BigInt(1)) && !g.is_zero()) { n_ = n_ / g; d_ = d_ / g; }
    }
};

}  // namespace pyrt
