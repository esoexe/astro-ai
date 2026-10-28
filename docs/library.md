# The A.S.T.R.O AI Library

The Library has two parts:

* **Reviewed items**: a small catalog of MCP servers, skills and plugins checked by the A.S.T.R.O AI team (`library/catalog.json`, schema 1).
* **Community catalog** (schema 2): every MCP server in the [official MCP Registry](https://registry.modelcontextprotocol.io), Agent Skills and Claude Code plugins found in public GitHub repositories, AI command-line tools (npm and PyPI), and a directory of AI apps and sites taken from open "awesome" lists. It is built by `library/import/index_all.js` and re-indexed every day by the Website workflow. Community items are labelled *Community* and are not reviewed by hand.

You can browse it in two places:

* **Console** on the website (`/profile` > Library). You don't need to sign in. Each item has *Open in A.S.T.R.O AI*, a copyable config for Claude Code, Cursor, VS Code and OpenCode, and a zip download.
* **A.S.T.R.O AI Desktop**: Workspace > Extensions > Library. Each item installs with one click.

## Installing

1. Click **Open in A.S.T.R.O AI** in the Console. This opens `astroai://library/<id>`. You can also open the Library tab in the app.
2. A.S.T.R.O AI shows the item's publisher, version, permissions and the exact command or URL it will use. Nothing is installed until you click **Install**.
3. If the item needs a token (for example a GitHub or Notion token), you type it in the app. The token is written only to your private local `mcp.json` and is never sent to A.S.T.R.O AI.
4. To uninstall, use the same dialog. It removes exactly the files listed in the install receipt (`ext/library.json`).

## Why installs are safe

The installer runs in the C++ server (`astro_library.inc`). The website and the browser only point at an item id; they never decide which files get written.

| Rule | Where it is enforced |
|---|---|
| The catalog is downloaded by the app itself over https (plain http only from `127.0.0.1` for tests) | `library_url_ok` |
| Every file must match the SHA-256 and size listed in the catalog | install, before anything is moved into place |
| Skills and plugins are text only (`.md`, `.json`, `.txt`). No scripts, hooks, styles or binaries | `build_library.js` **and** the installer |
| A plugin's `.mcp.json` may list only remote `https` servers | both |
| MCP packages are pinned to an exact version (`npx -y pkg@1.2.3`). Arguments cannot contain shell syntax | both |
| Files that look like secrets are rejected | `build_library.js` |
| Installs go to a staging folder first and replace the old version only when complete | installer |
| Local MCP servers start with a cleaned environment, without A.S.T.R.O AI's provider keys. They only get the variables the item declares | env scrub in `astro_dev.inc` |
| Ids are `[a-z0-9-]{1,48}`. Deep links with anything else are ignored | desktop launcher + web app |

## How the community catalog is built

`library/import/index_all.js` writes `library/v2/` on the website:

| File | What it holds |
|---|---|
| `manifest.json` | Counts per kind, the list of shard files, and the sources with their counts |
| `list/<kind>-<n>.json` | Compact rows for browsing and search: `[id, name, summary, publisher, flags, tags, extra, stars]`, 5,000 per file |
| `detail/<hh>.json` | Everything needed to install, keyed by id. `hh` is the first two hex characters of `sha256(id)` |
| `state.json` | Which commit of each GitHub repo was indexed, so unchanged repos are not cloned again |

Sources (`library/import/sources.json`):

* **MCP servers**: the official MCP Registry API. A server gets one-click setup when it has a Streamable HTTP remote, or an npm, PyPI or OCI (Docker) package. Packages are pinned to the version the registry lists (`npx -y pkg@x.y.z`, `uvx pkg==x.y.z`, `docker run -i --rm image:tag`). Environment variables, headers and URL values the registry declares become fields in the install dialog.
* **Skills and plugins**: shallow, blob-less `git clone`s of public GitHub repos (official repos first, then repos linked from open awesome lists, then repos found by GitHub topic search such as `claude-skills` and `claude-code-plugin`). Every folder with a `SKILL.md` is a skill; every folder with `.claude-plugin/plugin.json` is a plugin. The catalog records the commit and the SHA-256 of every file. When the same skill appears in several repos, the most trusted repo wins.
* **AI tools**: `library/import/cli_tools.json` (versions resolved from npm and PyPI at build time) plus open awesome lists of AI apps.

Directories that ask not to be crawled (`/api/` in their robots.txt) are not used.

## Community install rules

| Rule | Where it is enforced |
|---|---|
| MCP packages run at an exact version; `docker` refuses `:latest`, `--privileged`, host networking and device flags | indexer **and** `lib_install_community` |
| Skill and plugin files are downloaded from `raw.githubusercontent.com/<repo>/<commit>/…` and must match their SHA-256 | installer |
| Text files only (an allowlist of source and document extensions, no NUL bytes). Size caps per file and per item | both |
| No hooks (`hooks/`, `hooks.json`, the `hooks` key in `plugin.json`), no A.S.T.R.O plugin UI code (`main.js`, `style.css`) | both |
| Community plugins keep only remote `https` MCP servers; local ones are listed in the install result for you to add by hand | installer |
| Skill scripts are saved as text. They only run if the agent runs them, under your permission mode (Plan / Ask / Auto) | agent |
| Command-line tools install into `ext/tools/<id>` (npm `--prefix`, or a Python venv; never system-wide). Only the commands the tool declares are linked into `ext/tools/.bin`, which is added to PATH for the agent, terminal and MCP servers, so a tool's dependencies never shadow your own programs | installer |

## Adding an item

1. Add the item to `library/catalog.json`:
   * `kind` is `mcp`, `skill` or `plugin`.
   * `runs` is `remote` or `local`.
   * Also give `permissions`, `publisher`, `homepage` and `license`.
2. Put the item's files under `library/skills/<id>/` or `library/plugins/<id>/`.
3. Run `node library/build_library.js --check`. It must print no errors.
4. Run `bash tests/library_e2e.sh`. This installs and uninstalls against a local catalog.
5. Push. The website workflow rebuilds `library/index.json` with fresh hashes.

## `astroai://` links

| OS | Registration |
|---|---|
| Windows | The installer writes `HKCU\Software\Classes\astroai`. Uninstall removes it only if the key still points to this install. |
| Linux | The `.deb` and portable `install.sh` register `x-scheme-handler/astroai` |
| macOS | Not registered yet. Use the Library tab in the app instead. |

The launcher accepts only `astroai://library/<id>` and turns it into `#library=<id>` for the web app. That opens the item's dialog; it never installs anything on its own.

Set `ASTRO_LIBRARY_URL` to test with a different catalog, for example `http://127.0.0.1:3000/library/index.json` from `website/serve.js`. The community catalog is read from `v2/` next to it. `ASTRO_LIBRARY_RAW_BASE` replaces `https://raw.githubusercontent.com/` (tests only).

To rebuild the community catalog yourself: `node library/import/index_all.js out/v2 --reviewed website/dist/library/index.json` (set `GITHUB_TOKEN` to include GitHub topic search).
