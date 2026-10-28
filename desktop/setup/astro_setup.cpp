// Astro AI Setup: a custom installer and uninstaller for Windows (per-user, no admin rights).
//
// One executable, two modes:
//   * Installer: the app payload (a zip) and the WebView2 bootstrapper are embedded as resources.
//   * Uninstaller: the same program with the payload stripped out, copied to <install dir>\Uninstall.exe.
//
// The window is fully custom-drawn with GDI+ (no common controls). The left side is a live orbit scene from
// the app icon; while installing, the moon's journey around the orbit IS the progress bar.
//
// Command line (NSIS-compatible where it matters):
//   /S                    silent install / uninstall (no window)
//   /D=<dir>              install folder (must be last, like NSIS)
//   --no-desktop-shortcut skip the desktop shortcut
//   --uninstall           force uninstall mode;  --purge  also delete user data (with /S)
// Environment (testing): ASTRO_SETUP_SKIP_WV2=1 skips the WebView2 runtime step.
//
// Build: desktop/setup/build_setup.sh (called by desktop/package.sh).

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include "miniz.h"
#include "edition.h"   // APP_NAME_W, APP_ID_W, EXE_NAME_W, APP_VERSION_W, EDITION_KEYS (generated per edition)
#include "setup_ids.h"

using namespace Gdiplus;

// ----------------------------------------------------------------------------------------------------------
// Layout (logical 96-dpi pixels) and palette
// ----------------------------------------------------------------------------------------------------------
static const float W = 860, H = 560;
static const float CARD_X = 440, CARD_Y = 60, CARD_W = 396, CARD_H = 476;
static const float X0 = 472, CW = 332;
static const float OCX = 222, OCY = 300, ORX = 160, ORY = 62;
static const float PI = 3.14159265358979f;
static const float TILT = -14.0f * PI / 180.0f;
static const float T0 = 0.75f * PI;            // where the progress journey starts on the orbit

static const uint32_t BG = 0x0a0a0c, PEACH = 0xf3a679, PEACH_HI = 0xffd2b0, PEACH_LO = 0xf08a4b;
static const uint32_t TEXT = 0xf4f4f5, TEXT2 = 0xa6a6ae, TEXT3 = 0x75757e, RED = 0xf87171, GREEN = 0x6ee7b7, AMBER = 0xfbbf24;

enum Page { P_WELCOME, P_WORKING, P_DONE, P_ERROR };
enum HotId { H_NONE = -1, H_CLOSE, H_MIN, H_PRIMARY, H_SECONDARY, H_CHANGE, H_T_DESKTOP, H_T_PURGE, H_LINK, H_COUNT };

// ----------------------------------------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------------------------------------
static HINSTANCE g_inst;
static HWND g_hwnd;
static bool g_uninstall = false, g_silent = false, g_fromTemp = false, g_existing = false;
static bool g_desktopLink = true, g_purge = false;
static std::wstring g_dir, g_selfPath;
static const void* g_payload = nullptr; static size_t g_payloadSize = 0;
static uint64_t g_payloadBytes = 0;          // uncompressed size of the app
static Page g_page = P_WELCOME;
static double g_pageT = 0, g_startT = 0, g_doneT = 0, g_lastT = 0;
static float g_disp = 0;                      // displayed (eased) progress
static float g_hov[H_COUNT] = {0}, g_tog[H_COUNT] = {0};
static int g_hover = H_NONE, g_press = H_NONE;
static float g_dpi = 96;
static float g_moonFreeze = T0;

static std::thread g_worker;
static std::atomic<float> g_prog{0};
static std::atomic<int> g_stage{0}, g_state{0};   // state: 0 running, 1 ok, 2 error
static std::atomic<bool> g_wv2Busy{false};
static std::mutex g_mx;
static std::wstring g_err;
static std::vector<std::pair<float, std::wstring>> g_marks;   // (progress at which a file finished, name)

static ITaskbarList3* g_taskbar = nullptr;
static UINT g_tbMsg = 0;

// rendering resources
static ULONG_PTR g_gdipToken;
static int g_bw = 0, g_bh = 0;
static Bitmap* g_bgBmp = nullptr;
static Bitmap* g_coreBmp = nullptr; static const float CORE_R = 140;
static Bitmap* g_logo = nullptr;
static StringFormat* g_fmt = nullptr;
struct Fam { FontFamily* f = nullptr; };
static Fam F_SANS, F_SEMI, F_SERIF, F_SERIF_I, F_MONO, F_UI;
struct Hot { int id; RectF r; };
static std::vector<Hot> g_hots;

static double now() {
    static LARGE_INTEGER f = {}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) / double(f.QuadPart);
}
static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float easeOut(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
static float easeInOut(float t) { t = clamp01(t); return t < .5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

// ----------------------------------------------------------------------------------------------------------
// Small Win32 helpers
// ----------------------------------------------------------------------------------------------------------
static std::wstring winErr(DWORD e) {
    wchar_t* m = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, (LPWSTR)&m, 0, nullptr);
    std::wstring s = m ? m : L"error " + std::to_wstring(e);
    if (m) LocalFree(m);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
    return s;
}
static std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    CoTaskMemFree(p); return r;
}
static std::wstring envOr(const wchar_t* k) { wchar_t b[2048]; DWORD n = GetEnvironmentVariableW(k, b, 2048); return (n && n < 2048) ? std::wstring(b, n) : L""; }
static std::wstring parentDir(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"" : p.substr(0, i); }
static std::wstring leafName(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
static std::wstring lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
static std::wstring trimSlash(std::wstring s) { while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; }
static bool fileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static bool dirExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static std::wstring utf8w(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0); std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n); return w;
}
static std::wstring regGet(HKEY root, const std::wstring& key, const wchar_t* name) {
    wchar_t buf[1024]; DWORD sz = sizeof(buf);
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) return buf;
    return L"";
}
static bool regSetStr(const std::wstring& key, const wchar_t* name, const std::wstring& v) {
    HKEY h; if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)v.c_str(), DWORD((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h); return r == ERROR_SUCCESS;
}
static bool regSetDword(const std::wstring& key, const wchar_t* name, DWORD v) {
    HKEY h; if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); RegCloseKey(h); return r == ERROR_SUCCESS;
}
// The terminal app (astro.exe / astro-personal.exe) lives in the install folder: put that folder on the user's PATH.
static std::wstring lowerW(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
static void userPathEdit(const std::wstring& dir, bool add) {
    HKEY h; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
    std::vector<wchar_t> buf(32768); DWORD sz = DWORD(buf.size() * sizeof(wchar_t)), type = 0;
    std::wstring cur;
    if (RegQueryValueExW(h, L"Path", nullptr, &type, (BYTE*)buf.data(), &sz) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) cur.assign(buf.data());
    else type = REG_EXPAND_SZ;
    std::vector<std::wstring> parts; bool found = false;
    for (size_t i = 0; i <= cur.size();) {
        size_t j = cur.find(L';', i); if (j == std::wstring::npos) j = cur.size();
        std::wstring p = cur.substr(i, j - i); i = j + 1;
        if (p.empty()) continue;
        std::wstring q = p; while (!q.empty() && (q.back() == L'\\' || q.back() == L'/')) q.pop_back();
        if (lowerW(q) == lowerW(dir)) { found = true; if (!add) continue; }
        parts.push_back(p);
    }
    if (add && !found) parts.push_back(dir);
    if (add == found) { RegCloseKey(h); return; }   // nothing to change
    std::wstring out; for (auto& p : parts) { if (!out.empty()) out += L';'; out += p; }
    RegSetValueExW(h, L"Path", 0, type, (const BYTE*)out.c_str(), DWORD((out.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    DWORD_PTR res; SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &res);
}
static const std::wstring UNINST_KEY = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + APP_ID_W;
static const std::wstring APP_KEY = std::wstring(L"Software\\") + APP_ID_W;

static bool resource(int id, const void** p, size_t* n) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r); if (!g) return false;
    *p = LockResource(g); *n = SizeofResource(nullptr, r); return *p && *n;
}

static bool wv2Present() {
    const wchar_t* guid = L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    const std::wstring keys[] = { std::wstring(L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\") + guid,
                                  std::wstring(L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\") + guid };
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER })
        for (auto& k : keys) { auto v = regGet(root, k, L"pv"); if (!v.empty() && v != L"0.0.0.0") return true; }
    return false;
}

// Close copies of the app that run from the install folder, so their files can be replaced or removed.
static BOOL CALLBACK closeWindowsOf(HWND h, LPARAM pid) {
    DWORD p = 0; GetWindowThreadProcessId(h, &p);
    if (p == (DWORD)pid && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}
static void closeRunning(const std::wstring& dir) {
    std::wstring ldir = lower(trimSlash(dir)) + L"\\";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { sizeof(pe) };
    std::vector<HANDLE> victims;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        std::wstring n = lower(pe.szExeFile);
        if (n != lower(EXE_NAME_W) && n != L"astro-server.exe") continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2]; DWORD sz = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, path, &sz) && lower(path).rfind(ldir, 0) == 0) {
            if (n == lower(EXE_NAME_W)) EnumWindows(closeWindowsOf, (LPARAM)pe.th32ProcessID);
            victims.push_back(h);
        } else CloseHandle(h);
    }
    CloseHandle(snap);
    for (HANDLE h : victims) { if (WaitForSingleObject(h, 1500) == WAIT_TIMEOUT) TerminateProcess(h, 0); WaitForSingleObject(h, 2000); CloseHandle(h); }
}

