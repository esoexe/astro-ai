# Testing and releasing

## Tests

```bash
make && make test
```

| Test | What it checks |
|---|---|
| `tests/smoke_cpp.sh` | Core API, extensions, login |
| `tests/agent_parse_test.js` | Agent tool-call parser and the reasoning-leak filter |
| `tests/code_detect_test.js` | Code-request detection |
| `library/build_library.js --check` | Library rules |
| `tests/library_e2e.sh` | Library install and uninstall against a local catalog |
| `tests/quality_test.sh` | Background code check, model health, feedback |

UI tests (Playwright, optional): `tests/ui/gap_ui.js` and `tests/ui/chat_ui.js`.

## GitHub Actions

| Workflow | When it runs | What it does |
|---|---|---|
| `ci.yml` | Every push and pull request | Fails on tracked private files, runs the secret scan, then `make`, `make test` and the website build |
| `pages.yml` | Changes to `website/**` or `library/**` | Builds and deploys the site and library |
| `desktop.yml` | A `v*` tag, or run by hand | Builds the **Clean** edition for Windows (installer and zip), Linux (`.deb`, `tar.gz`) and macOS (universal `.dmg`). Tags also create a GitHub release with `SHA256SUMS.txt` |

## Code signing (optional)

Without signing, Windows SmartScreen and macOS Gatekeeper warn on first launch. Add the following repository secrets (Settings > Secrets and variables > Actions) to sign automatically.

**Windows (Authenticode)**

* `WINDOWS_CERT_PFX_BASE64` is a base64 `.pfx`; `WINDOWS_CERT_PASSWORD` is its password.
* Open-source projects can get free signing from the **SignPath Foundation** (signpath.org). It signs through its own GitHub action instead of a `.pfx`; replace the "Sign Windows installer" step with theirs once approved.

**macOS (Developer ID)**, which needs an Apple Developer account:

* `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, and `MACOS_SIGN_IDENTITY` (for example `Developer ID Application: Name (TEAMID)`).
* For notarization: `APPLE_ID`, `APPLE_TEAM_ID`, and `APPLE_APP_PASSWORD` (an app-specific password).

## Release checklist

1. Bump the version in every place it appears:
   * `desktop/package.sh` (`VER`) and `kVersion` in `desktop/src/astro_desktop.cpp`
   * `desktop/src/astro_desktop.rc`, `desktop/setup/build_setup.sh` and `desktop/installer/astro.nsi`
   * `website/site.json` and the changelog page
2. Run `make test`.
3. Build: `bash desktop/package.sh` locally, or push a tag for CI (Clean edition only).
4. Run `bash tools/update_github.sh "Release x.y.z"`. It exports, scans for secrets and pushes.
