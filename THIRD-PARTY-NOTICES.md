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

AI providers listed in `providers.json` are third-party services with their own terms. A.S.T.R.O AI is not affiliated with them.
