// Tiny JSON value + parser + writer for the astro terminal app.
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace aj {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v;
    Value() : v(nullptr) {}
    Value(std::nullptr_t) : v(nullptr) {}
    Value(bool b) : v(b) {}
    Value(int n) : v(static_cast<double>(n)) {}
    Value(double n) : v(n) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(Array a) : v(std::move(a)) {}
    Value(Object o) : v(std::move(o)) {}
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
    const Array* arr() const { return std::get_if<Array>(&v); }
    const Object* obj() const { return std::get_if<Object>(&v); }
    const std::string* str_ptr() const { return std::get_if<std::string>(&v); }
    const Value& operator[](const std::string& k) const {
        static const Value none;
        const Object* o = obj();
        if (!o) return none;
        auto it = o->find(k);
        return it == o->end() ? none : it->second;
    }
    std::string str(const std::string& fallback = "") const {
        if (auto s = str_ptr()) return *s;
        if (auto d = std::get_if<double>(&v)) { std::ostringstream os; if (*d == std::floor(*d) && std::fabs(*d) < 1e15) os << static_cast<long long>(*d); else os << *d; return os.str(); }
        return fallback;
    }
    double num(double fallback = 0) const { if (auto d = std::get_if<double>(&v)) return *d; if (auto s = str_ptr()) { try { return std::stod(*s); } catch (...) {} } return fallback; }
    bool truthy() const { if (auto b = std::get_if<bool>(&v)) return *b; if (auto d = std::get_if<double>(&v)) return *d != 0; if (auto s = str_ptr()) return !s->empty(); return arr() || obj(); }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    Value parse() { ws(); Value v = value(); ws(); if (i_ != s_.size()) throw std::runtime_error("trailing JSON"); return v; }
private:
    const std::string& s_; size_t i_ = 0;
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_; }
    char peek() { if (i_ >= s_.size()) throw std::runtime_error("unexpected end of JSON"); return s_[i_]; }
    Value value() {
        char c = peek();
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return Value(string());
        if (s_.compare(i_, 4, "true") == 0) { i_ += 4; return Value(true); }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return Value(false); }
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return Value(nullptr); }
        size_t st = i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) ++i_;
        if (st == i_) throw std::runtime_error("bad JSON value");
        return Value(std::stod(s_.substr(st, i_ - st)));
    }
    Object object() {
        Object o; ++i_; ws();
        if (peek() == '}') { ++i_; return o; }
        while (true) {
            ws(); std::string k = string(); ws();
            if (peek() != ':') throw std::runtime_error("expected :");
            ++i_; ws();
            o[k] = value(); ws();
            if (peek() == ',') { ++i_; continue; }
            if (peek() == '}') { ++i_; return o; }
            throw std::runtime_error("expected , or }");
        }
    }
    Array array() {
        Array a; ++i_; ws();
        if (peek() == ']') { ++i_; return a; }
        while (true) {
            ws(); a.push_back(value()); ws();
            if (peek() == ',') { ++i_; continue; }
            if (peek() == ']') { ++i_; return a; }
            throw std::runtime_error("expected , or ]");
        }
    }
    static void utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    uint32_t hex4() { if (i_ + 4 > s_.size()) throw std::runtime_error("bad escape"); uint32_t v = static_cast<uint32_t>(std::stoul(s_.substr(i_, 4), nullptr, 16)); i_ += 4; return v; }
    std::string string() {
        if (peek() != '"') throw std::runtime_error("expected string");
        ++i_;
        std::string out;
        while (true) {
            char c = peek(); ++i_;
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            char e = peek(); ++i_;
            switch (e) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                case 'u': { uint32_t cp = hex4(); if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') { i_ += 2; uint32_t lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); } utf8(out, cp); break; }
                default: out += e;
            }
        }
    }
};
inline Value parse(const std::string& s) { return Parser(s).parse(); }
inline bool try_parse(const std::string& s, Value& out) { try { out = parse(s); return true; } catch (...) { return false; } }

inline void write_str(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
            default: if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; } else out += static_cast<char>(c);
        }
    }
    out += '"';
}
inline void write(std::string& out, const Value& v) {
    if (v.is_null()) out += "null";
    else if (auto b = std::get_if<bool>(&v.v)) out += *b ? "true" : "false";
    else if (std::get_if<double>(&v.v)) out += v.str();
    else if (auto s = v.str_ptr()) write_str(out, *s);
    else if (auto a = v.arr()) { out += '['; bool f = true; for (auto& x : *a) { if (!f) out += ','; f = false; write(out, x); } out += ']'; }
    else if (auto o = v.obj()) { out += '{'; bool f = true; for (auto& [k, x] : *o) { if (!f) out += ','; f = false; write_str(out, k); out += ':'; write(out, x); } out += '}'; }
}
inline std::string dump(const Value& v) { std::string s; write(s, v); return s; }

}  // namespace aj
