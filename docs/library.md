# The A.S.T.R.O AI Library

The Library has two parts:

* **Reviewed items**: a small catalog of MCP servers, skills and plugins checked by the A.S.T.R.O AI team (`library/catalog.json`, schema 1).
* **Community catalog** (schema 2): every MCP server in the [official MCP Registry](https://registry.modelcontextprotocol.io); MCP servers published on npm, as Hugging Face Spaces, in Docker's MCP catalog and as GitHub projects; Agent Skills and Claude Code plugins found in public GitHub repositories; Open VSX editor extensions; AI command-line tools (npm and PyPI); and a directory of AI apps and sites taken from open "awesome" lists. It is built by `library/import/index_all.js` and re-indexed every day by the Website workflow. Community items are labelled *Community* and are not reviewed by hand.

You can browse it in two places:

* **Console** on the website (`/profile` > Library). You don't need to sign in. Each item has *Open in A.S.T.R.O AI*, a copyable config for Claude Code, Cursor, VS Code and OpenCode, and a zip download.
* **A.S.T.R.O AI Desktop**: Workspace > Extensions > Library. Each item installs with one click.

## Astro Packs (1.9)

The **Astro Packs** tab lists 21 packs. 18 are adapted from [Jake Schincariol](https://github.com/Jakeschincariol)'s MIT-licensed agent
skills: YouTube, Instagram, LinkedIn, X, Founder, Replica, Arena, Duel, Hunter, Trader, Day Trader, Master, Console, Modes,
Power Modes, Promptmaster, Closer (a small sales team: leads, outreach, follow-ups, call coaching,
proposals, contracts and a forecast; needs A.S.T.R.O AI 1.9.1 or later) and Seed (a startup raise: competitor research,
market size, investor matching from public sources and SEC filings, plan, model, deck, investor updates, call prep, a
term-sheet reader, a data room and a closer that drafts investor emails and books calls; needs A.S.T.R.O AI 1.9.2 or later). They are in the reviewed catalog, labelled *Community: adapted from Jake Schincariol (MIT)*, and
each one links to the exact upstream commit it was adapted from. (`chatgpt-youtube-agent-skill` is the same eleven YouTube
skills and helpers retargeted at ChatGPT, so the YouTube Pack covers it; `muesli` is a macOS Swift app, and the repos with
no license are not adapted.)

* To use an installed pack, type `/` in the Agent (the Agent page, or Workspace in Agent mode). The menu lists every
  installed skill with its description; pick one with the arrow keys and Enter or Tab, then add what you want, for
  example `/seed-plan a meal-kit delivery startup in Algiers` (1.9.3 or later). `/skills` lists them too.
  On a new Agent session, *From your packs* shows one chip per installed pack; a chip opens the same menu with only
  that pack's skills (1.9.4 or later).
* The original Python and Node helper scripts are rewritten in C++ and built into the engine (`packtools/`), so a pack
  needs no Python or Node. Its skill runs them as `astro-server tool <skill>/<helper>`; 16 packs have helpers, the other 5
  are instructions only.
* The source commits are pinned in `tools/jake_shas.txt`; `tools/port_packs.js` converts a repo into a pack and
  `library/pack-overrides/` holds hand-written changes. Each pack ships the upstream licence in `NOTICE.md`.
* The trading packs (Trader, Day Trader) are for research and education, not financial advice. Hunter is a job-search pack.

## Design tools (1.9.5)

For websites, 3D sites, web apps, mobile apps and desktop software. Every item names its upstream source and licence.

| Item | Kind | Source and licence | What it does |
|---|---|---|---|
| **Design Pack** (`astro-design`) | Astro Pack, built-in helper | [UI/UX Pro Max](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill) (MIT), [frontend-design](https://github.com/anthropics/skills/tree/main/skills/frontend-design) (Apache-2.0), design-director by the A.S.T.R.O AI team | `design-director` runs the job from a written brief to a visual check; `ui-ux-pro-max` is the design database with its search rewritten in C++ (`astro-server tool ui-ux-pro-max/search`); `frontend-design` is Anthropic's guidance for non-template visuals. Needs 1.9.5. |
| **3D Pack** (`astro-3d`) | Astro Pack, instructions | [threejs-skills](https://github.com/CloudAI-X/threejs-skills) (MIT) | 13 three.js and React Three Fiber skills, from scene setup to shaders, post-processing, performance and the glTF pipeline. |
| **Motion Pack** (`astro-motion`) | Astro Pack, instructions | [gsap-skills](https://github.com/greensock/gsap-skills) (MIT, official); StringTune skill by the Astro AI team | 8 GSAP skills: core, timelines, ScrollTrigger, plugins, utilities, frameworks, performance. Plus `stringtune`: [StringTune](https://string-tune.fiddle.digital) (Fiddle.Digital, MIT), CSS-first smooth scroll and attribute-driven effects. StringTune is a separate library, not a GSAP plugin. |
| **21st.dev Magic** (`21st-magic`) | Remote MCP | [21st.dev](https://21st.dev/mcp) (ISC) | Generates and finds React + Tailwind components. Needs your own 21st.dev key (sent as `x-api-key`); the free plan has a small monthly allowance. |
| **shadcn/ui** (`shadcn`) | Local MCP | [shadcn](https://ui.shadcn.com/docs/mcp) (MIT) | Browses and installs components from shadcn registries with the project's CLI (`npx shadcn mcp`). Needs 1.9.5, which allows a bare subcommand word in MCP arguments. |
| **Magic UI** (`magic-ui`) | Local MCP | [magicuidesign/mcp](https://github.com/magicuidesign/mcp) (MIT) | Animated React + Tailwind components with their code. |
| **Mobile (iOS and Android)** (`mobile-mcp`) | Local MCP | [mobile-next/mobile-mcp](https://github.com/mobile-next/mobile-mcp) (Apache-2.0) | Drives simulators, emulators and devices so the agent can check the app it built. Needs adb or Xcode. |

Already in the catalog and used by the design director for the visual check: **Playwright Browser**, **Chrome DevTools** and **Figma Context**.

How the UI/UX Pro Max port differs from the Python original (`packtools/design_uipro.inc`, tested by
`packtools/tests/design_uipro.sh`, which mirrors the upstream tests): same BM25 ranking, tokenizer, domain detection, score
floors, exact-identity lookups and design-system rules. Not included: `google-fonts.csv` (font questions are answered from the
74 curated pairings), the font-licence and Phosphor icon dumps. The stack version check is simpler (an explicit "legacy" or
"deprecated" asks for older guidance), `--format ascii` prints the same Markdown as `--format markdown`, and the
`--design-system --json` layout is A.S.T.R.O AI's own.

Not added: Expo's MCP server (OAuth sign-in only), Blender MCP (needs Python and `uvx`), and design galleries whose files
have no clear licence.

### Colibri (local models)

[Colibri](https://github.com/JustVugg/colibri) (Apache-2.0) runs very large mixture-of-experts models on an ordinary
computer by streaming the experts from disk. It is a model engine, not a Library item: install it with its own installer
(`START-HERE.bat` on Windows, `./start-here.sh` on Linux and macOS; it needs at least 8 GB of RAM and about 22 GB of free
disk), start it, and A.S.T.R.O AI finds it under Settings > Providers > **Colibri** at `http://127.0.0.1:8000/v1`. It
answers slower than a cloud model. Set `COLIBRI_URL` if it listens elsewhere, and `COLI_API_KEY` only if you gave Colibri
one. vLLM uses the same default port: each provider keeps only its own models (by `owned_by`), so they never double up.
Colibri's own `coli mcp` server can be added by hand under Extensions > MCP.

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
| `state-gh.json` | Repos found by GitHub topic search, where the search sweep stopped, and the npm versions already checked, so each daily run continues instead of starting over |

Sources (`library/import/sources.json`):

* **MCP servers**: the official MCP Registry API. A server gets one-click setup when it has a Streamable HTTP remote, or an npm, PyPI or OCI (Docker) package. Packages are pinned to the version the registry lists (`npx -y pkg@x.y.z`, `uvx pkg==x.y.z`, `docker run -i --rm image:tag`). Environment variables, headers and URL values the registry declares become fields in the install dialog.
* **Skills and plugins**: shallow, blob-less `git clone`s of public GitHub repos (official repos first, then repos linked from open awesome lists, then repos found by GitHub topic search such as `claude-skills` and `claude-code-plugin`). Every folder with a `SKILL.md` is a skill; every folder with `.claude-plugin/plugin.json` is a plugin. The catalog records the commit and the SHA-256 of every file. When the same skill appears in several repos, the most trusted repo wins.
* **More MCP servers**, each added only when no registry entry already covers the same package, image or repo:
  * **npm**: packages found by the npm registry search API (`npm_queries` in `sources.json`) that declare a command (`bin`). Pinned like registry packages (`npx -y pkg@x.y.z`). Libraries without a command are left out. Package metadata is cached by version.
  * **Hugging Face Spaces** tagged `mcp-server`: Gradio Spaces become remote SSE servers (`https://<space>.hf.space/gradio_api/mcp/sse`); other Spaces are link only.
  * **Docker's MCP catalog**: the `servers/*/server.yaml` files in the [docker/mcp-registry](https://github.com/docker/mcp-registry) git repo (MIT). Listed as links with the `docker run` command to use; Docker Desktop's MCP Toolkit installs them.
  * **GitHub topic search** (`mcp_topics`): MCP projects without a package. Link only.
* **Editor extensions**: the Open VSX search API, split by category and sort order so every extension is reached (`openvsx_categories`). Deprecated ones are left out. They install through the Code tool (Extensions → VS Code), which downloads from Open VSX and checks the checksum. A.S.T.R.O AI 1.9.5 and older show them as link only.
* **AI tools**: `library/import/cli_tools.json` (versions resolved from npm and PyPI at build time) plus open awesome lists of AI apps.

Directories that ask not to be crawled (`/api/` in their robots.txt) are not used, and neither is PyPI's JSON API (its robots.txt disallows it). Requests are spaced out (npm 1.2 s, Open VSX 2 s as its robots.txt asks, GitHub search 2.1 s). Names and descriptions matching a short list of unsafe terms (sexual content, "undress" and nudify tools, malware, jailbreaks) are left out; the manifest counts them as `filtered`.

Topic search is resumable: GitHub returns at most 1,000 results per query, so each topic is walked in creation-date slices, and `state-gh.json` records where the walk stopped. Each run first searches repos pushed since the last run, then continues the walk for up to 90 minutes. It takes several daily runs to finish a full pass. Repos are then cloned in priority order (official, awesome lists, topics by stars) until the run's time budget is used; unchanged repos are reused from the previous build.

What the catalog does not include: other directories' private APIs, PyPI packages found by name only, closed marketplaces, and duplicates. The counts on the Library page are the real number of entries, not a target.

### The big catalog (schema 3, 1.9.7)

The website can hold only so much (GitHub Pages sites are limited to 1 GB), and loading every row into the app does
not scale. Since 1.9.7 the full community catalog also has its own site, **https://esoexe.github.io/astro-library/v3/**,
built once a day by the workflow in `library/site/` (published as the `esoexe/astro-library` repository):

| File | What it holds |
|---|---|
| `manifest.json` | Counts and row ranges per kind, sources, the search-index prefixes |
| `list/<kind>-<n>.json.gz` | The same rows as schema 2, 500 per file, in rank order |
| `detail/<hhh>.json.gz` | Install details keyed by id; `hhh` is the first three hex characters of `sha256(id)` |
| `q/<xx>.json.gz` | Search index: word → row numbers (base-36 gaps), one file per two-letter word start |
| `state.json.gz`, `state-gh.json.gz` | Indexer state (commits, ids, topic and code search cursors, npm versions); not read by the app |

The app and the Console only download the pieces you look at; `web/lib3core.js` is the reader and the tokenizer
(the indexer uses the same file). If the library site can't be reached, they fall back to the schema 2 copy on the
website, which keeps working for older versions of the app.

The schema 3 build uses the same sources as schema 2, plus (`--extra`) **GitHub code search for `SKILL.md`
files**, in file-size windows that continue where the last build stopped (code search allows 10 requests a minute,
so a full pass takes many days). Its first run starts from the schema 2 build's state (`--prev2`), so repositories
already indexed are reused instead of cloned again. Counts change every day; the manifest and the site's front page
show the current numbers.

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

Set `ASTRO_LIBRARY_URL` to test with a different catalog, for example `http://127.0.0.1:3000/library/index.json` from `website/serve.js`. The community catalog is read from `v2/` next to it, and the schema 3 catalog from `v3/` next to it (or from `ASTRO_LIBRARY_V3_URL`). `ASTRO_LIBRARY_RAW_BASE` replaces `https://raw.githubusercontent.com/` (tests only).

To rebuild the community catalog yourself: `node library/import/index_all.js out/v2 --reviewed website/dist/library/index.json` (set `GITHUB_TOKEN` to include GitHub topic search). `--only mcp,npm,hf,docker,openvsx,tools,apps,skills,ghmcp` runs some sources only; `--prev <dir>` continues from an earlier build instead of the published one. `bash tests/indexer_sources_e2e.sh` tests the extra sources offline against fake services.
