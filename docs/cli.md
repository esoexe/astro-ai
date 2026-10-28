# Astro AI in the terminal (`astro`) and parallel agents

Version 1.4.0 adds two things:

- **`astro`**, a terminal app for the Astro AI agent: an interactive session, a print mode for scripts, and commands to run and manage several agents.
- **The agent engine**, which runs agents inside the Astro AI server instead of the browser tab. Agents keep working when you close the window. Several can run at the same time, and each one can work in an **isolated copy** of the project that you review, then merge or discard.

The terminal app, the **Parallel** panel on the Agent page and the HTTP API all use the same engine, so a session started in one place shows up in the others.

## Install

| Platform | Where `astro` is |
|---|---|
| Linux `.deb` (Clean) | `/usr/bin/astro` (the personal edition installs `/usr/bin/astro-personal`) |
| Linux portable `.tar.gz` | `astro` inside the folder; link it onto your PATH: `ln -s "$PWD/astro" ~/.local/bin/astro` |
| Windows installer | `astro.exe` in the install folder. Setup adds that folder to your user PATH, so open a **new** terminal after installing. The personal edition's file is `astro-personal.exe`. |
| Windows portable `.zip` | `astro.exe` inside the folder |
| macOS | `Astro AI Clean.app/Contents/Resources/astro`. Run this once: `sudo ln -sf "/Applications/Astro AI Clean.app/Contents/Resources/astro" /usr/local/bin/astro` |
| From source | `make astro` (needs libcurl) |

`astro` needs an Astro AI server. It looks for one in this order:

1. `ASTRO_URL`.
2. The desktop app's server (port 47612 for Clean, 47611 for personal).
3. A local server on port 4173.

If none is running, `astro` starts the server that ships next to it, in the background. It uses the same data folder, keys and settings as the desktop app. `astro server stop` stops that server. It does not stop a server owned by the desktop app.

## Interactive

```text
$ cd my-project
$ astro
◆ Astro AI terminal 1.4.0
  project my-project  ·  mode auto  ·  model auto
› add a /health endpoint and a test for it
● I'll look at the server first.
  ✓ read_file src/server.js
  ✓ edit_file src/server.js +6 −0
  ✓ bash `npm test` exit 0 · 2.1s
✔ Done  · 2 files changed · 6 steps · 14s · …
```

`astro` opens the current folder in place, like the desktop app's **Open folder**. A checkpoint is saved before every run, and `/undo` puts the folder back the way it was. Press Ctrl+C to stop the agent and Ctrl+D to quit.

| Command | What it does |
|---|---|
| `/mode plan\|ask\|auto` | **plan**: the agent can only read and reports back a plan. **ask**: you approve each change and command. **auto**: the agent acts on its own (the default). |
| `/model <id>`, `/models` | Choose a preferred model. If it fails, the agent falls back to the next one in the ranked list. |
| `/review` | Turn on a second-model review at the end of each run (one fix round). |
| `/new`, `/sessions`, `/resume <id>` | Start a new session, list sessions, or continue one. |
| `/agents` | List every agent in this project, including background ones. |
| `/parallel A \| B \| C [--merge]` | Run several agents at once, each in an isolated copy. |
| `/diff`, `/merge`, `/discard`, `/undo` | Review an isolated agent's changes and apply or drop them, or undo a run. |
| `/stop`, `/quit` | Stop the agent, or quit. |

In **ask** mode the agent's request appears inline: `[y] allow  [a] allow all this run  [n] deny`. You can also type what it should do instead.

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
- `--merge` brings every successful agent's changes back when all of them have finished. Without it, review each one first:

```bash
astro agents                 # ids, status, files changed
astro diff a1b2c3d4e5f       # unified diff of one agent's changes
astro merge a1b2c3d4e5f      # apply the changes (a checkpoint is saved first) and remove the copy
astro undo a1b2c3d4e5f       # undo that merge
astro discard a1b2c3d4e5f    # drop the copy instead
```

**Merging is safe:**

- If a file the agent changed was also changed in the project after the agent started, `merge` stops and lists the conflicts.
- `--partial` merges everything except those files.
- `--force` applies the agent's version anyway.
- `--keep` keeps the isolated copy after merging.

Up to 8 agents run at the same time. Set `ASTRO_MAX_AGENTS` on the server to change this. Agents beyond that number wait in a queue.

In the app, the same feature is the **Parallel** chip on the Agent page. Enter one task per line, choose **Isolated copy**, a mode and an optional review, and follow each agent live. From there you can answer its questions, look at the diff, and merge or discard.

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

`astro-ai-vscode-<version>.vsix` comes with the Clean downloads. It works in VS Code, VSCodium, Cursor, Windsurf and similar editors. To install it, run **Extensions → … → Install from VSIX** or `code --install-extension astro-ai-vscode-1.4.0.vsix`.

| Command (Ctrl+Shift+P → "Astro AI") | What it does |
|---|---|
| **Run agent on this folder** (Ctrl+Alt+A) | Starts an agent. It streams its steps to the *Astro AI Agent* output panel and asks for approvals and answers in pop-ups. |
| **Ask the agent about the selection** | The same, with the selected code as context. Also in the editor's right-click menu. |
| **Parallel agents** | Tasks separated by `|`. Each one runs in an isolated copy, and you choose **Show diff**, **Merge** or **Discard** for each when it finishes. |
| **Agents** | Follow, stop, show the diff, merge, discard, undo or send a follow-up to any agent, including ones started from `astro` or the app. |

The extension uses the engine of the desktop app. If the app is not open, it runs `astro server start`. Its settings (`astro.*`) are the server URL, mode, isolation, review, model and the path to `astro`. The source is in `ide/vscode/`.

## Engine HTTP API

Every call is `POST /api/agent/<action>` with a JSON body and the header `X-Astro-Client: 1`. A server with accounts also needs `X-Astro-Token` (the desktop app and `astro` run with accounts off on 127.0.0.1). Use this API to drive Astro AI from your own tools (RPC-style).

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
