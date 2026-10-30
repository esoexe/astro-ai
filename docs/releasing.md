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
| `tests/library_v2_e2e.sh` | Community catalog installs (skills, plugins, MCP servers, AI tools) and the fingerprint and hook rules |
| `tests/quality_test.sh` | Background code check, model health, feedback |

UI tests (Playwright, optional): `tests/ui/gap_ui.js` and `tests/ui/chat_ui.js`.

## GitHub Actions

| Workflow | When it runs | What it does |
|---|---|---|
| `ci.yml` | Every push and pull request | Fails on tracked private files, runs the secret scan, then `make`, `make test` and the website build |
| `pages.yml` | Changes to `website/**` or `library/**` | Builds and deploys the site and library |
| `desktop.yml` | A `v*` tag, or run by hand | Builds the **Clean** edition for Windows (installer and zip), Linux (`.deb`, `tar.gz`) and macOS (universal `.dmg`), each with the `astro` terminal app inside, plus the VS Code extension (`.vsix`). Tags also create a GitHub release with `SHA256SUMS.txt` |

## Code signing (optional)

Without signing, Windows SmartScreen and macOS Gatekeeper warn on first launch. The Desktop workflow signs
automatically once the repository has the right secrets and variables. **Step-by-step guide: [signing.md](signing.md).**

* Windows, free for open source: **SignPath Foundation**. Needs secret `SIGNPATH_API_TOKEN` and variable `SIGNPATH_ORGANIZATION_ID`; each release waits for your approval on signpath.io.
* Windows with your own certificate: `WINDOWS_CERT_PFX_BASE64` and `WINDOWS_CERT_PASSWORD`.
* macOS: `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY`; for notarization also `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.

## Release checklist

1. Bump the version in every place it appears:
   * `desktop/package.sh` (`VER`) and `kVersion` in `desktop/src/astro_desktop.cpp`
   * `cli/astro.cpp` (`VERSION`; must equal `ASTRO_APP_VERSION`) and `desktop/installer/astro.nsi`
   * (the Windows version resources now follow `VER` automatically)
   * `website/site.json` (`version`, `dateLabel`, `highlights` and their French/Arabic `highlights_i18n`) and the changelog page
2. Run `make test`.
3. Build: `bash desktop/package.sh` locally, or push a tag for CI (Clean edition only).
4. Run `bash tools/update_github.sh "Release x.y.z"`. It exports, scans for secrets and pushes.
