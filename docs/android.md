# A.S.T.R.O AI for Android

Since **1.8.1** the Android app is a **native app** (Kotlin + Jetpack Compose). There is no web page inside it. It runs the
**same C++ engine** as the desktop app on the phone itself and talks to it the way the desktop interface does, or it
connects to A.S.T.R.O AI on your own computer.

- **Phones:** Android 8.0 (API 26) or newer, 64-bit ARM (`arm64-v8a`). That covers almost every phone from the
  last several years. 32-bit-only and x86 devices are not supported.
- **Edition:** CLEAN only. No API keys are ever built into the APK.
- **Version:** its own number in [`android/app-version.txt`](../android/app-version.txt). Android-only updates (like
  1.8.1) leave the desktop apps alone.
- **Where to get it:** the [download page](https://esoexe.github.io/astro-ai/download.html) and GitHub releases
  (`android-v*` tags). Not on Google Play.

## What's in the app

| Screen | What it does |
| --- | --- |
| Chat | One Brain (all free models think together), modes, model picker, compare models, attach photos and documents, web search, actions with undo (notes, tasks, reminders, memory, settings), read aloud |
| Live voice | Hands-free conversation: Android speech recognition in, text-to-speech out, interrupt by tapping |
| Notes, Tasks, Reminders, Memory | Your organiser, stored on the phone. Reminders are real Android notifications, also after a restart |
| Reader, Knowledge, Calculator, Lab | Read and summarise pages and files, files the answers can quote, exact maths, side-by-side model tests |
| Workspace | Projects and files, an editor, a terminal for the commands the phone can run, search, checkpoints, Git, ZIP export, preview in the browser |
| Agent | Works on a project by itself (Plan / Ask first / Auto), with a live log, approvals, a change view, merge and undo |
| Settings | Interface, chat, One Brain, voice, providers and keys, connection, data (export, delete, sync with your computer), updates, about |

Long answers keep going when you switch apps (a foreground notification while work is running, nothing otherwise).
Text, links, images and PDFs can be shared to the app from any other app.

## How it works

```
APK
├─ lib/arm64-v8a/libastro_engine.so   the engine (a normal executable, named lib*.so so Android unpacks it into
│                                      nativeLibraryDir, the only place an app may run its own programs from)
├─ assets/payload/                    providers.json, model ranking, edition.json (unpacked to files/app)
├─ assets/i18n.json                   French and Arabic, generated from the web app's dictionary + android/i18n-native.json
├─ assets/cacert.pem                  Mozilla CA list for the engine's HTTPS (Android's store is not OpenSSL-readable)
├─ assets/licenses/                   license texts of everything inside the APK
└─ classes.dex                        the app: Kotlin, Jetpack Compose, Material 3
```

1. On start the app unpacks the payload, picks a **free port** and a **random 256-bit token**, and starts the engine
   with `ASTRO_BIND=127.0.0.1`, `ASTRO_AUTH=off`, `ASTRO_APP_TOKEN=<token>`, `ASTRO_DATA_DIR=<app files>/data`. In
   app-token mode the engine refuses **every** request without the token (`X-Astro-Token`). Other apps on the phone
   can reach `127.0.0.1`, which is why this matters: they get `401`.
2. The engine's stdin is a pipe held by the app. When Android ends the app, the pipe closes and the engine stops
   cleanly (`ASTRO_EXIT_ON_STDIN_EOF=1`). Exit code 75 (new keys saved) restarts it.
3. Chats, notes, tasks, memory and reminders are JSON files in the app's private storage (`files/store/`, written
   atomically). Engine data (provider keys, projects, agent sessions) stays in `files/data/`, the same folder as 1.8.0,
   so keys and projects carry over when you install 1.8.1 over 1.8.0.
4. Workspace and agent commands use Android's `/system/bin/sh` (`platform.h` maps `/bin/bash` and `/bin/sh` to it
   under `__ANDROID__`). There is no bash, Node, Python or compiler on a stock phone, so commands that need them say
   "not found". The Workspace shows which languages can run.

### Connect to my computer

Instead of the built-in engine, the app can use A.S.T.R.O AI on your computer (its models, keys and projects):

