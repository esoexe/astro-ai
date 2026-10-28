# Astro AI SDK (JavaScript)

This is a zero-dependency client for the Astro AI agent engine, the same API that the `astro` terminal app and the VS Code extension use. It works in Node 18+, Deno, Bun and browsers on the same origin.

```js
import { Astro } from './index.mjs';            // copy the file, or depend on this folder

const astro = new Astro();                       // ASTRO_URL, or the desktop app's engine on 127.0.0.1
const project = await astro.openFolder('/home/me/app');

// one agent, streaming
const run = await astro.start({ project, prompt: 'add a /health endpoint and a test', mode: 'auto' });
for await (const e of run.events({ onPrompt: async (q) => 'Allow' })) {
  if (e.type === 'tool_done') console.log(e.ok ? '✓' : '✗', e.label);
}

// parallel agents in isolated copies, then merge the ones that passed
const results = await astro.parallel(project, ['add dark mode', 'write tests for utils/']);
for (const { run, result } of results) if (result.status === 'done') await run.merge();
```

| Call | What it does |
|---|---|
| `astro.start({project, prompt, mode?, model?, review?, isolate?, interactive?, max_steps?})` | Starts an agent and returns a `Run` |
| `run.events({since?, onPrompt?, signal?})` | Async iterator over events until the session stops. `onPrompt` answers approvals and questions |
| `run.wait()` | Waits for the end and returns `{status, text, files, steps, ms, model, review, error}` |
| `run.send(text)` | Follow-up in the same session |
| `run.answer(text, qid?)` | Replies to an approval or question |
| `run.stop()`, `run.setMode(mode)` | Stops the agent, or changes its mode |
| `run.diff()`, `run.changes()` | Shows what an isolated agent changed |
| `run.merge({force?, partial?, keep?})`, `run.discard()` | Applies an isolated agent's changes, or drops them |
| `run.undo()` | Reverts a run, or a merge |
| `astro.parallel(project, prompts, opts?)` | One isolated agent per prompt. Resolves with `[{run, result}]` |
| `astro.list()`, `astro.models()`, `astro.openFolder(path)` | Lists sessions, lists ranked models, or opens a folder as a project |

Errors are thrown as `AstroError`, with `status` and `data` from the server. For example, a merge conflict has `data.conflicts`. Event types and fields are listed in [docs/cli.md](../../docs/cli.md#engine-http-api).
