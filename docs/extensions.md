# Themes, language tools and VS Code extensions (1.6)

A.S.T.R.O AI 1.6 adds three ways to extend the editor. All three live in **Extensions** in the Workspace sidebar, and the terminal app can manage them too.

| | What it is | Where |
|---|---|---|
| **Themes** | 19 built-in colour themes, plus any colour theme from Open VSX, applied to the whole app | Settings › Appearance, Extensions › Themes, command palette, `/theme` in the terminal |
| **Tools** | Compilers, runtimes and language servers, installed portably | Extensions › Tools, `astro tools` |
| **VS Code** (preview) | Desktop VS Code extensions from Open VSX, run in an extension host | Extensions › VS Code, `astro ext` |

## Themes

`web/themes.js` (`window.AstroThemes`) holds the built-in palettes. Each one sets a small set of colours: frame, background, surfaces, text, accent, danger/ok/warn and syntax colours. It derives every CSS variable the app uses from those, so chat, Workspace, editor, terminal (xterm) and the Agent page all change together. The theme is cached before the first paint, so pages don't flash.

Themes installed from Open VSX are converted the same way. The VS Code `colors` map gives the surfaces, accent and text, and `tokenColors` give the syntax colours. Contrast guards fix text that would be too faint on its background. `uiTheme` decides the base: `vs` = light, `vs-dark` = dark, `hc-black` = high-contrast dark, `hc-light` = high-contrast light.

The terminal app has the same theme names (`/theme`). It saves the choice in `~/.config/astro-ai/tui.conf` (`%APPDATA%\astro-ai\tui.conf` on Windows). `ASTRO_THEME=<name>` overrides it.

## Language tools

### Installs

The engine (`astro_tools.inc`) has a catalog of 19 tools: clangd, C/C++ compiler, CMake, Ninja, Node.js, TypeScript, VS Code HTML/CSS/JSON servers, Python, Pyright, rust-analyzer, Go, gopls, JDK, jdtls, Bash, YAML, Lua and PHP (Intelephense) language servers.

