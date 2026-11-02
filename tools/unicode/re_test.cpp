// re_test.cpp: checks packtools/pyre.h against results CPython 3.13 gives for the same calls (worked out from
// the re and html module rules; each line notes the Python call).
//   g++ -std=c++20 -O1 -I. tools/unicode/re_test.cpp -o /tmp/re_test && /tmp/re_test
#include "packtools/pyre.h"
#include <iostream>
using namespace pyrt;
static int fails = 0, passes = 0;
static void eq(const std::string& got, const std::string& want, const std::string& what) {
    if (got == want) { passes++; return; }
    fails++; std::cout << "FAIL " << what << ": got [" << got << "] want [" << want << "]\n";
}
static std::string J(const std::vector<std::string>& v) { std::string o = "["; for (std::size_t k = 0; k < v.size(); ++k) { if (k) o += "|"; o += v[k]; } return o + "]"; }
static std::string M(const re::Match& m) { return m ? m.group(0) : std::string("None"); }
static std::string err(const std::string& p, int f = 0) { try { re::Regex r(p, f); return "ok"; } catch (const PyErr& e) { return e.what(); } }
int main() {
    eq(M(re::search(R"(\d+)", "ab \u0661\u0662\u0663 cd")), "\u0661\u0662\u0663", "\\d is Unicode decimal");
    eq(M(re::search(R"(\d)", "\u00b2")), "None", "superscript two is a digit but not decimal");
    eq(J(re::findall(R"(\w+)", "h\u00e9llo w\u00f6rld_1 \u00bd")), "[h\u00e9llo|w\u00f6rld_1|\u00bd]", "\\w: alnum incl. numeric (one half)");
    eq(M(re::search(R"(\bsenior\b)", "seniorit\u00e9")), "None", "\\b sees \u00e9 as a word char");
    eq(M(re::search(R"(\bsenior\b)", "senior-dev")), "senior", "\\b at a hyphen");
    eq(M(re::search(R"(\b)", "")), "None", "\\b never matches an empty string");
    eq(M(re::search(R"(\B)", "")), "None", "\\B never matches an empty string");
    eq(M(re::match("(?i)\u017f", "S")), "S", "(?i) long s matches S");
    eq(M(re::match("(?i)s", "\u017f")), "\u017f", "(?i) s matches long s");
    eq(M(re::match("(?i)k", "\u212a")), "\u212a", "(?i) k matches Kelvin sign");
    eq(M(re::match("(?i)[k]", "K")), "K", "(?i)[k]");
    eq(M(re::match("(?i)i", "\u0130")), "\u0130", "(?i) i matches capital I with dot (simple lower is i)");
    eq(M(re::match("(?i)\u00b5", "\u039c")), "\u039c", "(?i) micro sign matches capital mu");
    eq(M(re::match("(?i)[^a-z]", "K")), "None", "(?i) negated class");
    eq(M(re::match("(?i)[a-z]+", "ABC\u212a")), "ABC\u212a", "(?i) range catches Kelvin through lower()");
    eq(M(re::match("[a-z]+", "abC")), "ab", "case-sensitive range");
    eq(M(re::match("(?i)<(script|style).*?</\\1>", "<SCRIPT>x</script> y")), "<SCRIPT>x</script>", "(?i) backreference compares lowercases");
    eq(M(re::match("(?i)<(script).*?</\\1>", "<\u017fcript>x</Script>")), "None", "long s lowercases to itself, not s");
    eq(re::sub("x*", "-", "abxd"), "-a-b--d-", "empty matches next to a non-empty one");
    eq(J(re::split("(,)", "a,b")), "[a|,|b]", "split keeps groups");
    eq(J(re::split(R"(\s*)", "ab c")), "[|a|b||c|]", "split on empty matches (3.7+)");
    eq(M(re::search(R"((?<![a-z0-9])intern(?:s|ship|ships)?(?![a-z0-9]))", " high school internship software ")), "internship", "lookbehind + alternation backtracking");
    eq(M(re::search(R"((?<![a-z0-9])staff(?:s|ship|ships)?(?![a-z0-9]))", " software engineer staffing tools ")), "None", "staffing is not staff");
    eq(M(re::search("a$", "a\n")), "a", "$ before a final newline");
    eq(M(re::search("a\\Z", "a\n")), "None", "\\Z only at the end");
    eq(J(re::findall("^b", "a\nb", re::M)), "[b]", "MULTILINE ^");
    eq(M(re::search("a.c", "a\nc")), "None", ". skips newline");
    eq(M(re::search("(?s)a.c", "a\nc")), "a\nc", "DOTALL");
    eq(re::sub(R"((\w+) (\w+))", R"(\2 \1)", "hello world"), "world hello", "template backrefs");
    eq(re::sub(R"((?P<w>o))", R"([\g<w>\g<0>\g<1>])", "foo"), "f[ooo][ooo]", "\\g<name> \\g<0>");
    eq(re::sub("a", R"(\n\t\\)", "xa"), "x\n\t\\", "template escapes");
    eq(re::sub("(a)|b", R"([\1])", "ab"), "[a][]", "an unmatched group expands to empty");
    { auto rows = re::compile("(a)(b)?").findall_groups("ab a"); eq(std::to_string(rows.size()) + rows[1][0] + "/" + rows[1][1], "2a/", "findall groups"); }
    eq(re::escape("C++ / C# a.b"), "C\\+\\+\\ /\\ C\\#\\ a\\.b", "re.escape (3.7+)");
    eq(M(re::search(re::escape("c++"), "we use c++ daily")), "c++", "an escaped pattern matches literally");
    eq(re::sub(R"([^a-z0-9+#]+)", " ", "c++, c#; go!"), "c++ c# go ", "class with + and #");
    eq(re::sub(R"(\s+)", " ", "a\x1c" "b\u2028c\u00a0d"), "a b c d", "\\s is str.isspace");
    eq(M(re::match(R"((\d{4})-(\d{2})-(\d{2}))", "\u0662\u0660\u0662\u0666-\u0661\u0660-\u0661\u0665")), "\u0662\u0660\u0662\u0666-\u0661\u0660-\u0661\u0665", "dates in Arabic-Indic digits");
    eq(M(re::match("a{,2}", "aaa")), "aa", "{,n}");
    eq(M(re::match("a{2}", "aaa")), "aa", "{n}");
    eq(M(re::match("a{}", "a{}")), "a{}", "{} is literal");
    eq(M(re::match("a{x", "a{x")), "a{x", "{x is literal");
    eq(M(re::match("a{1,2}?", "aaa")), "a", "lazy braces");
    eq(err("a**"), "multiple repeat", "a** is an error");
    eq(err("(?<=a+)b"), "look-behind requires fixed-width pattern", "variable lookbehind");
    eq(err("\\q"), "bad escape \\q", "unknown letter escape");
    eq(err("(a"), "missing ), unterminated subpattern", "unbalanced");
    eq(M(re::search("(?<=ab|cd)e", "xcde")), "e", "fixed-width alternation in lookbehind");
    eq(M(re::search(R"(\$\s?(\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?\s?[kK]?)\s*(?:USD)?\s*(?:-|to)\s*\$)", "pay: $196,000 - $245,000", re::I)), "$196,000 - $", "pay range prefix");
    { auto m = re::search(R"(\$\s?(\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?\s?[kK]?))", "range is $211.4K - x"); eq(m.group(1), "211.4K", "alt B with k"); }
    { auto m = re::search(R"(\$\s?(\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?\s?[kK]?)x)", "$1,0000x"); eq(M(m), "None", "neither alternative is followed by x: no match"); }
    { auto m = re::search(R"(\$(\d{1,3}(?:,\d{3})+|\d+),)", "$1,0000,"); eq(M(m) + "|" + m.group(1), "$1,|1", "alt A fails, alt B takes 1"); }
    eq(M(re::search(R"((\d{1,2})\s*\+?\s*(?:-\s*\d{1,2}\s*)?(?:years|yrs))", "3-5 YEARS", re::I)), "3-5 YEARS", "years range, IGNORECASE");
    eq(M(re::search("(?i)years", "YEAR\u017f")), "YEAR\u017f", "(?i) s in a word matches long s");
    eq(M(re::search(R"(\b(senior|sr\.?)\b)", "Sr. Engineer", re::I)), "Sr", "sr. then \\b: the dot is optional and \\b fails after it");
    eq(M(re::search("(a|ab)(c|bcd)(d*)", "abcd")), "abcd", "leftmost alternatives with backtracking");
    eq(M(re::search("(?x) a  b # comment\n c", "abc")), "abc", "VERBOSE");
    eq(M(re::search("(?i:A)b", "aB")), "None", "scoped flags end with the group");
    eq(M(re::search("(?i:A)b", "ab")), "ab", "scoped flags");
    eq(M(re::search("(?:a|b)+?c", "ababc")), "ababc", "lazy group repeat");
    eq(M(re::search("(a*)*b", "aaab")), "aaab", "nested star");
    eq(M(re::search("(a*)+$", "aaa")), "aaa", "empty iterations stop");
    eq(M(re::search("[\\]a-]+", "a]-x")), "a]-", "escaped ] and trailing - in a class");
    eq(M(re::search("[]a]+", "]a")), "]a", "] first in a class is literal");
    eq(M(re::search("\\x41\\u00e9\\U0001F600", "A\u00e9\U0001F600")), "A\u00e9\U0001F600", "hex escapes");
    eq(M(re::search("(?P<y>\\d+)-(?P=y)", "12-13 12-12")), "12-12", "named backreference");
    // html.unescape
    eq(html_unescape("&amp;lt;"), "&lt;", "one level only");
    eq(html_unescape("&notit;"), "\u00acit;", "longest prefix without ';'");
    eq(html_unescape("&notin;"), "\u2209", "a full name wins");
    eq(html_unescape("&ampx"), "&x", "prefix of a legacy name");
    eq(html_unescape("&#x41;&#65&#0;&#128;&#x110000;&#xD800;&#1;&#13;&#129;"), "AA\ufffd\u20ac\ufffd\ufffd\r\u0081", "numeric references");
    eq(html_unescape("&#;&#x;&#xg"), "&#;&#x;&#xg", "no digits: left alone");
    eq(html_unescape("&nosuch; &Amp; &lt &LT; a&b"), "&nosuch; &Amp; < < a&b", "unknown names stay");
    eq(html_unescape("&NotEqualTilde;&nbsp"), "\u2242\u0338\u00a0", "two code points; nbsp without ';'");
    eq(html_unescape("&#99999999999999999999;"), "\ufffd", "huge numbers");
    eq(html_unescape("&abcdefghijklmnopqrstuvwxyzabcdefghij;"), "&abcdefghijklmnopqrstuvwxyzabcdefghij;", "names stop at 32 characters");
    eq(html_unescape("&lt;p&gt;caf&eacute;&#8212;"), "<p>caf\u00e9\u2014", "typical");
    // str predicates
    eq(std::string(isupper_str("G") ? "T" : "F") + (isupper_str("g") ? "T" : "F") + (isupper_str("\u24b6") ? "T" : "F") + (isupper_str("1") ? "T" : "F") + (isupper_str("\u01c5") ? "T" : "F"), "TFTFF", "str.isupper");
    eq(std::string(isalpha_str("\u00e9a") ? "T" : "F") + (isdecimal_str("\u00b2") ? "T" : "F") + (isnumeric_str("\u00bd") ? "T" : "F") + (isalnum_str("a\u00b2") ? "T" : "F"), "TFTT", "isalpha / isdecimal / isnumeric / isalnum");
    std::cout << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