static bool makeLink(const std::wstring& lnk, const std::wstring& target, const std::wstring& args, const std::wstring& desc) {
    IShellLinkW* sl = nullptr; bool ok = false;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    sl->SetPath(target.c_str()); sl->SetArguments(args.c_str()); sl->SetWorkingDirectory(parentDir(target).c_str());
    sl->SetIconLocation(target.c_str(), 0); sl->SetDescription(desc.c_str());
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release(); return ok;
}

static void deleteTree(const std::wstring& dir, const std::wstring& skip, int* count, int total, float p0, float p1) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) deleteTree(p, skip, count, total, p0, p1); RemoveDirectoryW(p.c_str()); }
            else if (lower(p) != lower(skip)) {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                for (int i = 0; i < 6 && !DeleteFileW(p.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND; i++) Sleep(250);
                if (count) { ++*count; g_prog = p0 + (p1 - p0) * std::min(1.f, float(*count) / float(std::max(1, total)));
                    std::lock_guard<std::mutex> lk(g_mx); g_marks.push_back({ g_prog.load(), n }); }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static int countFiles(const std::wstring& dir) {
    int n = 0; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { std::wstring s = fd.cFileName; if (s == L"." || s == L"..") continue;
         if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += countFiles(dir + L"\\" + s); else n++; } while (FindNextFileW(h, &fd));
    FindClose(h); return n;
}

static void fail(const std::wstring& m) { { std::lock_guard<std::mutex> lk(g_mx); g_err = m; } g_state = 2; }
static void mark(const std::wstring& n) { std::lock_guard<std::mutex> lk(g_mx); g_marks.push_back({ g_prog.load(), n }); }

// ----------------------------------------------------------------------------------------------------------
// Install / uninstall work (runs on a worker thread, or directly in silent mode)
// ----------------------------------------------------------------------------------------------------------
struct WriteCtx { HANDLE h; uint64_t* done; uint64_t total; };
static size_t writeCb(void* op, mz_uint64, const void* buf, size_t n) {
    auto* c = (WriteCtx*)op; DWORD w = 0;
    if (!WriteFile(c->h, buf, (DWORD)n, &w, nullptr) || w != n) return 0;
    *c->done += n; g_prog = 0.04f + 0.76f * float(double(*c->done) / double(std::max<uint64_t>(1, c->total)));
    return n;
}

static void installWork() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring dir = trimSlash(g_dir);
    g_stage = 0; g_prog = 0.01f;
    // Space check (walk up to the first folder that exists).
    std::wstring probe = dir; while (!probe.empty() && !dirExists(probe)) probe = parentDir(probe);
    ULARGE_INTEGER freeB{}; if (!probe.empty() && GetDiskFreeSpaceExW(probe.c_str(), &freeB, nullptr, nullptr) && freeB.QuadPart < g_payloadBytes + (8ull << 20)) {
        fail(L"There isn't enough free space on this drive. Astro AI needs about " + std::to_wstring(g_payloadBytes / (1 << 20) + 8) + L" MB."); return; }
    int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) {
        fail(L"Couldn't create the folder " + dir + L" (" + winErr(r) + L"). Choose a folder inside your user profile."); return; }
    closeRunning(dir);
    g_prog = 0.04f; g_stage = 1;

    mz_zip_archive z; memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_mem(&z, g_payload, g_payloadSize, 0)) { fail(L"The installer is damaged (payload can't be read). Download it again."); return; }
    uint64_t total = 0, done = 0; mz_uint nf = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < nf; i++) { mz_zip_archive_file_stat st; if (mz_zip_reader_file_stat(&z, i, &st) && !st.m_is_directory) total += st.m_uncomp_size; }
    for (mz_uint i = 0; i < nf; i++) {
        mz_zip_archive_file_stat st; if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        std::wstring rel = utf8w(st.m_filename); for (auto& c : rel) if (c == L'/') c = L'\\';
        while (!rel.empty() && rel[0] == L'\\') rel.erase(0, 1);
        if (rel.empty() || rel.find(L"..") != std::wstring::npos || rel.find(L':') != std::wstring::npos) continue;
        std::wstring out = dir + L"\\" + rel;
        if (st.m_is_directory) { SHCreateDirectoryExW(nullptr, trimSlash(out).c_str(), nullptr); continue; }
        SHCreateDirectoryExW(nullptr, parentDir(out).c_str(), nullptr);
        HANDLE h = INVALID_HANDLE_VALUE; DWORD e = 0;
        for (int a = 0; a < 12; a++) {
            h = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) break;
            e = GetLastError(); if (a == 2) closeRunning(dir); Sleep(300);
        }
        if (h == INVALID_HANDLE_VALUE) { mz_zip_reader_end(&z); fail(L"Couldn't write " + rel + L" (" + winErr(e) + L"). Close " APP_NAME_W L" if it's open, then try again."); return; }
        WriteCtx c{ h, &done, total };
        bool ok = mz_zip_reader_extract_to_callback(&z, i, writeCb, &c, 0);
        CloseHandle(h);
        if (!ok) { mz_zip_reader_end(&z); fail(L"Couldn't unpack " + rel + L". The disk may be full, or the installer is damaged."); return; }
        mark(rel);
    }
    mz_zip_reader_end(&z);
    g_prog = 0.80f; g_stage = 2; mark(L"WebView2 runtime");

    if (envOr(L"ASTRO_SETUP_SKIP_WV2").empty() && !wv2Present()) {
        const void* p; size_t n;
        if (resource(IDR_WV2, &p, &n)) {
            g_wv2Busy = true;
            wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
            std::wstring exe = std::wstring(tmp) + L"MicrosoftEdgeWebview2Setup.exe";
            HANDLE h = CreateFileW(exe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD w = 0; bool okw = h != INVALID_HANDLE_VALUE && WriteFile(h, p, (DWORD)n, &w, nullptr) && w == n;
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (okw) {
                std::wstring cmd = L"\"" + exe + L"\" /silent /install";
                STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    for (int t = 0; t < 600 && WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT; t++)
                        g_prog = 0.80f + 0.09f * (1 - expf(-t / 40.f));
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                }
            }
            DeleteFileW(exe.c_str()); g_wv2Busy = false;
        }
    }
    g_prog = 0.90f; g_stage = 3;

    std::wstring exe = dir + L"\\" EXE_NAME_W, unexe = dir + L"\\Uninstall.exe";
    std::wstring sm = knownFolder(FOLDERID_Programs) + L"\\" APP_NAME_W;
    SHCreateDirectoryExW(nullptr, sm.c_str(), nullptr);
    makeLink(sm + L"\\" APP_NAME_W L".lnk", exe, L"", APP_NAME_W);           mark(L"Start menu shortcut");
    makeLink(sm + L"\\Uninstall " APP_NAME_W L".lnk", unexe, L"", L"Remove " APP_NAME_W);
    std::wstring desk = knownFolder(FOLDERID_Desktop) + L"\\" APP_NAME_W L".lnk";
    if (g_desktopLink) { makeLink(desk, exe, L"", APP_NAME_W); mark(L"Desktop shortcut"); } else DeleteFileW(desk.c_str());
    g_prog = 0.96f; g_stage = 4;

    // The uninstaller (this program built without a payload) ships inside the payload zip.
    if (!fileExists(unexe)) { fail(L"The installer is incomplete (Uninstall.exe is missing). Download it again."); return; }
    mark(L"Uninstall.exe");
    regSetStr(APP_KEY, L"InstallDir", dir);
    regSetStr(UNINST_KEY, L"DisplayName", APP_NAME_W);
    regSetStr(UNINST_KEY, L"DisplayVersion", APP_VERSION_W);
    regSetStr(UNINST_KEY, L"Publisher", L"Astro AI");
    regSetStr(UNINST_KEY, L"DisplayIcon", exe);
    regSetStr(UNINST_KEY, L"InstallLocation", dir);
    regSetStr(UNINST_KEY, L"UninstallString", L"\"" + unexe + L"\"");
    regSetStr(UNINST_KEY, L"QuietUninstallString", L"\"" + unexe + L"\" /S");
    regSetDword(UNINST_KEY, L"EstimatedSize", DWORD(g_payloadBytes / 1024 + 1024));
    regSetDword(UNINST_KEY, L"NoModify", 1);
    regSetDword(UNINST_KEY, L"NoRepair", 1);
    // astroai:// links from the website ("Open in Astro AI" in the Library). The app only accepts
    // astroai://library/install?id=<catalog id> and always shows what an item does before installing it.
    regSetStr(L"Software\\Classes\\astroai", nullptr, L"URL:Astro AI");
    regSetStr(L"Software\\Classes\\astroai", L"URL Protocol", L"");
    regSetStr(L"Software\\Classes\\astroai\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    regSetStr(L"Software\\Classes\\astroai\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
    mark(L"Registered in Installed apps");
    userPathEdit(dir, true); mark(L"Terminal app (astro) added to your PATH");
    g_prog = 1.0f; g_state = 1;
    CoUninitialize();
}

