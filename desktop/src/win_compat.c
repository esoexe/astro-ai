/* win_compat.c - small C runtime shims for the Windows build (curl's static libraries were built against
   a newer mingw-w64 C runtime that exports fstat64; msvcrt-based toolchains only have _fstat64).
   No headers on purpose: <sys/stat.h> maps fstat64 to _fstat64 with a macro. */
struct _stat64;
__declspec(dllimport) int __cdecl _fstat64(int fd, struct _stat64* st);
int fstat64(int fd, struct _stat64* st) { return _fstat64(fd, st); }
