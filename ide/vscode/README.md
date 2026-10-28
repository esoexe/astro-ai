# A.S.T.R.O AI Agent for VS Code

Run the [A.S.T.R.O AI](https://esoexe.github.io/astro-ai/) coding agent on the folder you have open. It works in VS Code, VSCodium, Cursor, Windsurf and other editors that run VS Code extensions.

The extension talks to the **A.S.T.R.O AI engine**, the local server that comes with the A.S.T.R.O AI desktop app. If the app is not open, the extension starts the engine with `astro server start`. Agents use the free models you set up in A.S.T.R.O AI. No account or API key is needed for the keyless providers.

## Commands (Ctrl+Shift+P → "A.S.T.R.O AI")

- **Run agent on this folder** (Ctrl+Alt+A): give the agent a job. Its steps stream into the *A.S.T.R.O AI Agent* output panel. A checkpoint is saved first, so **Undo** puts the folder back.
- **Ask the agent about the selection**: the same, with the selected code as context. This command is also in the editor's right-click menu.
- **Parallel agents**: several tasks separated by `|`. Each one runs in its own isolated copy of the folder (a git worktree or a copy). When an agent finishes, you see its diff, then **Merge** or **Discard** it. Merging stops when a file was also changed in your folder after the agent started.
- **Agents**: follow, stop, show the diff, merge, discard, undo or send a follow-up to any agent. This includes agents started from the terminal app (`astro`) or the desktop app.

The **mode** setting controls permissions:

- **Plan** is read-only.
- **Ask** asks you to approve each change and command.
- **Auto** acts on its own (the default).

Free models have rate and daily limits. If a provider is busy, the engine moves on to the next model.

Licence: MIT OR Apache-2.0 OR GPL-3.0.