static void uninstallWork() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring dir = trimSlash(g_dir);
    g_stage = 0; g_prog = 0.02f;
    closeRunning(dir);
    g_prog = 0.08f; g_stage = 1;
    int total = countFiles(dir), count = 0;
    deleteTree(dir, g_selfPath, &count, total, 0.08f, 0.82f);
    for (int a = 0; a < 10 && !RemoveDirectoryW(dir.c_str()) && dirExists(dir); a++) {
        if (a == 3 && fileExists(dir + L"\\Uninstall.exe")) {
            // The original Uninstall.exe is still running (it waits for us in silent mode): remove it once it exits.
            std::wstring cmd = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + dir + L"\\Uninstall.exe\" & rmdir \"" + dir + L"\"";
            STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi{}; wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
            if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
            break;
        }
        Sleep(300);
    }
    g_prog = 0.84f; g_stage = 2;
    DeleteFileW((knownFolder(FOLDERID_Desktop) + L"\\" APP_NAME_W L".lnk").c_str());
    std::wstring sm = knownFolder(FOLDERID_Programs) + L"\\" APP_NAME_W;
    deleteTree(sm, L"", nullptr, 0, 0, 0); RemoveDirectoryW(sm.c_str()); mark(L"Shortcuts");
    g_prog = 0.9f; g_stage = 3;
    RegDeleteTreeW(HKEY_CURRENT_USER, UNINST_KEY.c_str()); RegDeleteTreeW(HKEY_CURRENT_USER, APP_KEY.c_str()); mark(L"Installed apps entry");
    userPathEdit(dir, false);
    // Remove the astroai:// handler only if it still points at this install (the other edition may own it).
    if (lower(regGet(HKEY_CURRENT_USER, L"Software\\Classes\\astroai\\shell\\open\\command", nullptr)).find(lower(dir)) != std::wstring::npos)
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\astroai");
    if (g_purge) {
        std::wstring ra = knownFolder(FOLDERID_RoamingAppData), la = knownFolder(FOLDERID_LocalAppData);
        for (std::wstring d : { ra + L"\\" APP_NAME_W, ra + L"\\" EXE_NAME_W, la + L"\\" EXE_NAME_W }) {
            if (!dirExists(d)) continue; deleteTree(d, L"", nullptr, 0, 0, 0); RemoveDirectoryW(d.c_str()); }
        mark(L"Your data");
    }
    g_prog = 1.0f; g_stage = 4; g_state = 1;
    CoUninitialize();
}

static void startWork() {
    g_prog = 0; g_stage = 0; g_state = 0; g_disp = 0; g_wv2Busy = false;
    { std::lock_guard<std::mutex> lk(g_mx); g_marks.clear(); g_err.clear(); }
    if (g_worker.joinable()) g_worker.join();
    g_page = P_WORKING; g_pageT = now();
    g_worker = std::thread(g_uninstall ? uninstallWork : installWork);
}

// ----------------------------------------------------------------------------------------------------------
// Drawing helpers
// ----------------------------------------------------------------------------------------------------------
static float g_alpha = 1;   // multiplies every colour drawn (page fades)
static Color col(float a, uint32_t rgb) { int A = (int)lroundf(std::max(0.f, std::min(255.f, a * g_alpha))); return Color((BYTE)A, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { return (uint32_t)lroundf(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) << s; };
    return ch(16) | ch(8) | ch(0);
}
static Font* font(Fam& fam, float px, int style = FontStyleRegular) {
    static std::map<std::tuple<void*, int, int>, Font*> cache;
    auto k = std::make_tuple((void*)fam.f, (int)lroundf(px * 10), style);
    auto it = cache.find(k); if (it != cache.end()) return it->second;
    Font* f = new Font(fam.f, px, style, UnitPixel); cache[k] = f; return f;
}
static float textW(Graphics& g, const std::wstring& s, Font* f) {
    RectF r; g.MeasureString(s.c_str(), (INT)s.size(), f, PointF(0, 0), g_fmt, &r); return r.Width;
}
static void text(Graphics& g, const std::wstring& s, Font* f, float x, float y, Color c) {
    SolidBrush b(c); g.DrawString(s.c_str(), (INT)s.size(), f, PointF(x, y), g_fmt, &b);
}
static void textBox(Graphics& g, const std::wstring& s, Font* f, RectF r, Color c, StringAlignment al = StringAlignmentNear, bool ellipsisPath = false) {
    StringFormat* sf = g_fmt->Clone(); sf->SetAlignment(al);
    if (ellipsisPath) { sf->SetTrimming(StringTrimmingEllipsisPath); sf->SetFormatFlags(sf->GetFormatFlags() | StringFormatFlagsNoWrap); }
    else sf->SetTrimming(StringTrimmingWord);
    SolidBrush b(c); g.DrawString(s.c_str(), (INT)s.size(), f, r, sf, &b); delete sf;
}
// Shorten a path from the middle so the drive and the last folders stay visible: C:\Users\…\Programs\Astro AI
static std::wstring fitPath(Graphics& g, const std::wstring& s, Font* f, float w) {
    if (textW(g, s, f) <= w) return s;
    std::vector<std::wstring> parts; size_t a = 0;
    for (size_t i = 0; i <= s.size(); i++) if (i == s.size() || s[i] == L'\\' || s[i] == L'/') { parts.push_back(s.substr(a, i - a)); a = i + 1; }
    for (size_t k = 1; k + 1 < parts.size(); k++) {
        std::wstring t = parts[0] + L"\\\u2026";
        for (size_t j = k + 1; j < parts.size(); j++) t += L"\\" + parts[j];
        if (textW(g, t, f) <= w) return t;
    }
    std::wstring t = s; while (t.size() > 4 && textW(g, L"\u2026" + t, f) > w) t.erase(0, 1);
    return L"\u2026" + t;
}
static float tracked(Graphics& g, const std::wstring& s, Font* f, float x, float y, Color c, float tr) {
    for (wchar_t ch : s) { std::wstring one(1, ch); text(g, one, f, x, y, c); x += textW(g, one, f) + tr; }
    return x;
}
static float trackedW(Graphics& g, const std::wstring& s, Font* f, float tr) { float w = 0; for (wchar_t ch : s) w += textW(g, std::wstring(1, ch), f) + tr; return w - tr; }
static void roundRect(GraphicsPath& p, RectF r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90); p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90); p.CloseFigure();
}
static void fillRound(Graphics& g, RectF r, float rad, const Brush& b) { GraphicsPath p; roundRect(p, r, rad); g.FillPath(&b, &p); }
static void strokeRound(Graphics& g, RectF r, float rad, Color c, float w = 1) { GraphicsPath p; roundRect(p, r, rad); Pen pen(c, w); g.DrawPath(&pen, &p); }
static void glow(Graphics& g, float cx, float cy, float r, Color c) {
    if (r < 1 || c.GetA() == 0) return;
    GraphicsPath p; p.AddEllipse(cx - r, cy - r, 2 * r, 2 * r);
    PathGradientBrush b(&p); b.SetCenterPoint(PointF(cx, cy)); b.SetCenterColor(c);
    Color sc(0, c.GetR(), c.GetG(), c.GetB()); INT one = 1; b.SetSurroundColors(&sc, &one);
    REAL fac[] = { 0, .05f, .22f, .55f, 1 }, pos[] = { 0, .25f, .5f, .75f, 1 }; b.SetBlend(fac, pos, 5);
    g.FillPath(&b, &p);
}
static void hot(int id, RectF r) { g_hots.push_back({ id, r }); }
static void arrow(Graphics& g, float x, float y, float s, Color c, bool diag = false) {
    Pen p(c, 1.6f); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    if (!diag) { g.DrawLine(&p, x, y, x + s, y); PointF h[3] = { {x + s - 4.5f, y - 4.5f}, {x + s, y}, {x + s - 4.5f, y + 4.5f} }; g.DrawLines(&p, h, 3); }
    else { g.DrawLine(&p, x, y + s, x + s, y); PointF h[3] = { {x + s - 5, y}, {x + s, y}, {x + s, y + 5} }; g.DrawLines(&p, h, 3); }
}

// ----------------------------------------------------------------------------------------------------------
// The orbit scene
// ----------------------------------------------------------------------------------------------------------
// A drawing surface: a top-down 32-bit DIB that GDI can blit and GDI+ can draw into directly (fast on Windows and wine).
struct Surf { HDC dc = nullptr; HBITMAP bmp = nullptr, old = nullptr; void* bits = nullptr; Bitmap* gb = nullptr; int w = 0, h = 0; };
static Surf g_bgS, g_layerS, g_memS;
static void surfFree(Surf& s) { delete s.gb; s.gb = nullptr; if (s.dc) { SelectObject(s.dc, s.old); DeleteObject(s.bmp); DeleteDC(s.dc); } s = Surf(); }
static void surfMake(Surf& s, int w, int h) {
    surfFree(s);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    s.dc = CreateCompatibleDC(nullptr); s.bmp = CreateDIBSection(s.dc, &bi, DIB_RGB_COLORS, &s.bits, nullptr, 0);
    s.old = (HBITMAP)SelectObject(s.dc, s.bmp); s.w = w; s.h = h;
    s.gb = new Bitmap(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)s.bits);
}
static void surfCopy(Surf& dst, const Surf& src) { if (dst.bits && src.bits && dst.w == src.w && dst.h == src.h) memcpy(dst.bits, src.bits, size_t(src.w) * src.h * 4); }

static PointF orb(float t, float s = 1) {
    float x = ORX * s * cosf(t), y = ORY * s * sinf(t);
    return PointF(OCX + x * cosf(TILT) - y * sinf(TILT), OCY + x * sinf(TILT) + y * cosf(TILT));
}
static bool isFront(float t) { return sinf(t) > 0; }
static void arc(Graphics& g, float a, float b, bool front, float w, Color c, float s = 1) {
    if (b - a < 0.002f || c.GetA() == 0) return;
    int n = std::max(2, (int)((b - a) / (2 * PI) * 240) + 1);
    Pen pen(c, w); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    std::vector<PointF> run; bool cur = isFront(a);
    for (int i = 0; i <= n; i++) {
        float t = a + (b - a) * i / n; PointF p = orb(t, s); bool f = isFront(t);
        if (f != cur) { run.push_back(p); if (cur == front && run.size() > 1) g.DrawLines(&pen, run.data(), (INT)run.size()); run.clear(); cur = f; }
        run.push_back(p);
    }
    if (cur == front && run.size() > 1) g.DrawLines(&pen, run.data(), (INT)run.size());
}

