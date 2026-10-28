// Drives ide/vscode/extension.js with a fake `vscode` module against a running engine (ASTRO_URL).
// Usage: ASTRO_URL=http://127.0.0.1:4195 node tests/vscode_ext_test.js <empty folder>
'use strict';
const Module = require('module');
const fs = require('fs');
const path = require('path');
const folder = process.argv[2];
fs.mkdirSync(folder, { recursive: true });
let pass = 0, fail = 0;
const ok = (n, c, x = '') => { if (c) { pass++; console.log('  ok  ', n); } else { fail++; console.log('  FAIL', n, x); } };

const commands = {}; const outLines = []; const infos = []; const inputs = []; const picks = [];
const fake = {
  StatusBarAlignment: { Left: 1 },
  Uri: { parse: (s) => s, file: (p) => ({ fsPath: p }) },
  env: { openExternal: () => {} },
  commands: { registerCommand: (id, fn) => { commands[id] = fn; return { dispose() {} }; }, executeCommand: () => {} },
  workspace: {
    workspaceFolders: [{ uri: { fsPath: folder } }],
    getWorkspaceFolder: () => null,
    getConfiguration: () => ({ get: (k) => ({ mode: 'auto', serverUrl: process.env.ASTRO_URL })[k] }),
    onDidChangeConfiguration: () => ({ dispose() {} }),
    saveAll: async () => true,
    asRelativePath: (u) => String(u),
    openTextDocument: async (o) => { fake.__lastDoc = o; return o; },
  },
  window: {
    activeTextEditor: undefined,
    createOutputChannel: () => ({ appendLine: (l) => outLines.push(l), show() {}, dispose() {} }),
    createStatusBarItem: () => ({ show() {}, dispose() {} }),
    showInputBox: async () => inputs.shift(),
    showQuickPick: async (items) => { const p = picks.shift(); return typeof p === 'function' ? p(items) : p; },
    showInformationMessage: async (msg, ...btns) => { infos.push(msg); return btns.includes(fake.__infoAnswer) ? fake.__infoAnswer : undefined; },
    showWarningMessage: async () => undefined,
    showErrorMessage: async (m) => { infos.push(`ERROR ${m}`); },
    showTextDocument: async () => {},
    createTerminal: () => ({ show() {}, sendText() {} }),
  },
};
const orig = Module._resolveFilename;
Module._resolveFilename = function (req, ...rest) { return req === 'vscode' ? 'vscode' : orig.call(this, req, ...rest); };
require.cache.vscode = { id: 'vscode', filename: 'vscode', loaded: true, exports: fake };
const ext = require(path.join(__dirname, '..', 'ide', 'vscode', 'extension.js'));
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(fn, ms = 15000) { const t = Date.now(); while (Date.now() - t < ms) { if (await fn()) return true; await wait(150); } return false; }

(async () => {
  ext.activate({ subscriptions: [] });
  ok('commands registered', ['astro.run', 'astro.parallel', 'astro.agents', 'astro.stop'].every((c) => commands[c]));
  inputs.push('create file vs.txt containing from vscode');
  await commands['astro.run']();
  ok('agent wrote the file', await until(() => fs.existsSync(path.join(folder, 'vs.txt')) && fs.readFileSync(path.join(folder, 'vs.txt'), 'utf8').trim() === 'from vscode'));
  ok('steps streamed to the output', await until(() => outLines.some((l) => /✔ Done/.test(l))), outLines.slice(-5).join(' | '));
  ok('done notification', await until(() => infos.some((m) => /done \(1 file/.test(m))), infos.join(' | '));

  fake.__infoAnswer = 'Merge';
  inputs.push('create file pa.txt containing A | create file pb.txt containing B');
  await commands['astro.parallel']();
  ok('parallel agents merged on request', await until(() => fs.existsSync(path.join(folder, 'pa.txt')) && fs.existsSync(path.join(folder, 'pb.txt')), 20000), infos.join(' | '));
  fake.__infoAnswer = undefined;
  await until(() => infos.filter((m) => /^Merged/.test(m)).length >= 2, 10000);
  await wait(500);

  // Agents list -> pick the first merged one -> Undo
  picks.push((items) => items.find((i) => /pa\.txt/.test(i.label)), 'Undo');
  await commands['astro.agents']();
  ok('undo from the agents list', await until(() => !fs.existsSync(path.join(folder, 'pa.txt'))), infos.slice(-3).join(' | '));
  ext.deactivate();
  console.log(`vscode extension: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
