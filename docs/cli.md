# A.S.T.R.O AI in the terminal (`astro`) and parallel agents

Version 1.4.0 added two things, and 1.5.0 turned the terminal app into a full-screen app:

- **`astro`**, a terminal app for the A.S.T.R.O AI agent: an interactive session, a print mode for scripts, and commands to run and manage several agents.
- **The agent engine**, which runs agents inside the A.S.T.R.O AI server instead of the browser tab. Agents keep working when you close the window. Several can run at the same time, and each one can work in an **isolated copy** of the project that you review, then merge or discard.

The terminal app, the **Parallel** panel on the Agent page and the HTTP API all use the same engine, so a session started in one place shows up in the others.

## Install

### Terminal only (no setup wizard, no admin rights)

```sh
# Linux (x64) and macOS
curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh
```

```powershell
# Windows 10/11, in PowerShell
irm https://esoexe.github.io/astro-ai/install.ps1 | iex
```

The script downloads the Clean portable build (macOS: the disk image) from the latest GitHub release and checks it against the release's `SHA256SUMS.txt`. It then installs `astro` and its engine into `~/.astro-ai` and links `~/.local/bin/astro`. On Windows the files go into `%LOCALAPPDATA%\Programs\AstroAI-CLI`, which is added to your user PATH. Run it again to update.

Options: `ASTRO_VERSION=1.6.1` installs a specific version, `ASTRO_HOME` changes the install folder and `ASTRO_BIN` changes the link folder (Linux/macOS). Uninstall with `curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh -s -- --uninstall`, or on Windows set `$env:ASTRO_UNINSTALL="1"` and run the PowerShell line again. Uninstalling keeps your chats, settings and keys.

It works right away with the free models that need no key. To add more providers, open `http://127.0.0.1:47612` in a browser while the engine runs and go to Settings → Providers.

### With the desktop app

| Platform | Where `astro` is |
|---|---|
| Linux `.deb` (Clean) | `/usr/bin/astro` (the personal edition installs `/usr/bin/astro-personal`) |
| Linux portable `.tar.gz` | `astro` inside the folder; link it onto your PATH: `ln -s "$PWD/astro" ~/.local/bin/astro` |
| Windows installer | `astro.exe` in the install folder. Setup adds that folder to your user PATH, so open a **new** terminal after installing. The personal edition's file is `astro-personal.exe`. |
| Windows portable `.zip` | `astro.exe` inside the folder |
| macOS | `A.S.T.R.O AI Clean.app/Contents/Resources/astro`. Run this once: `sudo ln -sf "/Applications/A.S.T.R.O AI Clean.app/Contents/Resources/astro" /usr/local/bin/astro` |
| From source | `make astro` (needs libcurl) |

`astro` needs an A.S.T.R.O AI server. It looks for one in this order:

1. `ASTRO_URL`.
2. The desktop app's server (port 47612 for Clean, 47611 for personal).
3. A local server on port 4173.

If none is running, `astro` starts the server that ships next to it, in the background. It uses the same data folder, keys and settings as the desktop app. `astro server stop` stops that server. It does not stop a server owned by the desktop app.

## The full-screen app (1.5)

```text
$ cd my-project
$ astro
```

Since version 1.5, `astro` opens a full-screen app in your terminal, in the style of other terminal coding agents. The conversation fills the screen and the prompt stays at the bottom. Every edit appears as a coloured diff, and the status line shows the mode, the model and what the agent is doing. `astro` opens the current folder in place, like the desktop app's **Open folder**. A checkpoint is saved before every run, and `/undo` puts the folder back the way it was.