struct Star { float x, y, r, a, sp, ph; };
static std::vector<Star> g_stars;
static void makeStars() {
    uint32_t s = 0x9e3779b9u; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
    for (int i = 0; i < 150; i++) {
        Star st; st.x = rnd() * (CARD_X - 12); st.y = 44 + rnd() * (H - 60);
        float big = rnd(); st.r = big > .93f ? 1.25f + rnd() * .5f : .45f + rnd() * .6f;
        st.a = 60 + rnd() * 150; st.sp = .6f + rnd() * 2.2f; st.ph = rnd() * 6.28f; g_stars.push_back(st);
    }
}

// Back-half pieces must pass behind the core: clip the core's disc out while drawing them.
static void excludeCore(Graphics& g) { GraphicsPath p; p.AddEllipse(OCX - 29.5f, OCY - 29.5f, 59.f, 59.f); Region r(&p); g.ExcludeClip(&r); }
static float introOf(double t) { return easeOut(float((t - g_startT) / 1.4)); }

// Parts of the scene that don't change frame to frame (baked into the cached layer): base ring, core, caption.
static void drawSceneStatic(Graphics& g, double t) {
    g_alpha = easeOut(float((t - g_startT) / 0.8));
    bool ringBaked = introOf(t) >= 0.999f;
    if (ringBaked) arc(g, T0, T0 + 2 * PI, false, 1.3f, col(57, PEACH));
    if (g_coreBmp) {
        float s = g_dpi / 96.f; GraphicsState cs = g.Save(); g.ResetTransform();
        Rect dst((INT)lroundf((OCX - CORE_R) * s), (INT)lroundf((OCY - CORE_R) * s), (INT)g_coreBmp->GetWidth(), (INT)g_coreBmp->GetHeight());
        if (g_alpha >= 0.999f) g.DrawImage(g_coreBmp, dst.X, dst.Y, dst.Width, dst.Height);
        else {
            ColorMatrix cm = { { {1,0,0,0,0}, {0,1,0,0,0}, {0,0,1,0,0}, {0,0,0,g_alpha,0}, {0,0,0,0,1} } };
            ImageAttributes ia; ia.SetColorMatrix(&cm);
            g.DrawImage(g_coreBmp, dst, 0, 0, (INT)g_coreBmp->GetWidth(), (INT)g_coreBmp->GetHeight(), UnitPixel, &ia);
        }
        g.Restore(cs);
    }
    if (ringBaked) arc(g, T0, T0 + 2 * PI, true, 1.3f, col(119, PEACH));
    g_alpha = easeOut(float((t - g_startT - .4) / 0.8));
    tracked(g, std::wstring(L"V") + APP_VERSION_W + L"  \u00B7  WINDOWS X64", font(F_MONO, 10), 30, H - 40, col(110, TEXT3), 1.6f);
    g_alpha = 1;
}

// Tiny stars: blended directly into the frame memory, much cheaper than thousands of GDI+ calls per second.
static void drawStarsRaw(Surf& sf, double t) {
    float s = g_dpi / 96.f, ga = easeOut(float((t - g_startT) / 0.8));
    uint32_t* px = (uint32_t*)sf.bits; int w = sf.w, h = sf.h, sz = std::max(1, (int)lroundf(s));
    for (auto& st : g_stars) {
        if (st.r >= 1.2f) continue;
        float x = fmodf(st.x + float(t) * 1.6f * st.r, CARD_X - 12);
        float a = clamp01(st.a * (0.55f + 0.45f * sinf(float(t) * st.sp + st.ph)) * ga / 255.f);
        int X = (int)(x * s), Y = (int)(st.y * s);
        for (int dy = 0; dy < sz; dy++) for (int dx = 0; dx < sz; dx++) {
            int xx = X + dx, yy = Y + dy; if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
            uint32_t& p = px[size_t(yy) * w + xx]; uint32_t o = 0xff000000u;
            for (int c = 0; c < 24; c += 8) { uint32_t v = (p >> c) & 255; v = v + (uint32_t)((255 - v) * a); o |= v << c; }
            p = o;
        }
    }
}

// Everything that moves: stars, shooting star, the moon, the progress trail, bursts.
static void drawSceneDynamic(Graphics& g, double t) {
    float intro = introOf(t);
    g_alpha = easeOut(float((t - g_startT) / 0.8));
    GraphicsState gs = g.Save();
    g.SetClip(RectF(0, 44, CARD_X - 6, H - 44));

    // the few bright stars, anti-aliased (tiny ones are written straight into the frame by drawStarsRaw)
    for (auto& s : g_stars) if (s.r >= 1.2f) {
        float x = fmodf(s.x + float(t) * 1.6f * s.r, CARD_X - 12);
        float a = s.a * (0.55f + 0.45f * sinf(float(t) * s.sp + s.ph));
        SolidBrush b(col(a, 0xffffff)); g.FillEllipse(&b, x - s.r, s.y - s.r, s.r * 2, s.r * 2);
    }
    {   // a shooting star every 7 s
        double period = 7.0; double k = floor((t - g_startT) / period); double e = (t - g_startT) - k * period;
        if (k >= 0 && e < 0.9) {
            uint32_t sd = (uint32_t)(k * 2654435761u); auto r = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) / 16777216.0f; };
            float sx = 40 + r() * 260, sy = 60 + r() * 120, ang = (22 + r() * 16) * PI / 180, len = 110;
            float p = easeOut(float(e / 0.9)), hx = sx + cosf(ang) * 260 * p, hy = sy + sinf(ang) * 260 * p;
            float fade = 1 - float(e / 0.9);
            LinearGradientBrush lb(PointF(hx - cosf(ang) * len, hy - sinf(ang) * len), PointF(hx, hy), col(0, 0xffffff), col(200 * fade, 0xfff3ea));
            Pen pen(&lb, 1.3f); pen.SetEndCap(LineCapRound);
            g.DrawLine(&pen, hx - cosf(ang) * len * p, hy - sinf(ang) * len * p, hx, hy);
        }
    }

    float tm = T0 + float(t - g_startT) * 0.55f, trailA = T0, trailB = T0, moonA = 255, moonScale = 1, flash = 0;
    double de = t - g_doneT;
    if (g_page == P_WORKING) {
        tm = trailB = g_uninstall ? T0 + 2 * PI * (1 - g_disp) : T0 + 2 * PI * g_disp;
        g_moonFreeze = tm;
    } else if (g_page == P_DONE) {
        float e = float(de);
        if (!g_uninstall) { tm = T0 + 2 * PI + 0.55f * e + (2.0f - 0.55f) * (1 - expf(-e * 2)) / 2; trailB = T0 + 2 * PI; flash = std::max(0.f, 1 - e / 0.9f); }
        else { tm = T0; moonScale = 1 + e * 1.3f; moonA = 255 * std::max(0.f, 1 - e / 1.5f); }
    } else if (g_page == P_ERROR) tm = g_moonFreeze;
    float trailAlpha = (g_page == P_DONE && !g_uninstall) ? 120 + 110 * std::max(0.f, 1 - float(de) / 2.5f) : 235;
    bool err = g_page == P_ERROR;
    PointF mp = orb(tm, moonScale); bool moonFront = isFront(tm);

    auto layer = [&](bool front) {
        GraphicsState ls = g.Save(); if (!front) excludeCore(g);
        if (intro < 0.999f) arc(g, T0, T0 + 2 * PI * intro, front, 1.3f, col(front ? 119 : 57, PEACH));
        if (trailB > trailA) {
            arc(g, trailA, trailB, front, 7, col((front ? 0.22f : 0.12f) * trailAlpha, err ? RED : PEACH));
            arc(g, trailA, trailB, front, 2.2f, col(front ? trailAlpha : trailAlpha * 0.6f, err ? RED : PEACH_HI));
        }
        if (g_page == P_DONE && !g_uninstall && de < 1.4) { float e = float(de / 1.4); arc(g, 0, 2 * PI, front, 1.5f, col(200 * (1 - e) * (1 - e), PEACH_HI), 1 + e * 0.9f); }
        if (moonFront == front && moonA > 1) {
            glow(g, mp.X, mp.Y, 20, col((front ? 0.42f : 0.25f) * moonA, PEACH));
            SolidBrush mb(col(front ? moonA : moonA * 0.7f, 0xffdcc4)); g.FillEllipse(&mb, mp.X - 7, mp.Y - 7, 14.f, 14.f);
        }
        g.Restore(ls);
    };
    layer(false);
    if (flash > 0.01f) { glow(g, OCX, OCY, 120 + 40 * (1 - flash), col(90 * flash, PEACH)); glow(g, OCX - 4, OCY - 5, 30, col(170 * flash, 0xfff1e6)); }
    layer(true);
    g.Restore(gs);
    g_alpha = 1;
}

