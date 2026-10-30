# A.S.T.R.O AI for Android

The Android app runs the **same C++ engine** as the desktop app, on the phone itself, and shows the normal web
app in a WebView. Nothing is rewritten for mobile: chats, One Brain, the Workspace, the agent, notes and settings are
the same code as on the desktop.

- **Phones:** Android 8.0 (API 26) or newer, 64-bit ARM (`arm64-v8a`). That covers almost every phone from the
  last several years. 32-bit-only and x86 devices are not supported.
- **Size:** about 7 MB to download.
- **Edition:** CLEAN only. No API keys are ever built into the APK.

## How it works

```
APK
├─ lib/arm64-v8a/libastro_engine.so   the engine (a normal executable, named lib*.so so Android unpacks it
│                                      into nativeLibraryDir, the only place an app may run its own programs from)
├─ assets/payload/                    web/, providers.json, edition.json (unpacked to files/app on first start
│                                      and after every update)
├─ assets/cacert.pem                  Mozilla CA list for HTTPS (pinned; Android's own store is not OpenSSL-readable)
└─ classes.dex                        the shell: MainActivity.java (no libraries, plain Android APIs)
```

1. On start the shell unpacks the payload, picks a **free port** and a **random 256-bit token**, and starts the
   engine with `ASTRO_BIND=127.0.0.1`, `ASTRO_APP_TOKEN=<token>`, `ASTRO_DATA_DIR=<app files>/data`.
2. The token is set as the WebView's `astro_session` cookie. In app-token mode the engine refuses **every** request
   without it, including password sign-up. Other apps on the phone *can* reach `127.0.0.1`, which is why this
   matters: they get `401`.
3. The engine's stdin is a pipe held by the app. When the app closes, or Android kills it, the pipe closes and the
   engine shuts down cleanly (`ASTRO_EXIT_ON_STDIN_EOF=1`). Exit code 75 (new keys saved) restarts it.
4. Shell commands (Workspace runner, agent) use Android's `/system/bin/sh`. `platform.h` maps `/bin/bash` and
   `/bin/sh` to it under `__ANDROID__`, so no call site changes. There is no bash, Node or compiler on a stock phone,
   so commands that need them will say "not found". The bubblewrap sandbox is not available on Android.

### The bridge (`window.AstroAndroid`, `web/android.js`)

The Android WebView has no Web Speech API. `web/android.js` (loaded on every platform, does nothing outside the
app) provides `speechSynthesis`, `SpeechSynthesisUtterance` and `SpeechRecognition` on top of Android's own
TextToSpeech and SpeechRecognizer, so the app's voice features work unchanged. It also:

- saves `<a download>` files to **Downloads/A.S.T.R.O AI**;
- opens outside links in the browser;
- shows "open in new tab" pages in a full-screen panel (Android Back closes it);
- colours the status bar with the app's theme.

Android adds a JavaScript bridge to *every* frame, including the frames that show code you run. So the calls that
touch the microphone, files or other apps need a one-time secret. The shell hands it out once per page load,
before any frame exists, and only the app page gets it.

Other Android features:
- **Share:** "Share → A.S.T.R.O AI" from any app puts the shared text in the chat box.
- **Permissions:** only `INTERNET` and `RECORD_AUDIO`. The microphone is asked for the first time you use voice
  input. App backups are off (`allowBackup=false`, data-extraction rules exclude everything).
- **Cleartext:** plain http is allowed only for `127.0.0.1`.

## Building

You need: the Android NDK r30+, JDK 17, the Android SDK (platform 36, build-tools 36) and Gradle 9.6+.

```bash
# 1. the engine: OpenSSL 3.5.9, curl 8.23.0 and the engine, from pinned sources (about 10 minutes the first time)
ANDROID_NDK_ROOT=/path/to/android-ndk-r30 bash android/build_engine.sh

# 2. optional: test the engine on a Linux PC under qemu (needs qemu-user-static)
ASTRO_ANDROID_STATIC_TEST=1 ANDROID_NDK_ROOT=... bash android/build_engine.sh
bash android/test_engine_qemu.sh

# 3. the APK
gradle -p android assembleDebug          # android/app/build/outputs/apk/debug/app-debug.apk
```

The shipped engine is linked against the phone's own `libc.so` for **API 26**, with OpenSSL, curl, zlib and libc++
linked in statically. Linking against the API-26 stubs proves at build time that nothing newer than Android 8.0 is
used. A fully static bionic binary is built **only for the qemu test**. It is never shipped: `libc.a` comes from
the newest Android and may make system calls that older phones' app sandbox (seccomp) forbids.

The web side has its own test, with a mock bridge in Chromium:

```bash
node tests/android_shim_test.js http://127.0.0.1:4173    # needs a running engine and Playwright
```

## Releases and signing

Tagged releases are built by the `android` job in `.github/workflows/desktop.yml`, which:
1. builds the engine and runs the qemu test;
2. builds the APK and signs it with the project key from the `ANDROID_KEYSTORE_BASE64`,
   `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS` secrets;
3. checks that the certificate matches [`android/signing-cert.sha256`](../android/signing-cert.sha256);
4. attaches `AstroAI-<version>-android-arm64-CLEAN.apk` to the release with its SHA-256.

Release signing certificate (SHA-256):

```
51:53:44:2D:71:CE:AB:E7:DC:78:5B:E5:59:BB:BA:84:2A:E0:50:7B:19:44:56:5D:23:9B:7B:89:42:F7:16:84
```

Check a downloaded APK with `apksigner verify --print-certs AstroAI-*.apk`.

**The signing key must never change.** Android only installs an update over an existing app when it is signed
with the same key. If the key were lost, every user would have to uninstall (losing their data) and reinstall. The
maintainer keeps an offline backup of the keystore and its password. Neither is ever committed to the
repository (`android/.gitignore`, and the export script removes `*.jks` / `keystore.properties`).

## Status

New in 1.8.0, and honest about it:
- Built and tested on a computer only: the engine under qemu (start-up, token protection, web app, shell
  commands, clean stop, HTTPS with the bundled CA list), and the bridge in Chromium with a mock.
- **Not yet tried on many real phones.** Please report how it runs: <https://esoexe.github.io/astro-ai/beta.html>.
- It is not on Google Play. Install the APK from the download page; updates install over the old version and keep
  your data.

## Open-source licenses

The APK contains OpenSSL, curl, the Mozilla CA list and LLVM libc++. Their license texts and a notice are inside the APK
under `assets/licenses/` (written by `android/build_engine.sh` from the same verified source archives), and listed in
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
