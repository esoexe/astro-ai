// Unit tests of packtools/bigdec.h (BigInt, Decimal, Fraction). Expected values are CPython results: decimal in its
// default context (28 digits, ROUND_HALF_EVEN), fractions, and int floor division; built and run by bigdec.sh.
#include "../bigdec.h"
#include <cstdio>
using namespace pyrt;
static int pass = 0, fail = 0;
static void eq(const std::string& got, const std::string& want, const char* what) {
    if (got == want) ++pass; else { ++fail; std::printf("FAIL %s: got [%s] want [%s]\n", what, got.c_str(), want.c_str()); }
}
static Decimal D(const char* s) { return Decimal::parse(s); }
int main() {
    // BigInt
    BigInt a = BigInt::parse("1000000000000000000000000000007"), b = BigInt::parse("1000000000003");
    eq((a / b).str(), "999999999997000000", "big floordiv"); eq((a % b).str(), "9000007", "big mod");
    eq((BigInt::parse("123456789012345678901234567890") * BigInt::parse("987654321098765432109876543210")).str(), "121932631137021795226185032733622923332237463801111263526900", "big mul");
    eq(BigInt::pow10(0).str(), "1", "pow10 0"); eq(BigInt::pow10(18).str(), "1000000000000000000", "pow10 18");
    BigInt p(1); for (int i = 0; i < 100; ++i) p = p.mul_small(2); eq(p.str(), "1267650600228229401496703205376", "2**100");
    BigInt x = -(BigInt::pow10(25)) - BigInt(1), y(999999937);
    eq((x / y).str(), "-10000000630000040", "neg floordiv"); eq((x % y).str(), "309997479", "neg mod");
    eq((BigInt(-7) / BigInt(2)).str(), "-4", "-7//2"); eq((BigInt(-7) % BigInt(2)).str(), "1", "-7%2");
    eq((BigInt(7) / BigInt(-2)).str(), "-4", "7//-2"); eq((BigInt(7) % BigInt(-2)).str(), "-1", "7%-2");
    eq((BigInt(5) - BigInt(5)).str(), "0", "zero"); eq(BigInt(-0LL).str(), "0", "-0 int");
    eq(gcd(BigInt(84), BigInt(-36)).str(), "12", "gcd"); eq(BigInt::parse(" -1_000 ").str(), "-1000", "int() underscores");
    eq(std::to_string(BigInt::parse("999999999999").ndigits()), "12", "ndigits");
    // Decimal parse and str
    eq(D("123.4500").str(), "123.4500", "keeps zeros"); eq(D("1E+2").str(), "1E+2", "sci"); eq(D("0.0000001").str(), "1E-7", "small sci");
    eq(D("0.000001").str(), "0.000001", "small plain"); eq(D("-0").str(), "-0", "neg zero"); eq(D(" 1_000.5 ").str(), "1000.5", "underscore");
    eq(D("1e3").str(), "1E+3", "1e3"); eq(D("12e-1").str(), "1.2", "12e-1"); eq(D(".5").str(), "0.5", ".5"); eq(D("5.").str(), "5", "5.");
    eq(D("inf").str(), "Infinity", "inf"); eq(D("-Infinity").str(), "-Infinity", "-inf"); eq(D("nan").str(), "NaN", "nan");
    eq(D("0E-8").str(), "0E-8", "0E-8"); eq(D("0.00").str(), "0.00", "0.00"); eq(D("100").repr(), "Decimal('100')", "repr");
    try { D("1,000"); eq("no", "throw", "bad syntax"); } catch (const DecimalError& e) { eq(e.type, "decimal.InvalidOperation", "bad syntax type"); }
    try { D(""); eq("no", "throw", "empty"); } catch (const DecimalError&) { ++pass; }
    // arithmetic
    eq((Decimal(1) / Decimal(3)).str(), "0.3333333333333333333333333333", "1/3");
    eq((Decimal(2) / Decimal(3)).str(), "0.6666666666666666666666666667", "2/3");
    eq((Decimal(1) / Decimal(4)).str(), "0.25", "1/4"); eq((Decimal(10) / Decimal(4)).str(), "2.5", "10/4");
    eq((Decimal(100) / Decimal(10)).str(), "10", "100/10"); eq((D("1.00") / Decimal(4)).str(), "0.25", "1.00/4");
    eq((D("6.0") / Decimal(2)).str(), "3.0", "6.0/2"); eq((Decimal(0) / Decimal(5)).str(), "0", "0/5"); eq((D("0.00") / Decimal(5)).str(), "0.00", "0.00/5");
    eq((Decimal(1000) / D("0.5")).str(), "2.00E+3", "1000/0.5 (ideal exponent 1)"); eq((Decimal(-1) / Decimal(8)).str(), "-0.125", "-1/8");
    eq((Decimal(5) * D("1.10")).str(), "5.50", "mul"); eq((D("1.30") + D("1.2")).str(), "2.50", "add"); eq((Decimal(-1) * Decimal(0)).str(), "-0", "-1*0");
    eq((D("2.50") - D("2.5")).str(), "0.00", "x-x"); eq((D("-0") + D("-0")).str(), "-0", "-0+-0"); eq((D("-0") + D("0")).str(), "0", "-0+0");
    eq((D("10000000000000000000000000000") + Decimal(1)).str(), "1.000000000000000000000000000E+28", "add rounds");
    eq((D("0.1") * 3).str(), "0.3", "0.1*3"); eq((-D("0")).str(), "0", "neg 0"); eq((-D("1.50")).str(), "-1.50", "neg");
    eq((D("7") % D("3")).str(), "1", "7%3"); eq((D("-7") % Decimal(3)).str(), "-1", "-7%3"); eq((D("5.5") % Decimal(1)).str(), "0.5", "5.5%1");
    eq((D("12.00") % Decimal(1)).str(), "0.00", "12.00%1"); eq((D("12.00") % Decimal(1)) ? "t" : "f", "f", "bool of 0.00");
    eq((Decimal(19) / Decimal(7)).str(), "2.714285714285714285714285714", "19/7");
    eq((Decimal(4788) * D("0.9")).str(), "4309.2", "4788*0.9");
    try { (void)(Decimal(1) / Decimal(0)); eq("no", "throw", "div0"); } catch (const DecimalError& e) { eq(e.type, "decimal.DivisionByZero", "div0 type"); }
    // quantize and integral
    const Decimal ONE(1), CENT = D("0.01");
    eq(D("1.5").quantize(ONE).str(), "2", "q 1.5 even"); eq(D("2.5").quantize(ONE).str(), "2", "q 2.5 even");
    eq(D("2.5").quantize(ONE, Rounding::HALF_UP).str(), "3", "q 2.5 up"); eq(D("-2.5").quantize(ONE, Rounding::HALF_UP).str(), "-3", "q -2.5 up");
    eq(D("-2.5").quantize(ONE, Rounding::CEILING).str(), "-2", "q -2.5 ceil"); eq(D("2.1").quantize(ONE, Rounding::CEILING).str(), "3", "q 2.1 ceil");
    eq(D("-0.001").quantize(CENT).str(), "-0.00", "q -0.00"); eq(D("5").quantize(CENT).str(), "5.00", "q pad");
    eq(D("1.005").quantize(CENT, Rounding::HALF_UP).str(), "1.01", "q 1.005 up"); eq(D("1.005").quantize(CENT).str(), "1.00", "q 1.005 even");
    eq(D("0.15").quantize(D("0.1"), Rounding::HALF_UP).str(), "0.2", "q 0.15"); eq(D("1234.5").quantize(D("1E+2")).str(), "1.2E+3", "q pos exp");
    eq(D("12.30").to_integral_value().str(), "12", "integral"); eq(D("2.5").to_integral_value().str(), "2", "integral even"); eq(D("1E+2").to_integral_value().str(), "1E+2", "integral pos");
    eq(D("-3.99").to_int().str(), "-3", "int trunc"); eq(D("1E+3").to_int().str(), "1000", "int pos exp");
    // compare
    eq(D("1.0") == Decimal(1) ? "t" : "f", "t", "1.0==1"); eq(D("0.1") < D("0.10001") ? "t" : "f", "t", "lt"); eq(D("-1") < D("-0.5") ? "t" : "f", "t", "neg lt");
    eq(D("-Infinity") < Decimal(-1000000) ? "t" : "f", "t", "-inf lt"); eq(D("nan") == D("nan") ? "t" : "f", "f", "nan ne");
    // format
    eq(D("1234567.5").format(",f"), "1,234,567.5", ",f"); eq(D("1234.5").format(",.2f"), "1,234.50", ",.2f"); eq(D("0.125").format(".2f"), "0.12", ".2f even");
    eq(D("-0.001").format(".2f"), "-0.00", "-0.00 f"); eq(D("1E+3").format(",f"), "1,000", "pos exp f"); eq(D("123").format(","), "123", ",");
    eq(D("1234.50").format(""), "1234.50", "empty spec"); eq(D("1E+3").format(""), "1E+3", "empty spec sci"); eq(D("0.5").format(".0%"), "50%", "pct");
    eq(D("12345678").format(","), "12,345,678", ", int"); eq(D("0.000001").format("f"), "0.000001", "f small"); eq(D("1E-7").format("f"), "0.0000001", "f tiny");
    eq(D("2.675").format(".2f"), "2.68", "2.675 .2f even (exact decimal)");
    eq(std::to_string(D("0.1").to_double()), std::to_string(0.1), "to_double");
    // Decimal(float) and normalize()
    eq(Decimal::from_double(0.1).str(), "0.1000000000000000055511151231257827021181583404541015625", "Decimal(0.1)");
    eq(Decimal::from_double(2.5).str(), "2.5", "Decimal(2.5)"); eq(Decimal::from_double(3.0).str(), "3", "Decimal(3.0)");
    eq(Decimal::from_double(1e20).str(), "100000000000000000000", "Decimal(1e20)"); eq(Decimal::from_double(-0.0).str(), "-0", "Decimal(-0.0)");
    eq(Decimal::from_double(-1.25).str(), "-1.25", "Decimal(-1.25)"); eq(Decimal::from_double(1.0 / 1024).str(), "0.0009765625", "Decimal(2**-10)");
    eq(D("1.500").normalize().str(), "1.5", "normalize 1.500"); eq(D("1000").normalize().str(), "1E+3", "normalize 1000");
    eq(D("0.00").normalize().str(), "0", "normalize 0.00"); eq(D("-0.0").normalize().str(), "-0", "normalize -0.0"); eq(D("120.0").normalize().str(), "1.2E+2", "normalize 120.0");
    eq(D("1000").normalize().format("f"), "1000", "normalized f"); eq(D("12.50").is_integral() ? "t" : "f", "f", "12.50 not integral"); eq(D("12.00").is_integral() ? "t" : "f", "t", "12.00 integral");
    // Fraction
    eq(Fraction(BigInt(6), BigInt(4)).str(), "3/2", "frac reduce"); eq((Fraction(BigInt(1), BigInt(3)) + Fraction(BigInt(1), BigInt(6))).str(), "1/2", "frac add");
    eq(Fraction(BigInt(3), BigInt(-6)).str(), "-1/2", "frac sign"); eq(Fraction(BigInt(-1), BigInt(2)).floor().str(), "-1", "frac floor");
    eq(Fraction(BigInt(5), BigInt(2)).round_half_even().str(), "2", "round 5/2"); eq(Fraction(BigInt(7), BigInt(2)).round_half_even().str(), "4", "round 7/2");
    eq(Fraction(BigInt(-7), BigInt(2)).trunc().str(), "-3", "trunc"); eq(Fraction(BigInt(-7), BigInt(2)).ceil().str(), "-3", "ceil");
    eq((Fraction(10) / Fraction(4)).str(), "5/2", "frac div"); eq(Fraction::from_decimal(D("0.125")).str(), "1/8", "from decimal");
    eq(std::to_string(Fraction(BigInt(1), BigInt(3)).to_double()), std::to_string(1.0 / 3), "frac float");
    eq(Fraction(BigInt(1), BigInt(3)) < Fraction(BigInt(1), BigInt(2)) ? "t" : "f", "t", "frac lt"); eq(Fraction(4).str(), "4", "frac int");
    std::printf("bigdec: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
