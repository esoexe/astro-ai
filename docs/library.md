# The Astro AI Library

The Library is a reviewed catalog of **MCP servers**, **skills** and **plugins**. You can browse it in two places:

* **Console** on the website (`account.html` > Library). You don't need to sign in. Each item has *Open in Astro AI*, a copyable config for Claude Code, Cursor, VS Code and OpenCode, and a zip download.
* **Astro AI Desktop**: Workspace > Extensions > Library. Each item installs with one click.

## Installing

1. Click **Open in Astro AI** in the Console. This opens `astroai://library/<id>`. You can also open the Library tab in the app.
2. Astro AI shows the item's publisher, version, permissions and the exact command or URL it will use. Nothing is installed until you click **Install**.
3. If the item needs a token (for example a GitHub or Notion token), you type it in the app. The token is written only to your private local `mcp.json` and is never sent to Astro AI.
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
| Local MCP servers start with a cleaned environment, without Astro AI's provider keys. They only get the variables the item declares | env scrub in `astro_dev.inc` |
| Ids are `[a-z0-9-]{1,48}`. Deep links with anything else are ignored | desktop launcher + web app |

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

Set `ASTRO_LIBRARY_URL` to test with a different catalog, for example `http://127.0.0.1:3000/library/index.json` from `website/serve.js`.