// static background: base colour, glows, glass card, film grain
static void buildBackground(int pw, int ph, float s) {
    delete g_bgBmp; g_bgBmp = new Bitmap(pw, ph, PixelFormat32bppARGB);
    Graphics g(g_bgBmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.ScaleTransform(s, s);
    float sa = g_alpha; g_alpha = 1;
    SolidBrush base(col(255, BG)); g.FillRectangle(&base, -1.f, -1.f, W + 2, H + 2);
    glow(g, OCX, OCY, 330, col(42, PEACH_LO));
    glow(g, W - 40, H + 40, 380, col(22, 0x7c8cff));
    glow(g, CARD_X + 140, -60, 260, col(14, PEACH));
    // glass card
    RectF card(CARD_X, CARD_Y, CARD_W, CARD_H);
    LinearGradientBrush cb(PointF(0, CARD_Y), PointF(0, CARD_Y + CARD_H), col(16, 0xffffff), col(7, 0xffffff));
    fillRound(g, card, 18, cb);
    strokeRound(g, card, 18, col(22, 0xffffff), 1);
    Pen hl(col(30, 0xffffff), 1); g.DrawLine(&hl, CARD_X + 18, CARD_Y + .5f, CARD_X + CARD_W - 18, CARD_Y + .5f);
    strokeRound(g, RectF(0.5f, 0.5f, W - 1, H - 1), 16, col(26, 0xffffff), 1);
    g_alpha = sa;
    // grain
    BitmapData bd; Rect rr(0, 0, pw, ph);
    if (g_bgBmp->LockBits(&rr, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
        uint32_t seed = 12345;
        for (int y = 0; y < ph; y++) {
            uint8_t* row = (uint8_t*)bd.Scan0 + y * bd.Stride;
            for (int x = 0; x < pw; x++) {
                seed = seed * 1103515245u + 12345u; int n = int((seed >> 16) & 7) - 3;
                for (int c = 0; c < 3; c++) { int v = row[x * 4 + c] + n; row[x * 4 + c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
            }
        }
        g_bgBmp->UnlockBits(&bd);
    }
    surfMake(g_bgS, pw, ph);
    { Graphics gg(g_bgS.gb); gg.SetCompositingMode(CompositingModeSourceCopy); gg.DrawImage(g_bgBmp, 0, 0, pw, ph); }
    // the glowing core, pre-rendered once per DPI
    delete g_coreBmp; int cs = (int)ceilf(CORE_R * 2 * s);
    g_coreBmp = new Bitmap(cs, cs, PixelFormat32bppPARGB);
    {
        Graphics c(g_coreBmp); c.SetSmoothingMode(SmoothingModeAntiAlias); c.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        c.Clear(Color(0, 0, 0, 0)); c.ScaleTransform(s, s); c.TranslateTransform(CORE_R - OCX, CORE_R - OCY);
        float sa2 = g_alpha; g_alpha = 1;
        glow(c, OCX, OCY, 108, col(70, PEACH_LO));
        GraphicsPath p; p.AddEllipse(OCX - 30, OCY - 30, 60.f, 60.f);
        PathGradientBrush b(&p); b.SetCenterPoint(PointF(OCX - 10, OCY - 12));
        b.SetCenterColor(col(255, 0xffe6d2)); Color sc = col(255, 0xe9793d); INT one = 1; b.SetSurroundColors(&sc, &one);
        REAL fac[] = { 0, .45f, .8f, 1 }, pos[] = { 0, .3f, .65f, 1 }; b.SetBlend(fac, pos, 4);
        c.FillPath(&b, &p);
        Pen rim(col(40, 0xffffff), 1); c.DrawArc(&rim, OCX - 29.5f, OCY - 29.5f, 59.f, 59.f, 200, 80);
        g_alpha = sa2;
    }
}

// ----------------------------------------------------------------------------------------------------------
// Widgets
// ----------------------------------------------------------------------------------------------------------
static void primaryButton(Graphics& g, RectF r, const std::wstring& label, int id, bool withArrow = true) {
    float h = g_hov[id], pr = (g_press == id && g_hover == id) ? 1.f : 0.f;
    r.Y += pr;
    for (int i = 6; i >= 1; i--) {   // soft glow
        RectF e(r.X - i * 1.6f, r.Y - i * 1.6f + 3, r.Width + i * 3.2f, r.Height + i * 3.2f);
        SolidBrush b(col((5 + 9 * h) * (7 - i) / 6.f, PEACH_LO)); fillRound(g, e, r.Height / 2 + i * 1.6f, b);
    }
    LinearGradientBrush lb(PointF(r.X, 0), PointF(r.X + r.Width, 0), col(255, mix(PEACH_HI, 0xffffff, .25f * h)), col(255, mix(PEACH_LO, 0xffb27e, .3f * h)));
    fillRound(g, r, r.Height / 2, lb);
    Pen top(col(90, 0xffffff), 1); g.DrawLine(&top, r.X + r.Height / 2, r.Y + 1, r.X + r.Width - r.Height / 2, r.Y + 1);
    Font* f = font(F_SEMI, 14.5f);
    float tw = textW(g, label, f), aw = withArrow ? 16.f : 0.f, gap = withArrow ? 10.f : 0.f, total = tw + gap + aw;
    float x = r.X + (r.Width - total) / 2, y = r.Y + (r.Height - 18) / 2;
    text(g, label, f, x, y, col(255, 0x1a0e06));
    if (withArrow) arrow(g, x + tw + gap + 3 * h, r.Y + r.Height / 2 + .5f, aw, col(255, 0x1a0e06));
    hot(id, r);
}
static void textButton(Graphics& g, float cx, float y, const std::wstring& label, int id) {
    Font* f = font(F_SANS, 13); float w = textW(g, label, f);
    text(g, label, f, cx - w / 2, y, col(255, mix(TEXT2, TEXT, g_hov[id])));
    Pen u(col(120 * g_hov[id], TEXT), 1); g.DrawLine(&u, cx - w / 2, y + 18, cx + w / 2, y + 18);
    hot(id, RectF(cx - w / 2 - 12, y - 6, w + 24, 30));
}
static void toggleRow(Graphics& g, float y, const std::wstring& title, const std::wstring& sub, int id, bool on, uint32_t onColor) {
    float t = g_tog[id], h = g_hov[id];
    RectF row(X0 - 10, y - 8, CW + 20, 50);
    SolidBrush rb(col(10 * h, 0xffffff)); fillRound(g, row, 12, rb);
    text(g, title, font(F_SANS, 13.5f), X0, y, col(255, TEXT));
    textBox(g, sub, font(F_SANS, 11.5f), RectF(X0, y + 19, CW - 56, 18), col(255, on && onColor == RED ? RED : TEXT3));
    RectF tr(X0 + CW - 40, y + 6, 40, 22);
    if (t > 0.01f) {
        LinearGradientBrush lb(PointF(tr.X, 0), PointF(tr.X + tr.Width, 0), col(255 * t, onColor == RED ? 0xfca5a5 : PEACH_HI), col(255 * t, onColor == RED ? RED : PEACH_LO));
        fillRound(g, tr, 11, lb);
    }
    SolidBrush off(col(255 * (1 - t), 0x26262b)); fillRound(g, tr, 11, off);
    strokeRound(g, tr, 11, col(40 * (1 - t), 0xffffff));
    float kx = tr.X + 11 + t * 18;
    SolidBrush sh(col(60, 0x000000)); g.FillEllipse(&sh, kx - 8, tr.Y + 4, 16.f, 16.f);
    SolidBrush kb(col(255, mix(0x9a9aa2, 0xffffff, t))); g.FillEllipse(&kb, kx - 8, tr.Y + 3, 16.f, 16.f);
    hot(id, row);
}
static void folderGlyph(Graphics& g, float x, float y, Color c) {
    GraphicsPath p; p.AddLine(x, y + 2, x + 5, y + 2); p.AddLine(x + 5, y + 2, x + 7, y + 4); p.AddLine(x + 7, y + 4, x + 15, y + 4);
    p.AddLine(x + 15, y + 4, x + 15, y + 13); p.AddLine(x + 15, y + 13, x, y + 13); p.CloseFigure();
    Pen pen(c, 1.3f); pen.SetLineJoin(LineJoinRound); g.DrawPath(&pen, &p);
}
static void label(Graphics& g, const std::wstring& s, float y, uint32_t c = PEACH, float a = 210) { tracked(g, s, font(F_MONO, 10.5f), X0, y, col(a, c), 2.2f); }
static void title2(Graphics& g, const std::wstring& a, const std::wstring& b, float y, float size) {
    text(g, a, font(F_SERIF, size), X0 - 2, y, col(255, TEXT));
    text(g, b, font(F_SERIF_I, size), X0 - 2, y + size * 1.02f, col(255, PEACH));
}
static void locationBox(Graphics& g, float y, bool editable) {
    label(g, editable ? L"INSTALL LOCATION" : L"INSTALLED IN", y, TEXT3, 255);
    RectF box(X0, y + 18, CW, 42);
    float h = editable ? g_hov[H_CHANGE] : 0;
    SolidBrush b(col(10 + 6 * h, 0xffffff)); fillRound(g, box, 10, b);
    strokeRound(g, box, 10, col(255, mix(0x2a2a30, 0x8a5a3c, h)));
    folderGlyph(g, X0 + 14, y + 31, col(200, PEACH));
    float right = editable ? 70 : 14;
    text(g, fitPath(g, g_dir, font(F_UI, 13), CW - 48 - right), font(F_UI, 13), X0 + 40, y + 30, col(255, 0xe4e4e7));
    if (editable) {
        Font* f = font(F_SEMI, 12.5f); float w = textW(g, L"Change", f);
        text(g, L"Change", f, X0 + CW - 14 - w, y + 31, col(255, mix(PEACH, PEACH_HI, h)));
        hot(H_CHANGE, box);
    }
}
static std::wstring mb(uint64_t b) { return std::to_wstring((b + (1 << 19)) >> 20) + L" MB"; }

// ----------------------------------------------------------------------------------------------------------
// Pages
// ----------------------------------------------------------------------------------------------------------
static int workingStageIndex() {
    static const float B[] = { 0, .04f, .80f, .90f, .96f }, BU[] = { 0, .08f, .84f, .90f, .99f };
    int ds = 0; for (int i = 0; i < 5; i++) if (g_disp >= (g_uninstall ? BU[i] : B[i])) ds = i;
    return std::min(ds, g_stage.load());
}
static std::wstring workingStage(double t) {
    static const wchar_t* STAGE_IN[] = { L"Preparing for launch", L"Unpacking the brain", L"Checking the WebView2 runtime", L"Creating shortcuts", L"Final systems check" };
    static const wchar_t* STAGE_UN[] = { L"Powering down", L"Removing files", L"Removing shortcuts", L"Cleaning up", L"Final sweep" };
    int st = workingStageIndex();
    std::wstring s = g_uninstall ? STAGE_UN[st] : STAGE_IN[st];
    if (!g_uninstall && st == 2 && g_wv2Busy) s = L"Installing Microsoft Edge WebView2";
    return s + std::wstring(int(t * 2.5) % 4, L'.');
}
// The moving parts of the installing page, drawn every frame over the cached layer.
static void drawWorkingDynamic(Graphics& g, double t) {
    float e = easeOut(float((t - g_pageT) / 0.55));
    g_alpha = e;
    GraphicsState gs = g.Save(); g.TranslateTransform(0, (1 - e) * 14);
    int pct = (int)floorf(g_disp * 100 + 0.0001f);
    Font* pf = font(F_SERIF, 132); std::wstring ps = std::to_wstring(pct);
    text(g, ps, pf, X0 - 6, 190, col(255, TEXT));
    text(g, L"%", font(F_SERIF_I, 46), X0 - 6 + textW(g, ps, pf) + 6, 268, col(255, PEACH));
    float fw = std::max(3.f, CW * g_disp);
    LinearGradientBrush fb(PointF(X0, 0), PointF(X0 + CW, 0), col(255, PEACH_HI), col(255, PEACH_LO));
    fillRound(g, RectF(X0, 380, fw, 3), 1.5f, fb);
    glow(g, X0 + fw, 381.5f, 12, col(150, PEACH_HI));
    std::wstring cur;
    { std::lock_guard<std::mutex> lk(g_mx); for (auto& m : g_marks) { cur = m.second; if (m.first >= g_disp) break; } }
    if (g_wv2Busy && workingStageIndex() == 2) cur = L"Downloading from Microsoft \u2014 this can take a minute";
    text(g, fitPath(g, cur, font(F_MONO, 11), CW), font(F_MONO, 11), X0, 396, col(255, TEXT3));
    g.Restore(gs); g_alpha = 1;
}

static void drawContent(Graphics& g, double t) {
    float e = easeOut(float((t - g_pageT) / 0.55));
    float intro = easeOut(float((t - g_startT - 0.25) / 0.8));
    g_alpha = e * intro;
    GraphicsState gs = g.Save(); g.TranslateTransform(0, (1 - e) * 14);
    const std::wstring V = APP_VERSION_W;

    if (g_page == P_WELCOME && !g_uninstall) {
        label(g, std::wstring(g_existing ? L"UPDATE" : L"SETUP") + L"  \u00B7  VERSION " + V, 96);
        Font* tf = font(F_SERIF, 58);
        text(g, L"Astro AI", tf, X0 - 3, 116, col(255, TEXT));
        if (!EDITION_KEYS) text(g, L"clean", font(F_SERIF_I, 58), X0 - 3 + textW(g, L"Astro AI ", tf), 116, col(255, PEACH));
        textBox(g, L"Chat, a code Workspace and an Agent \u2014 one brain of free AI models, working together.",
                font(F_SANS, 14.5f), RectF(X0, 190, CW, 46), col(255, TEXT2));
        // edition chip
        std::wstring chip = EDITION_KEYS ? L"Personal edition \u00B7 includes your API keys" : L"Clean edition \u00B7 no API keys inside";
        uint32_t dot = EDITION_KEYS ? AMBER : GREEN;
        Font* cf = font(F_SANS, 12.5f); float cw = textW(g, chip, cf) + 40;
        RectF cr(X0, 250, cw, 30);
        SolidBrush cbg(col(14, 0xffffff)); fillRound(g, cr, 15, cbg); strokeRound(g, cr, 15, col(26, 0xffffff));
        glow(g, X0 + 16, 265, 9, col(110, dot)); SolidBrush db(col(255, dot)); g.FillEllipse(&db, X0 + 12.5f, 261.5f, 7.f, 7.f);
        text(g, chip, cf, X0 + 28, 257, col(255, 0xdcdce0));
        locationBox(g, 304, true);
        toggleRow(g, 386, L"Desktop shortcut", L"A Start menu shortcut is always added", H_T_DESKTOP, g_desktopLink, PEACH);
        primaryButton(g, RectF(X0, 446, CW, 48), (g_existing ? L"Update " : L"Install ") + std::wstring(APP_NAME_W), H_PRIMARY);
        std::wstring foot = L"PER-USER  \u00B7  NO ADMIN NEEDED  \u00B7  " + mb(g_payloadBytes);
        if (EDITION_KEYS) foot = L"PER-USER  \u00B7  " + mb(g_payloadBytes) + L"  \u00B7  KEEP PRIVATE";
        Font* ff = font(F_MONO, 9.5f); float fw = trackedW(g, foot, ff, 1.4f);
        tracked(g, foot, ff, X0 + (CW - fw) / 2, 508, col(255, EDITION_KEYS ? 0x9a7b4a : 0x5f5f68), 1.4f);
    } else if (g_page == P_WELCOME && g_uninstall) {
        label(g, L"UNINSTALL  \u00B7  VERSION " + V, 96);
        title2(g, L"Leaving", L"orbit?", 112, 54);
        textBox(g, L"This removes " APP_NAME_W L" from this PC: the app, its shortcuts and its entry in Installed apps.",
                font(F_SANS, 14), RectF(X0, 236, CW, 44), col(255, TEXT2));
        locationBox(g, 296, false);
        toggleRow(g, 378, L"Also delete my data", g_purge ? L"Chats, settings and keys \u2014 can't be undone" : L"Chats, projects, settings and saved API keys",
                  H_T_PURGE, g_purge, RED);
        primaryButton(g, RectF(X0, 438, CW, 48), L"Uninstall", H_PRIMARY, false);
        textButton(g, X0 + CW / 2, 500, L"Keep it", H_SECONDARY);
    } else if (g_page == P_WORKING) {
        label(g, g_uninstall ? L"UNINSTALLING" : (g_existing ? L"UPDATING" : L"INSTALLING"), 96);
        text(g, workingStage(t), font(F_SERIF, 34), X0 - 2, 118, col(255, TEXT));
        RectF tr(X0, 380, CW, 3);
        SolidBrush tb(col(22, 0xffffff)); fillRound(g, tr, 1.5f, tb);
        std::wstring foot = L"PLEASE KEEP THIS WINDOW OPEN";
        Font* ff = font(F_MONO, 9.5f); float w = trackedW(g, foot, ff, 1.4f);
        tracked(g, foot, ff, X0 + (CW - w) / 2, 508, col(255, 0x5f5f68), 1.4f);
    } else if (g_page == P_DONE && !g_uninstall) {
        label(g, g_existing ? L"UPDATED" : L"COMPLETE", 96);
        title2(g, L"Ready for", L"liftoff.", 112, 54);
        textBox(g, APP_NAME_W L" is installed. Open it any time from the Start menu" + std::wstring(g_desktopLink ? L" or your desktop." : L"."),
                font(F_SANS, 14), RectF(X0, 236, CW, 44), col(255, TEXT2));
        RectF tip(X0, 300, CW, 96);
        SolidBrush tb(col(10, 0xffffff)); fillRound(g, tip, 12, tb); strokeRound(g, tip, 12, col(22, 0xffffff));
        tracked(g, L"TIP", font(F_MONO, 10), X0 + 16, 314, col(220, PEACH), 2);
        textBox(g, L"To run code and terminal commands in the Workspace and Agent, install Git for Windows.",
                font(F_SANS, 12.5f), RectF(X0 + 16, 332, CW - 32, 36), col(255, 0xc8c8ce));
        Font* lf = font(F_SEMI, 12.5f); std::wstring lt = L"Get Git for Windows"; float lw = textW(g, lt, lf);
        uint32_t lc = mix(PEACH, PEACH_HI, g_hov[H_LINK]);
        text(g, lt, lf, X0 + 16, 372, col(255, lc)); arrow(g, X0 + 22 + lw, 375, 8, col(255, lc), true);
        hot(H_LINK, RectF(X0 + 10, 366, lw + 30, 26));
        primaryButton(g, RectF(X0, 438, CW, 48), L"Launch " APP_NAME_W, H_PRIMARY);
        textButton(g, X0 + CW / 2, 500, L"Close", H_SECONDARY);
    } else if (g_page == P_DONE && g_uninstall) {
        label(g, L"REMOVED", 96);
        title2(g, L"Goodbye", L"for now.", 112, 54);
        std::wstring body = APP_NAME_W L" was removed from this PC.";
        body += g_purge ? L" Your data was deleted too." : L" Your chats and settings are kept in %APPDATA%\\" APP_NAME_W L", in case you come back.";
        textBox(g, body, font(F_SANS, 14), RectF(X0, 236, CW, 90), col(255, TEXT2));
        primaryButton(g, RectF(X0, 446, CW, 48), L"Close", H_PRIMARY, false);
    } else if (g_page == P_ERROR) {
        label(g, L"SOMETHING WENT WRONG", 96, RED, 230);
        title2(g, L"That didn't", L"land.", 112, 54);
        std::wstring m; { std::lock_guard<std::mutex> lk(g_mx); m = g_err; }
        textBox(g, m, font(F_SANS, 13.5f), RectF(X0, 236, CW, 150), col(255, TEXT2));
        primaryButton(g, RectF(X0, 438, CW, 48), L"Try again", H_PRIMARY);
        textButton(g, X0 + CW / 2, 500, L"Close", H_SECONDARY);
    }
    g.Restore(gs);
    g_alpha = 1;
}

static void drawChrome(Graphics& g, double t) {
    g_alpha = easeOut(float((t - g_startT) / 0.6));
    if (g_logo) { g.SetInterpolationMode(InterpolationModeHighQualityBicubic); g.DrawImage(g_logo, RectF(22, 19, 20, 20)); }
    text(g, APP_NAME_W, font(F_SEMI, 12.5f), 50, 21, col(235, TEXT));
    float x = 50 + textW(g, APP_NAME_W L" ", font(F_SEMI, 12.5f)) + 4;
    tracked(g, g_uninstall ? L"UNINSTALL" : L"SETUP", font(F_MONO, 9.5f), x, 23.5f, col(150, TEXT3), 1.6f);
    // window buttons
    RectF cb(W - 50, 12, 36, 30), mbx(W - 90, 12, 36, 30);
    bool busy = g_page == P_WORKING;
    float hc = g_hov[H_CLOSE] * (busy ? 0.f : 1.f), hm = g_hov[H_MIN];
    if (hc > 0.01f) { SolidBrush b(col(230 * hc, 0xe5484d)); fillRound(g, cb, 8, b); }
    if (hm > 0.01f) { SolidBrush b(col(22 * hm, 0xffffff)); fillRound(g, mbx, 8, b); }
    Pen px(col(busy ? 70 : 255, mix(0xa1a1aa, 0xffffff, hc)), 1.3f); px.SetStartCap(LineCapRound); px.SetEndCap(LineCapRound);
    float cx = cb.X + 18, cy = cb.Y + 15;
    g.DrawLine(&px, cx - 5, cy - 5, cx + 5, cy + 5); g.DrawLine(&px, cx + 5, cy - 5, cx - 5, cy + 5);
    Pen pm(col(255, mix(0xa1a1aa, 0xffffff, hm)), 1.3f); pm.SetStartCap(LineCapRound); pm.SetEndCap(LineCapRound);
    g.DrawLine(&pm, mbx.X + 13, mbx.Y + 15.5f, mbx.X + 23, mbx.Y + 15.5f);
    hot(H_CLOSE, cb); hot(H_MIN, mbx);
    g_alpha = 1;
}

// ----------------------------------------------------------------------------------------------------------
// Window
// ----------------------------------------------------------------------------------------------------------
static float scale() { return g_dpi / 96.f; }
static std::wstring g_sig;
static void ensureBuffer(HDC) {
    RECT rc; GetClientRect(g_hwnd, &rc); int w = std::max(1L, rc.right), h = std::max(1L, rc.bottom);
    if (w == g_bw && h == g_bh && g_memS.dc) return;
    surfMake(g_layerS, w, h); surfMake(g_memS, w, h);
    g_bw = w; g_bh = h; g_sig.clear();
    buildBackground(w, h, scale());
}

// Everything that feeds the cached layer. When this string doesn't change, the layer is reused as-is.
static std::wstring layerSig(double t) {
    auto q = [](float v) { return std::to_wstring((int)lroundf(clamp01(v) * 48)); };
    std::wstring s = std::to_wstring((int)g_page) + L"|" + q(easeOut(float((t - g_pageT) / 0.55))) + L"|" + q(easeOut(float((t - g_startT) / 0.8)))
        + L"|" + q(easeOut(float((t - g_startT - .25) / 0.8))) + L"|" + q(easeOut(float((t - g_startT - .4) / 0.8))) + L"|" + q(easeOut(float((t - g_startT) / 0.6)))
        + L"|" + (introOf(t) >= 0.999f ? L"R" : L"r") + L"|" + std::to_wstring(g_press) + L"|" + (g_desktopLink ? L"D" : L"d") + (g_purge ? L"P" : L"p") + L"|" + g_dir;
    for (int i = 0; i < H_COUNT; i++) s += L"," + q(g_hov[i]) + L":" + q(g_tog[i]);
    if (g_page == P_WORKING) s += L"|" + workingStage(t);
    return s;
}
static void quality(Graphics& g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit); g.SetCompositingQuality(CompositingQualityHighSpeed);
    g.ScaleTransform(scale(), scale());
}

static void paint(HDC wdc) {
    ensureBuffer(wdc);
    double t = now(), q0 = t;
    static int rebuilt = 0;
    std::wstring sig = layerSig(t);
    if (sig != g_sig) {
        g_sig = sig; g_hots.clear(); rebuilt++;
        surfCopy(g_layerS, g_bgS);
        Graphics lg(g_layerS.gb); quality(lg);
        drawSceneStatic(lg, t); drawChrome(lg, t); drawContent(lg, t);
    }
    surfCopy(g_memS, g_layerS);
    drawStarsRaw(g_memS, t);
    {
        Graphics g(g_memS.gb); quality(g);
        drawSceneDynamic(g, t);
        if (g_page == P_WORKING) drawWorkingDynamic(g, t);
    }
    BitBlt(wdc, 0, 0, g_bw, g_bh, g_memS.dc, 0, 0, SRCCOPY);
    static double acc = 0; static int frames = 0; static std::wstring prof = envOr(L"ASTRO_SETUP_PROFILE");
    if (!prof.empty()) {
        acc += now() - q0;
        if (++frames == 120) {
            FILE* f = _wfopen(prof.c_str(), L"a");
            if (f) { fprintf(f, "page=%d avg %.2f ms/frame, layer rebuilt %d/120\n", (int)g_page, acc / 120 * 1000, rebuilt); fclose(f); }
            acc = 0; frames = 0; rebuilt = 0;
        }
    }
}

static void tick() {
    double t = now(); float dt = float(std::min(0.1, t - g_lastT)); g_lastT = t;
    for (int i = 0; i < H_COUNT; i++) {
        float target = (g_hover == i) ? 1.f : 0.f; g_hov[i] += (target - g_hov[i]) * std::min(1.f, dt * 14);
    }
    float tD = g_desktopLink ? 1.f : 0.f, tP = g_purge ? 1.f : 0.f;
    g_tog[H_T_DESKTOP] += (tD - g_tog[H_T_DESKTOP]) * std::min(1.f, dt * 16);
    g_tog[H_T_PURGE] += (tP - g_tog[H_T_PURGE]) * std::min(1.f, dt * 16);
    if (g_page == P_WORKING) {
        float target = g_prog.load();
        float maxSpeed = 1.f / 3.4f;   // the journey takes at least ~3.4 s so it can be seen
        g_disp += std::min(maxSpeed * dt, std::max(0.f, target - g_disp) * std::min(1.f, dt * 5) + 0.02f * dt);
        if (g_disp > target) g_disp = target;
        if (g_taskbar) g_taskbar->SetProgressValue(g_hwnd, (ULONGLONG)(g_disp * 1000), 1000);
        if (g_state == 2) {
            if (g_worker.joinable()) g_worker.join();
            g_page = P_ERROR; g_pageT = t;
            if (g_taskbar) g_taskbar->SetProgressState(g_hwnd, TBPF_ERROR);
        } else if (g_state == 1 && g_disp >= 0.999f) {
            if (g_worker.joinable()) g_worker.join();
            g_disp = 1; g_page = P_DONE; g_pageT = g_doneT = t;
            if (g_taskbar) g_taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS);
            FLASHWINFO fi = { sizeof(fi), g_hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0 }; FlashWindowEx(&fi);
        }
    }
}


static int hitTest(float lx, float ly) {
    for (auto it = g_hots.rbegin(); it != g_hots.rend(); ++it) if (it->r.Contains(lx, ly)) return it->id;
    return H_NONE;
}

static void pickFolder() {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&d))) return;
    DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    d->SetTitle(L"Choose where to install " APP_NAME_W);
    std::wstring start = g_dir; while (!start.empty() && !dirExists(start)) start = parentDir(start);
    IShellItem* si = nullptr;
    if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_IShellItem, (void**)&si))) { d->SetFolder(si); si->Release(); }
    if (SUCCEEDED(d->Show(g_hwnd))) {
        IShellItem* r = nullptr;
        if (SUCCEEDED(d->GetResult(&r))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(r->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                std::wstring s = trimSlash(p);
                if (lower(leafName(s)) != lower(APP_NAME_W)) s += L"\\" APP_NAME_W;
                g_dir = s; CoTaskMemFree(p);
            }
            r->Release();
        }
    }
    d->Release();
}