- **Modes.** Shift+Tab cycles **auto** (the agent acts on its own; the default), **plan** (read-only, it reports back a plan) and **ask** (you approve each change and command).
- **Approvals.** In ask mode a dialog shows the exact diff or command. Press `y` to allow, `a` to allow everything for the rest of the run, or `n` (or Esc) to tell the agent what to do instead.
- **Questions** from the agent appear as a menu. Pick an option or type your own answer.
- **While it works** you can type a follow-up and press Enter. Esc stops the agent.
- **Side panel.** It shows the project, the session, the mode and model, changed files, the to-do list and parallel agents. It opens by default in windows at least 140 columns wide, and Ctrl+B toggles it in windows from 96 columns.
- **Prompt.** `/` opens the command menu (with your project commands and skills), `@path` mentions a file (Tab completes it) and `!command` runs a shell command in the folder. Alt+Enter or Ctrl+J adds a new line, and bracketed paste keeps pasted lines together. ↑/↓ is history, PgUp/PgDn scrolls, and Ctrl+A/E/U/K/W edit the line.
- **Overlays.** Ctrl+O picks a model (Auto = best first with automatic fallback). Ctrl+G manages agents: Enter opens one, `d` shows the diff, `m` merges, `x` discards, `s` stops. Ctrl+R resumes a session and `?` lists every shortcut.
- **Sessions.** `astro -c` continues the last session in this folder and `astro --resume <id>` (`-r`) opens a given one. When you quit (Ctrl+D, or Ctrl+C twice), the app prints the command that resumes it.
- **Colours.** True colour when the terminal supports it (`COLORTERM=truecolor`), otherwise 256 colours. It follows a light or dark terminal; force one with `ASTRO_THEME=light|dark`. `NO_COLOR=1` turns colour off. `/mouse` turns on mouse-wheel scrolling (it is off by default so you can select text).
- **Themes (1.6).** `/theme` opens a picker with the same themes as the app (A.S.T.R.O Peach, Teal, Tokyo Night, Catppuccin, Dracula, Nord, One Dark, Gruvbox, Rosé Pine, GitHub, Solarized, Midnight and light versions). Arrow keys preview, Enter keeps, Esc cancels. `/theme <name>` sets one directly. The choice is saved in `~/.config/astro-ai/tui.conf` (Windows: `%APPDATA%\astro-ai\tui.conf`); `ASTRO_THEME=<name>` overrides it. Themes change the text colours; the terminal's own background stays.
- **Live logo (1.6).** The welcome card draws the orbit logo live in true colour, with satellites moving along the rings. `/logo off` (or `ASTRO_NO_ANIM=1`) keeps it still.
- **HD logo (1.6.1).** In terminals that show Sixel pictures (Windows Terminal 1.22+, WezTerm, iTerm2, foot, Konsole and others) the logo is drawn as a sharp high-resolution picture at up to 25 fps. A.S.T.R.O detects support at start-up; `/logo hd` or `/logo text` chooses for you, `/logo auto` restores detection, `/logo info` shows what was found. `ASTRO_LOGO=hd|text` does the same from the environment.

| Command | What it does |
|---|---|
| `/mode plan\|ask\|auto` | Set the mode (same as Shift+Tab). |
| `/model [id]`, `/models` | Choose a preferred model, or list the ranked coding models. If a model fails, the agent falls back to the next one. |
| `/review on\|off` | A second model reviews the work at the end of each run (one fix round). |
| `/new`, `/clear`, `/sessions`, `/resume [id]` | Start a new session (`/clear` also clears the screen), browse sessions, or continue one. |
| `/diff`, `/undo` | Show what this session changed, or put the folder back. |
| `/parallel "A" "B" [--merge]` | Run several agents at once, each in an isolated copy (`A \| B` also works). |
| `/agents`, `/merge <id>`, `/discard <id>` | Manage isolated agents and apply or drop their changes. |
| `/init` | Write `AGENTS.md`, a guide to this project for agents. |
| `/export [file]`, `/copy` | Save the conversation as Markdown, or copy the last answer. |
| `/theme [name]`, `/logo on\|off\|hd\|text\|auto\|info` | Colour theme with live preview; animated or still logo; HD picture or text logo. |
| `/panel`, `/mouse`, `/status`, `/help` | Side panel, mouse scrolling, engine and session details, help. |
| `/stop`, `/quit` | Stop the agent (Esc), or quit (Ctrl+D). |

### Classic prompt