- **Portable, no admin.** Everything goes into `<data dir>/tools/<id>/`, and A.S.T.R.O puts those folders first on its own `PATH`. Nothing is installed system-wide, so removing a tool just deletes its folder.
- **Official sources only.** Downloads come from GitHub releases of the projects themselves, nodejs.org, go.dev, Adoptium and download.eclipse.org, over HTTPS only. Each download is capped at 900 MB.
- **Verified.** Each download is checked against the SHA-256 the project publishes: the GitHub release asset digest, `SHASUMS256.txt` for Node.js, the go.dev JSON for Go, or the Adoptium API for the JDK. The manifest records how each tool was verified: `sha256`, `npm` (npm's own integrity checks), `go` (Go module checksums) or `https-only`.
- **Compilers.** On Windows the C/C++ compiler is LLVM-MinGW (portable clang). On Linux and macOS, A.S.T.R.O finds the system compiler and, if there isn't one, says how to get it (`xcode-select --install`, `apt install build-essential`, …). Rust is only found if it's already installed, because rustup is an installer.

Endpoints (POST, JSON, `X-Astro-Client: 1`): `/api/dev/tools/list {detect}`, `/tools/install {id}` (runs in the background; poll `list` for `job.progress`), `/tools/cancel`, `/tools/remove`.

### Language servers in the editor

The engine runs language servers over stdio and relays JSON-RPC to the browser over HTTP (`/api/dev/lsp/start|send|recv|stop|status`, long-polling `recv`). `web/lsp.js` (`window.AstroLSP`) is the client. It keeps one server per project and language (at most 4), syncs open files and maps results into CodeMirror:

- completion (with snippets and docs) and hover
- push and pull diagnostics, shown in the editor and in Problems
- go to definition (F12 or Ctrl+click)
- formatting (Shift+Alt+F) and rename (F2, across files)

Notes:

- TypeScript 7 is native and has no `tsserver.js`. A.S.T.R.O runs `tsc --lsp --stdio` and uses pull diagnostics.
- jdtls's launcher is a Python script, so A.S.T.R.O starts its Equinox jar with Java directly.
- Microsoft's `ms-vscode.cpptools` may only be used with Microsoft's own VS Code products and isn't on Open VSX. A.S.T.R.O uses **clangd** for C and C++. Add a `compile_commands.json` (CMake: `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) for multi-file projects.

## VS Code extensions (preview)

### How it works

```
browser (web/exthost.js)  ⇄  engine /api/dev/lsp/* (lang "exthost")  ⇄  node web/exthost-node.js  ⇄  extensions
```

- **Install.** `POST /api/dev/exthost/install {namespace, name}` gets the extension's metadata from `open-vsx.org`, downloads the `.vsix` and checks it against Open VSX's published SHA-256. It then extracts `extension/` into `<data dir>/ext/vscode/<publisher>.<name>/` and records it in `installed.json`. Extensions without a `main` entry point are rejected with a hint: theme and snippet packs belong in the Themes and Snippets tabs, and web-only extensions are not supported. Other endpoints: `exthost/list`, `exthost/remove {id}`, `exthost/toggle {id, on}`.
- **Host.** `web/exthost-node.js` runs on Node.js 18 or newer: the portable Node from Tools first, then the one on `PATH`. It answers `require('vscode')` with a compatibility layer and loads enabled extensions. It activates them on their activation events (`*`, `onStartupFinished`, `onLanguage:`, `onCommand:`, `workspaceContains:`) and talks to the browser with Content-Length framed JSON-RPC. Its stdout is reserved for that protocol, so extension `console` output goes to the "Extension Host" output channel.
- **Browser.** `web/exthost.js` (`window.AstroExtHost`) starts the host for the open project when at least one extension is enabled. It also:
  - syncs open documents and the selection
  - shows messages, quick picks and input boxes
  - applies edits through the editor, so they can be undone
  - shows status-bar items and output channels
  - merges extension completion, hover, definition and formatting with the language servers

### Supported API

`commands` (register, execute, built-in editor commands) · `window` (information/warning/error messages with buttons, modal messages, `showQuickPick`, `showInputBox`, `createOutputChannel`, `createStatusBarItem`, `withProgress`, `activeTextEditor` with `edit`, `selection(s)`, `insertSnippet`, `showTextDocument`, editor change events) · `workspace` (`workspaceFolders`, `textDocuments`, `openTextDocument`, `getConfiguration` with defaults from `contributes.configuration` and updates, `applyEdit`, `fs`, `findFiles`, document open/change/save/close events, `onDidChangeConfiguration`) · `languages` (completion, hover, definition, document formatting, diagnostics collections, `getLanguages`, `match`) · `env` (`clipboard`, `openExternal`, `appName`, `language`, `machineId`) · `ExtensionContext` (`subscriptions`, `globalState` / `workspaceState`, `secrets`, `extensionPath`, `asAbsolutePath`, storage paths) · `extensions.getExtension` · classes: `Position`, `Range`, `Selection`, `Location`, `Uri`, `TextEdit`, `WorkspaceEdit`, `SnippetString`, `MarkdownString`, `CompletionItem`, `Hover`, `Diagnostic`, `ThemeColor`, `ThemeIcon`, `EventEmitter`, `Disposable`, `CancellationTokenSource` and the usual enums.

Anything else is a harmless stub: it is logged once and listed in the VS Code tab as "APIs used by your extensions that A.S.T.R.O does not provide yet". **Webviews, debuggers, tree views, custom editors, notebooks and extensions that start their own language client may not work.** For language servers, use the Tools tab instead.

### Tested

- `tests/exthost_test.js` (part of `make test`) drives the host with `tests/fixtures/vscode-hello`. It covers activation, commands, configuration, messages with buttons, input box, quick pick, `globalState`, an output channel, a status-bar item, a diagnostics collection, `activeTextEditor.edit`, completion, hover, formatting and stubbing unknown APIs.
- Real extensions checked:
  - **change-case** (`wmaurer.change-case`): edits the selection.
  - **Path Intellisense** (`christian-kohler.path-intellisense`): completes file paths in import strings.
  - **Error Lens** (`usernamehw.errorlens`): installs.
  - **Material Icon Theme** (`PKief.material-icon-theme`): activates without errors.