static void launchApp() {
    std::wstring exe = trimSlash(g_dir) + L"\\" EXE_NAME_W;
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, trimSlash(g_dir).c_str(), SW_SHOWNORMAL);
}

static void click(int id) {
    switch (id) {
    case H_CLOSE: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    case H_MIN: ShowWindow(g_hwnd, SW_MINIMIZE); break;
    case H_CHANGE: if (g_page == P_WELCOME && !g_uninstall) pickFolder(); break;
    case H_T_DESKTOP: g_desktopLink = !g_desktopLink; break;
    case H_T_PURGE: g_purge = !g_purge; break;
    case H_LINK: ShellExecuteW(nullptr, L"open", L"https://git-scm.com/download/win", nullptr, nullptr, SW_SHOWNORMAL); break;
    case H_PRIMARY:
        if (g_page == P_WELCOME) startWork();
        else if (g_page == P_DONE) { if (!g_uninstall) launchApp(); PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
        else if (g_page == P_ERROR) { g_page = P_WELCOME; g_pageT = now(); if (g_taskbar) g_taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS); }
        break;
    case H_SECONDARY: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    }
}

static void applyDpi(UINT dpi) {
    g_dpi = (float)dpi; g_bw = g_bh = 0;
    int w = (int)lroundf(W * scale()), h = (int)lroundf(H * scale());
    // Windows 11: native rounded corners; older: a rounded window region.
    int pref = 2;   // DWMWCP_ROUND
    if (FAILED(DwmSetWindowAttribute(g_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref)))) {
        int r = (int)lroundf(16 * scale()); SetWindowRgn(g_hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r), TRUE);
    }
}

