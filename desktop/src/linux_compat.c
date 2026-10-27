/* Lets the Linux builds made on a new distro run on older ones (glibc >= 2.35: Ubuntu 22.04+,
 * Debian 12+, Fedora 36+). GCC 14 + glibc 2.38 headers turn strtol & co. into C23 symbols and
 * pull in arc4random / a newer fmod; these map them back to long-standing glibc symbols.
 * Compile as plain C11 (no _GNU_SOURCE) so strtol below is the classic symbol. */
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>

long __isoc23_strtol(const char* s, char** e, int b) { return strtol(s, e, b); }
long long __isoc23_strtoll(const char* s, char** e, int b) { return strtoll(s, e, b); }
unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return strtoul(s, e, b); }
unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return strtoull(s, e, b); }

uint32_t arc4random(void) {
    uint32_t v = 0;
    while (getrandom(&v, sizeof v, 0) != (ssize_t)sizeof v) {}
    return v;
}

__asm__(".symver fmod_compat, fmod@GLIBC_2.2.5");
double fmod_compat(double, double);
double __wrap_fmod(double x, double y) { return fmod_compat(x, y); }
