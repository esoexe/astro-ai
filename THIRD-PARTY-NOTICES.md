# Third-party notices

A.S.T.R.O AI's own code is licensed under MIT OR Apache-2.0 OR GPL-3.0-only (see `LICENSE`).
These bundled components keep their own licenses. Their full license texts ship next to them in this repo.

| Component | Where | License | License text |
|---|---|---|---|
| miniz 3.0.2 | `desktop/third_party/miniz/` | MIT | `desktop/third_party/miniz/LICENSE` |
| webview (Serge Zaitsev and contributors) | `desktop/third_party/webview/` | MIT | `desktop/third_party/webview/LICENSE` |
| Microsoft WebView2 SDK headers | `desktop/third_party/webview2/` | BSD-style (Microsoft) | `desktop/third_party/webview2/LICENSE.txt` |
| Microsoft Edge WebView2 Runtime bootstrapper | `desktop/third_party/webview2-bootstrapper/` | Microsoft redistributable | Distributed unmodified, as Microsoft permits, so the installer can add the WebView2 Runtime when it's missing |
| curl and its dependencies for Windows (curl, LibreSSL, libssh2, nghttp2, nghttp3, ngtcp2, brotli, zstd, zlib-ng, libpsl) | `desktop/third_party/curl-win64/` | curl, ISC/OpenSSL-style, BSD, MIT, zlib licenses | `desktop/third_party/curl-win64/licenses/` |
| three.js r186 and OrbitControls | `web/vendor/three.module.min.js`, `web/vendor/OrbitControls.js` | MIT | https://github.com/mrdoob/three.js/blob/dev/LICENSE |
| CodeMirror 6 and Lezer (bundled in `web/vendor/ide.js`) | `web/vendor/ide.js`, built from `tools/ide-vendor/` | MIT | https://github.com/codemirror/dev/blob/main/LICENSE |
| xterm.js and addon-fit (bundled in `web/vendor/ide.js`) | `web/vendor/ide.js`, `web/vendor/xterm.css` | MIT | https://github.com/xtermjs/xterm.js/blob/master/LICENSE |
| Inter, Instrument Serif, JetBrains Mono fonts | `web/vendor/*.woff2`, `website/src/static/assets/fonts/`, `desktop/setup/fonts/` | SIL Open Font License 1.1 | `web/vendor/FONTS-LICENSE.txt` |
| OpenSSL 3.5.9 (Android app only, linked into the engine) | built by `android/build_engine.sh` | Apache-2.0 | shipped in the APK as `assets/licenses/OpenSSL-LICENSE.txt` |
| curl / libcurl 8.23.0 (Android app only) | built by `android/build_engine.sh` | curl license | shipped in the APK as `assets/licenses/curl-COPYING.txt` |
| Mozilla CA certificate list (curl.se extract, Android app only) | APK `assets/cacert.pem`, unmodified | MPL-2.0 | https://www.mozilla.org/MPL/2.0/ ; source https://curl.se/docs/caextract.html |
| LLVM libc++ (Android NDK, Android app only) | linked into the engine | Apache-2.0 WITH LLVM-exception | https://llvm.org/LICENSE.txt |
| Kotlin standard library, kotlinx.coroutines (Android app only) | Maven dependencies of `android/app` | Apache-2.0 | https://github.com/JetBrains/kotlin |
| AndroidX (core, activity, lifecycle) and Jetpack Compose with Material 3 (Android app only) | Maven dependencies of `android/app` | Apache-2.0 | https://developer.android.com/jetpack/androidx ; APK `assets/licenses/NOTICE.txt` |
| Inter, Instrument Serif, JetBrains Mono (Android app) | `android/app/src/main/res/font/` (TTF copies of `desktop/setup/fonts/`) | SIL Open Font License 1.1 | APK `assets/licenses/FONTS-LICENSE.txt` |

**Downloaded on request, not bundled:** the optional *Natural voice* installs Piper (MIT, https://github.com/rhasspy/piper) and voice
models from `rhasspy/piper-voices` with their own licenses, shown before you install: en_US ljspeech (public domain),
fr_FR siwis (CC BY 4.0), ar_JO kareem (personal use only, as published by its author). Each download is checked against a pinned SHA-256.

AI providers listed in `providers.json` are third-party services with their own terms. A.S.T.R.O AI is not affiliated with them.