`astro --classic` (or `ASTRO_TUI=0`) keeps the line-by-line prompt from 1.4, which prints into your scrollback. It is also used automatically when stdin or stdout is not a terminal, or when `TERM=dumb`. It has the same commands. In ask mode the request appears inline as `[y] allow  [a] allow all this run  [n] deny`.

## Print mode (scripts and CI)

```bash
astro -p "fix the failing test in tests/date.test.js"          # progress on stderr, report on stdout
astro -p --json "summarise TODOs" > events.jsonl               # one JSON event per line
astro -p --plan "how would you add OAuth?"                     # read-only
astro -p --isolate "try upgrading to React 19" && astro diff <id>
```

Exit codes: `0` done, `1` error, `2` stopped, `3` paused at the step limit (`astro attach <id>`, then continue).

In ask mode with no one to answer (print mode without a terminal), the agent does not wait forever. Each change it would need approval for is blocked, and it explains in its report what it would have done.

## Parallel agents

```bash
astro parallel "add dark mode" "write tests for utils/" "update the README" --merge
```

- Each agent works in its **own isolated copy**:
  - In a git repository, that copy is a **git worktree** checked out at your current commit, with your uncommitted and untracked changes copied in. It is detached, so no branch is created.
  - Otherwise it is a plain copy of the folder, without `.git` and build output.
  - In both cases `node_modules` is shared through a link rather than copied.
- Your project is not touched while the agents work.
- `--merge` brings every successful agent's changes back when all of them have finished. An agent whose last test or build command still failed is skipped (its `done` event carries `"failing"`). Without it, review each one first:

```bash
astro agents                 # ids, status, files changed
astro diff a1b2c3d4e5f       # unified diff of one agent's changes
astro merge a1b2c3d4e5f      # apply the changes (a checkpoint is saved first) and remove the copy
astro undo a1b2c3d4e5f       # undo that merge
astro discard a1b2c3d4e5f    # drop the copy instead
```

**Merging is safe:**

- If a file the agent changed was also changed in the project after the agent started, `merge` runs a 3-way merge (`git merge-file`) against the file as it was when the agent started. Changes in different places are combined (`"combined": true` in the API result). If they overlap, `merge` stops and lists the conflicts.
- `--partial` merges everything except those files.
- `--force` applies the agent's version anyway.
- `--keep` keeps the isolated copy after merging.

Up to 8 agents run at the same time. Set `ASTRO_MAX_AGENTS` on the server to change this. Agents beyond that number wait in a queue.

In the app, the same feature is the **Parallel** chip on the Agent page. Enter one task per line, choose **Isolated copy**, a mode and an optional review, and follow each agent live. From there you can answer its questions, look at the diff, and merge or discard.

## Language tools and VS Code extensions (1.6)

```text
$ astro tools list                       # compilers, runtimes and language servers: installed, found, available
$ astro tools install clangd cmake ninja # portable installs, SHA-256 checked, no admin rights
$ astro tools info cpp-compiler          # where to get a compiler on this OS
$ astro tools remove ninja

$ astro ext install wmaurer.change-case  # a VS Code extension from Open VSX (preview)
$ astro ext list
$ astro ext disable wmaurer.change-case
$ astro ext remove wmaurer.change-case
```

Both talk to the engine, so the same installs show up in the app's Extensions › Tools and Extensions › VS Code tabs. Language servers and extensions run in the app's editor. See [extensions.md](extensions.md) for what's supported and how it works.

## Project tools (`.astro/tools/*.json`)

A project can give the agent its own tools. Each JSON file in `.astro/tools/` defines one tool:

```json
{
  "name": "run_tests",
  "description": "Run the unit tests, or one test file",
  "command": "npm test -- {{file}}",
  "args": { "file": "test file to run (optional)" },
  "readonly": false,
  "timeout": 300
}
```

- The agent sees these tools in its job and calls them like built-in tools, for example `{"tool":"run_tests","file":"src/date.test.js"}`. The command runs in the project folder.
- Each `{{arg}}` is replaced with the shell-quoted value, so an argument can never add a second command. A missing argument becomes nothing.
- Names use `a-z`, `0-9` and `_`, and cannot reuse a built-in tool name. Up to 30 tools per project.
- **Permissions:**
  - A tool marked `"readonly": true` runs in every mode.
  - Any other tool counts as a change. **Plan** mode blocks it, and **Ask** mode asks you first, showing the exact command.