static UINT dpiOf(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) { UINT d = fn(h); if (d) return d; }
    HDC dc = GetDC(nullptr); UINT d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); return d ? d : 96;
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_tbMsg && m == g_tbMsg) {
        if (!g_taskbar) CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, (void**)&g_taskbar);
        return 0;
    }
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if (!IsIconic(h)) { tick(); InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, wp, lp); if (r != HTCLIENT) return r;
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &p);
        float lx = p.x / scale(), ly = p.y / scale();
        if (hitTest(lx, ly) != H_NONE) return HTCLIENT;
        if (ly < 52 || lx < CARD_X - 6) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOUSEMOVE: {
        float lx = GET_X_LPARAM(lp) / scale(), ly = GET_Y_LPARAM(lp) / scale();
        g_hover = hitTest(lx, ly);
        if (g_page == P_WORKING && g_hover == H_CLOSE) g_hover = H_NONE;
        TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
        return 0;
    }
    case WM_MOUSELEAVE: g_hover = H_NONE; return 0;
    case WM_LBUTTONDOWN: g_press = g_hover; SetCapture(h); return 0;
    case WM_LBUTTONUP: { ReleaseCapture(); int p = g_press; g_press = H_NONE; if (p != H_NONE && p == g_hover) click(p); return 0; }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover != H_NONE) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_KEYDOWN:
        if (wp == VK_RETURN && g_page != P_WORKING) click(H_PRIMARY);
        else if (wp == VK_ESCAPE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE: if (g_page == P_WORKING) return 0; DestroyWindow(h); return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp; applyDpi(HIWORD(wp));
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY: KillTimer(h, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ----------------------------------------------------------------------------------------------------------
// Fonts and images from resources
// ----------------------------------------------------------------------------------------------------------
static void loadFont(int id, Fam& out, const wchar_t* fallback) {
    const void* p; size_t n;
    if (resource(id, &p, &n)) {
        auto* pfc = new PrivateFontCollection();
        if (pfc->AddMemoryFont(p, (INT)n) == Ok && pfc->GetFamilyCount() > 0) {
            auto* fams = new FontFamily[1]; INT found = 0; pfc->GetFamilies(1, fams, &found);
            if (found > 0 && fams[0].IsAvailable()) { out.f = &fams[0]; return; }
        }
    }
    out.f = new FontFamily(fallback);
    if (!out.f->IsAvailable()) { delete out.f; out.f = new FontFamily(L"Tahoma"); }
}
static Bitmap* loadPng(int id) {
    const void* p; size_t n; if (!resource(id, &p, &n)) return nullptr;
    IStream* s = SHCreateMemStream((const BYTE*)p, (UINT)n); if (!s) return nullptr;
    Bitmap* b = Bitmap::FromStream(s); s->Release();
    if (b && b->GetLastStatus() != Ok) { delete b; return nullptr; }
    return b;
}

// ----------------------------------------------------------------------------------------------------------
// Entry
// ----------------------------------------------------------------------------------------------------------
static bool hasArg(int argc, wchar_t** argv, const wchar_t* a) { for (int i = 1; i < argc; i++) if (!lstrcmpiW(argv[i], a)) return true; return false; }
static std::wstring argVal(int argc, wchar_t** argv, const wchar_t* a) { for (int i = 1; i + 1 < argc; i++) if (!lstrcmpiW(argv[i], a)) return argv[i + 1]; return L""; }

static void selfDeleteLater() {
    std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + g_selfPath + L"\"";
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t self[MAX_PATH * 2]; GetModuleFileNameW(nullptr, self, MAX_PATH * 2); g_selfPath = self;
    g_silent = hasArg(argc, argv, L"/S");
    g_fromTemp = hasArg(argc, argv, L"--from-temp");
    g_purge = hasArg(argc, argv, L"--purge");
    if (hasArg(argc, argv, L"--no-desktop-shortcut")) g_desktopLink = false;
    g_uninstall = hasArg(argc, argv, L"--uninstall") || !resource(IDR_PAYLOAD, &g_payload, &g_payloadSize);
    if (g_fromTemp) { wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); SetCurrentDirectoryW(tmp); }   // never hold the install folder open

    if (g_uninstall) {
        g_dir = argVal(argc, argv, L"--dir");
        if (g_dir.empty()) g_dir = lower(leafName(g_selfPath)) == L"uninstall.exe" ? parentDir(g_selfPath) : regGet(HKEY_CURRENT_USER, APP_KEY, L"InstallDir");
        if (g_dir.empty() || !dirExists(g_dir)) {
            if (!g_silent) MessageBoxW(nullptr, APP_NAME_W L" doesn't seem to be installed for this user.", APP_NAME_W L" Uninstall", MB_ICONINFORMATION);
            return 1;
        }
        // Run from a temporary copy so the install folder (and Uninstall.exe itself) can be removed.
        if (!g_fromTemp) {
            wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
            std::wstring copy = std::wstring(tmp) + L"astro-uninstall-" + std::to_wstring(GetTickCount()) + L".exe";
            if (CopyFileW(g_selfPath.c_str(), copy.c_str(), FALSE)) {
                std::wstring cmd = L"\"" + copy + L"\" --uninstall --from-temp --dir \"" + trimSlash(g_dir) + L"\"";
                if (g_silent) cmd += L" /S"; if (g_purge) cmd += L" --purge";
                STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, tmp, &si, &pi)) {
                    DWORD code = 0;
                    if (g_silent) { WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code); }
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return (int)code;
                }
            }
        }
    } else {
        mz_zip_archive z; memset(&z, 0, sizeof(z));
        if (mz_zip_reader_init_mem(&z, g_payload, g_payloadSize, 0)) {
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) { mz_zip_archive_file_stat st; if (mz_zip_reader_file_stat(&z, i, &st)) g_payloadBytes += st.m_uncomp_size; }
            mz_zip_reader_end(&z);
        }
        std::wstring prev = regGet(HKEY_CURRENT_USER, APP_KEY, L"InstallDir");
        if (!prev.empty() && fileExists(trimSlash(prev) + L"\\" EXE_NAME_W)) { g_dir = trimSlash(prev); g_existing = true; }
        else g_dir = knownFolder(FOLDERID_LocalAppData) + L"\\Programs\\" APP_NAME_W;
        // NSIS-style /D=<dir> (rest of the command line)
        std::wstring cl = GetCommandLineW(); size_t dpos = cl.find(L" /D=");
        if (dpos != std::wstring::npos) { g_dir = trimSlash(cl.substr(dpos + 4)); while (!g_dir.empty() && (g_dir.back() == L' ' || g_dir.back() == L'"')) g_dir.pop_back(); }
    }

    if (g_silent) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (g_uninstall) uninstallWork(); else installWork();
        int rc = g_state == 1 ? 0 : 2;
        if (g_fromTemp) selfDeleteLater();
        return rc;
    }

    HANDLE mx = CreateMutexW(nullptr, TRUE, (std::wstring(L"AstroSetup-") + APP_ID_W).c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another copy is open: bring it forward. (If it has no window, it's stuck; carry on.)
        HWND other = FindWindowW(L"AstroSetupWindow", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); return 0; }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gin; GdiplusStartup(&g_gdipToken, &gin, nullptr);
    g_fmt = StringFormat::GenericTypographic()->Clone();
    g_fmt->SetFormatFlags(g_fmt->GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    loadFont(IDR_FONT_SANS, F_SANS, L"Segoe UI");
    loadFont(IDR_FONT_SEMI, F_SEMI, L"Segoe UI Semibold");
    loadFont(IDR_FONT_SERIF, F_SERIF, L"Georgia");
    loadFont(IDR_FONT_SERIF_I, F_SERIF_I, L"Georgia");
    loadFont(IDR_FONT_MONO, F_MONO, L"Consolas");
    F_UI.f = new FontFamily(L"Segoe UI"); if (!F_UI.f->IsAvailable()) { delete F_UI.f; F_UI.f = F_SANS.f; }
    g_logo = loadPng(IDR_LOGO);
    makeStars();
    g_tog[H_T_DESKTOP] = g_desktopLink ? 1.f : 0.f; g_tog[H_T_PURGE] = g_purge ? 1.f : 0.f;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DROPSHADOW; wc.lpfnWndProc = wndProc; wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"AstroSetupWindow";
    RegisterClassExW(&wc);
    g_tbMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");

    UINT dpi = dpiOf(nullptr);
    float s = dpi / 96.f; int w = (int)lroundf(W * s), h = (int)lroundf(H * s);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2, y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    std::wstring title = std::wstring(APP_NAME_W) + (g_uninstall ? L" Uninstall" : L" Setup");
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, title.c_str(), WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                             x, y, w, h, nullptr, nullptr, inst, nullptr);
    UINT real = dpiOf(g_hwnd);
    if (real != dpi) { s = real / 96.f; w = (int)lroundf(W * s); h = (int)lroundf(H * s); SetWindowPos(g_hwnd, nullptr, wa.left + ((wa.right - wa.left) - w) / 2, wa.top + ((wa.bottom - wa.top) - h) / 2, w, h, SWP_NOZORDER); }
    applyDpi(real);
    g_startT = g_pageT = g_lastT = now();
    ShowWindow(g_hwnd, SW_SHOW); UpdateWindow(g_hwnd); SetForegroundWindow(g_hwnd);
    SetTimer(g_hwnd, 1, 16, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (g_worker.joinable()) g_worker.join();
    if (g_taskbar) g_taskbar->Release();
    int rc = (g_page == P_DONE) ? 0 : (g_page == P_ERROR ? 2 : 1);
    if (g_uninstall && g_fromTemp) selfDeleteLater();
    ReleaseMutex(mx);
    return rc;
}
