#!/usr/bin/env bash
# Creates the demo project the website's terminal demo recordings use: a small Express todo API with
# project rules (AGENTS.md), a Claude-style skill and a project MCP server. Usage: mkdemo.sh [dir]
set -eu
D="${1:-/tmp/adt/projects/todo-api}"
rm -rf "$D"; mkdir -p "$D/src/routes" "$D/test" "$D/.claude/skills/api-tests" "$D/tools"
cd "$D"
cat > package.json <<'J'
{
  "name": "todo-api",
  "version": "0.4.0",
  "private": true,
  "type": "module",
  "scripts": { "dev": "node src/server.js", "test": "node --test" },
  "dependencies": { "express": "^4.21.2" }
}
J
cat > src/server.js <<'J'
import express from 'express';
import { todos } from './routes/todos.js';

const app = express();
app.use(express.json());
app.use('/todos', todos);

export default app;
if (process.argv[1] === new URL(import.meta.url).pathname) app.listen(3000, () => console.log('API on :3000'));
J
cat > src/routes/todos.js <<'J'
import { Router } from 'express';

export const todos = Router();
const items = new Map();
let nextId = 1;

todos.get('/', (req, res) => res.json([...items.values()]));

todos.post('/', (req, res) => {
  const title = String(req.body?.title || '').trim();
  if (!title) return res.status(400).json({ error: 'title is required' });
  const todo = { id: nextId++, title, done: false };
  items.set(todo.id, todo);
  res.status(201).json(todo);
});

todos.delete('/:id', (req, res) => {
  if (!items.delete(Number(req.params.id))) return res.status(404).json({ error: 'not found' });
  res.status(204).end();
});
J
cat > test/todos.test.js <<'J'
import { test } from 'node:test';
import assert from 'node:assert/strict';
import app from '../src/server.js';

test('create and list todos', async () => {
  const srv = app.listen(0);
  const base = `http://127.0.0.1:${srv.address().port}`;
  const made = await fetch(`${base}/todos`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ title: 'Ship it' }) });
  assert.equal(made.status, 201);
  const list = await (await fetch(`${base}/todos`)).json();
  assert.equal(list.length, 1);
  srv.close();
});
J
cat > AGENTS.md <<'J'
# todo-api

- ES modules, Express 4, Node 20+. No new dependencies without asking.
- Tests use `node --test` in `test/`. Run `npm test` after every change.
- Keep handlers small; put shared helpers in `src/`.
J
cat > .claude/skills/api-tests/SKILL.md <<'J'
---
name: api-tests
description: Write HTTP tests for this API with node:test and fetch against app.listen(0).
---
Start the app with `app.listen(0)`, call it with `fetch`, assert with `node:assert/strict`, and close the server at the end.
J
cat > tools/notes-mcp.js <<'J'
#!/usr/bin/env node
// A tiny MCP server (stdio, JSON-RPC) with two tools, for the demo project.
import { createInterface } from 'node:readline';
const notes = ['Rate limits: 100 requests per minute per IP', 'Deploy target: Node 20 on Fly.io'];
const tools = [
  { name: 'list_notes', description: 'List the project notes', inputSchema: { type: 'object', properties: {} } },
  { name: 'add_note', description: 'Add a project note', inputSchema: { type: 'object', properties: { text: { type: 'string' } }, required: ['text'] } },
];
const send = (m) => process.stdout.write(JSON.stringify(m) + '\n');
createInterface({ input: process.stdin }).on('line', (l) => {
  let m; try { m = JSON.parse(l); } catch { return; }
  if (m.id === undefined) return;
  if (m.method === 'initialize') return send({ jsonrpc: '2.0', id: m.id, result: { protocolVersion: '2024-11-05', capabilities: { tools: {} }, serverInfo: { name: 'project-notes', version: '1.0.0' } } });
  if (m.method === 'tools/list') return send({ jsonrpc: '2.0', id: m.id, result: { tools } });
  if (m.method === 'tools/call') {
    if (m.params?.name === 'add_note') notes.push(String(m.params.arguments?.text || ''));
    return send({ jsonrpc: '2.0', id: m.id, result: { content: [{ type: 'text', text: notes.join('\n') }] } });
  }
  send({ jsonrpc: '2.0', id: m.id, error: { code: -32601, message: 'unknown method' } });
});
J
cat > .mcp.json <<'J'
{ "mcpServers": { "project-notes": { "command": "node", "args": ["tools/notes-mcp.js"] } } }
J
printf 'node_modules/\n' > .gitignore
npm install --silent --no-audit --no-fund >/dev/null 2>&1
git init -q && git add -A && git -c user.email=demo@astro -c user.name=demo commit -qm "todo-api" && echo "demo ready: $D"
npm test 2>&1 | grep -E "^# (pass|fail)"