- Project tools work in the engine: the terminal app, the **Parallel** panel, the VS Code extension and the HTTP API.

## VS Code extension

`astro-ai-vscode-<version>.vsix` comes with the Clean downloads. It works in VS Code, VSCodium, Cursor, Windsurf and similar editors. To install it, run **Extensions → … → Install from VSIX** or `code --install-extension astro-ai-vscode-1.5.0.vsix`.

| Command (Ctrl+Shift+P → "A.S.T.R.O AI") | What it does |
|---|---|
| **Run agent on this folder** (Ctrl+Alt+A) | Starts an agent. It streams its steps to the *A.S.T.R.O AI Agent* output panel and asks for approvals and answers in pop-ups. |
| **Ask the agent about the selection** | The same, with the selected code as context. Also in the editor's right-click menu. |
| **Parallel agents** | Tasks separated by `|`. Each one runs in an isolated copy, and you choose **Show diff**, **Merge** or **Discard** for each when it finishes. |
| **Agents** | Follow, stop, show the diff, merge, discard, undo or send a follow-up to any agent, including ones started from `astro` or the app. |

The extension uses the engine of the desktop app. If the app is not open, it runs `astro server start`. Its settings (`astro.*`) are the server URL, mode, isolation, review, model and the path to `astro`. The source is in `ide/vscode/`.

## Engine HTTP API

Every call is `POST /api/agent/<action>` with a JSON body and the header `X-Astro-Client: 1`. A server with accounts also needs `X-Astro-Token` (the desktop app and `astro` run with accounts off on 127.0.0.1). Use this API to drive A.S.T.R.O AI from your own tools (RPC-style).

| Action | Body | Returns |
|---|---|---|
| `start` | `{project, prompt, mode?, model?, review?, interactive?, max_steps?, isolate?}` | `{id, project, isolated, git}` |
| `send` | `{id, prompt}` (a follow-up in the same session) | `{ok}` |
| `events` | `{id, since, wait?, live?}`: long poll for up to `wait` seconds | `{status, running, waiting, waiting_kind, live, seq, steps, events[]}` |
| `get` | `{id, since?}` | the session with its events |
| `list` | `{project?, all?}` | `{sessions, running, max}` |
| `answer` | `{id, answer, qid?}`: approvals take `Allow`, `Allow all in this run`, `Deny` or free text | `{ok}` |
| `stop`, `mode` | `{id}`, `{id, mode}` | `{ok}` |
| `changes`, `diff` | `{id}` | the changed files, or a unified diff |
| `merge` | `{id, discard?, force?, partial?}` | `{merged, conflicts, skipped, checkpoint, discarded}` |
| `discard`, `undo`, `delete` | `{id}` | `{ok}` |
| `models` | `{}` | `{ranked, all}` |

To work on a folder outside the server's projects folder, open it first with `POST /api/dev/fs/open {path}`. The response gives the project name to use.

**Event types:**

- `user`, `say` (agent text), `tool`, `tool_done` (`ok`, `blocked`, `label`, `summary`, `out`, `code`, `added`, `removed`, `ms`).
- `approval` and `question` (the agent is waiting; reply with `answer`), then `answered`.
- `note`, `todo`, `review` (`verdict`, `text`), `done` (`text`, `files`, `steps`, `ms`, `model`), `error`, `status`.

**Session statuses:** `queued`, `running`, `waiting`, `paused`, `done`, `stopped`, `error`, `interrupted`.

Sessions are saved in `<data>/.astro_engine/runs/`. If the server restarts, a session that was running is marked `interrupted`. Send a follow-up to continue it.

## Limits

- Free models have rate and daily limits. Running many agents at once uses up a provider's quota faster. When one provider is busy, the engine moves on to the next model in the ranked list.
- Stop ends the agent at once. A shell command that was already running finishes on its own in the background.
- Isolated copies use disk space until you merge or discard them.