1. On the computer, run the server so other devices can reach it: build from source and start `./workspace_server`.
   It listens on port 4173 on your network and asks you to create a password the first time. (The 1.8.0 desktop app
   and the `astro` command listen on `127.0.0.1` only, so the phone can't reach them.)
2. In the app: Settings → Connection → Connect to my computer, type the address (for example `192.168.1.20:4173`).
   Use the same Wi-Fi, or a private network such as Tailscale.

The password goes only to your computer; the phone keeps a session token, never the password. The app's network
layer (`net/Api.kt`) refuses plain `http://` to anything that does not resolve to this phone or a private address
(10.x, 172.16–31.x, 192.168.x, 100.64–127.x, link-local, IPv6 ULA). Everything on the internet goes over HTTPS.

Optional sync (Settings → Data) keeps chats, notes and tasks the same on the phone and the computer (`/api/sync`).

### Permissions

| Permission | Why | When |
| --- | --- | --- |
| Internet, network state | Reach the AI providers (or your computer) | Always |
| Microphone | Voice input and live voice | Asked the first time you tap the mic |
| Notifications | Reminders, "answer ready" | Asked when you first set a reminder (Android 13+) |
| Exact alarms | Reminders ring on time | Offered once; without it they may be a few minutes late |
| Run at start-up | Re-arm reminders after a reboot | Automatic |
| Foreground service | Finish a long answer or agent run while you use other apps | Only while work is running |
| Install packages | Install updates downloaded from the official site | Android asks you to allow it the first time |

App backups are off (`allowBackup=false`; data-extraction rules exclude everything).

### Updates

The app reads the `android` section of <https://esoexe.github.io/astro-ai/latest.json> (at most every 12 hours, or
when you tap Check), downloads the APK over HTTPS, checks its **SHA-256** against the feed and hands it to Android's
installer. Android itself refuses any APK that is not signed with the same key as the installed app. Turn automatic
checks off in Settings → About.

The 1.8.0 app (the WebView version) cannot tell you about 1.8.1: its update check compares the desktop version number.
Install 1.8.1 over it from the download page.

## Building

You need: the Android NDK r30+, JDK 17, the Android SDK (platform 37, build-tools 36) and Gradle 9.6+.

```bash
# 1. the engine: OpenSSL 3.5.9, curl 8.23.0 and the engine, from pinned sources (about 10 minutes the first time)
ANDROID_NDK_ROOT=/path/to/android-ndk-r30 bash android/build_engine.sh

# 2. optional: test the engine on a Linux PC under qemu (needs qemu-user-static)
ASTRO_ANDROID_STATIC_TEST=1 ANDROID_NDK_ROOT=... bash android/build_engine.sh
bash android/test_engine_qemu.sh

# 3. translations and icons (generated; CI fails if they are stale or a UI string has no French/Arabic)
node tools/android_i18n.js && node tools/android_icons.js

# 4. tests, lint and the APK
gradle -p android testDebugUnitTest lintRelease assembleDebug
```

On a machine with about 2 GB of RAM, run Kotlin compilation, R8 and packaging as separate Gradle runs (fresh JVMs),
for example `gradle :app:compileReleaseKotlin`, then `gradle :app:minifyReleaseWithR8`, then `gradle assembleRelease`.

The shipped engine is linked against the phone's own `libc.so` for **API 26**, with OpenSSL, curl, zlib and libc++
linked in statically. Linking against the API-26 stubs proves at build time that nothing newer than Android 8.0 is
used. A fully static bionic binary is built **only for the qemu test** and never shipped.

UI strings are looked up by their English text, like the web app (`T("...")` in Compose, `tr("...")` elsewhere).
Add new strings to [`android/i18n-native.json`](../android/i18n-native.json) with French and Arabic.

## Releases and signing

Releases are built by [`.github/workflows/android.yml`](../.github/workflows/android.yml) when a tag like
`android-v1.8.1` is pushed (the tag must match `android/app-version.txt`). It:
1. checks translations and icons, builds the engine and runs the qemu test;
2. runs the unit tests and lint, builds the APK and signs it with the project key from the
   `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS` secrets;
3. checks that the certificate matches [`android/signing-cert.sha256`](../android/signing-cert.sha256) and that no
   key file is inside the APK;
4. creates a GitHub release with `AstroAI-<version>-android-arm64.apk` and its SHA-256 (not marked "Latest", so the
   desktop release keeps that).

Then the APK goes into the website's downloads and `website/site.json` → `android.version` is updated, which
publishes the `android` section of `latest.json`.

Release signing certificate (SHA-256):

```
51:53:44:2D:71:CE:AB:E7:DC:78:5B:E5:59:BB:BA:84:2A:E0:50:7B:19:44:56:5D:23:9B:7B:89:42:F7:16:84
```

Check a downloaded APK with `apksigner verify --print-certs AstroAI-*.apk`.

**The signing key must never change.** Android only installs an update over an existing app when it is signed with
the same key. The maintainer keeps the keystore offline and in the repository secrets; neither the keystore nor its
password is ever committed (the export script removes `*.jks` and `keystore.properties`). See
[releasing.md](releasing.md) for recovering it from the secrets.

## Status

New in 1.8.1, and honest about it:
- Built and tested on a computer: unit tests, lint, the engine under qemu, and the app's engine API against a real
  engine. **Not yet tried on many real phones.** Please report how it runs:
  <https://esoexe.github.io/astro-ai/beta.html>.
- No iOS app.

## Open-source licenses

The APK contains OpenSSL, curl, the Mozilla CA list, LLVM libc++, the Kotlin standard library, AndroidX and Jetpack
Compose, and the Inter, Instrument Serif and JetBrains Mono fonts. Their license texts and a notice are inside the APK
under `assets/licenses/` (Settings → About → Licenses lists them), and in
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
