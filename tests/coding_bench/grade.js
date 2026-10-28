// Shared grader for the coding benchmarks: pulls the code out of a model answer and runs the hidden tests.
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const FENCE = /```([\w+#-]*)[^\n]*\n([\s\S]*?)```/g;
const LANGS = { js: /^(js|javascript|node|mjs|jsx|ts)?$/i, py: /^(py|python|python3)?$/i, cpp: /^(c\+\+|cpp|cxx|cc|c)?$/i };
function extract(text, task) {
  const blocks = [...text.matchAll(FENCE)].map((m) => ({ lang: m[1], code: m[2] })).filter((b) => LANGS[task.lang].test(b.lang));
  const withFn = blocks.filter((b) => b.code.includes(task.fn));
  if (!withFn.length) return '';
  // The defining block(s): those that declare the function/class, joined in order (multi-file answers).
  const decl = task.lang === 'py' ? new RegExp(`def\\s+${task.fn}\\b|class\\s+${task.fn}\\b`) : new RegExp(`(function\\s+${task.fn}\\b|class\\s+${task.fn}\\b|${task.fn}\\s*=|\\b${task.fn}\\s*\\()`);
  const defs = withFn.filter((b) => decl.test(b.code));
  return (defs.length ? defs : withFn).map((b) => b.code).join('\n');
}
function strip(code, task) {
  if (task.lang === 'js') {
    return code.replace(/^\s*export\s+default\s+/gm, '').replace(/^\s*export\s+(?=(async\s+)?(function|class|const|let|var)\b)/gm, '')
      .replace(/^\s*export\s*\{[^}]*\};?/gm, '').replace(/^\s*module\.exports\s*=.*$/gm, '').replace(/^\s*import\s.+from\s.+$/gm, '');
  }
  if (task.lang === 'py') return code.replace(/^if\s+__name__\s*==\s*['"]__main__['"]\s*:[\s\S]*$/m, '');
  // Multi-file answers are joined into one unit, so local headers are already inlined.
  return code.replace(/^\s*#include\s+"[^"]+".*$/gm, '').replace(/^\s*#pragma once\s*$/gm, '').replace(/^\s*int\s+main\s*\([^)]*\)\s*\{[\s\S]*$/m, (m) => cutMain(m));
}
function cutMain(src) { // drop the model's own main() so the hidden test's main() can link
  let depth = 0; let started = false;
  for (let i = 0; i < src.length; i++) {
    if (src[i] === '{') { depth++; started = true; } else if (src[i] === '}') { depth--; if (started && depth === 0) return src.slice(i + 1); }
  }
  return '';
}
function grade(task, text) {
  const code = extract(text, task);
  if (!code) return { pass: false, why: 'no code block' };
  if (/\.\.\.\s*$|\/\/ ?\.\.\.|# ?\.\.\.|TODO|your code here/im.test(code)) { /* graded anyway: placeholders usually fail tests */ }
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ab-'));
  try {
    const body = strip(code, task);
    let r;
    if (task.lang === 'js') {
      const f = path.join(dir, 't.js'); fs.writeFileSync(f, `${body}\n;(()=>{${task.test}\n})();console.log('ok');`);
      r = spawnSync('node', [f], { encoding: 'utf8', timeout: 15000 });
    } else if (task.lang === 'py') {
      const f = path.join(dir, 't.py'); fs.writeFileSync(f, `${body}\n${task.test}\nprint('ok')\n`);
      r = spawnSync('python3', [f], { encoding: 'utf8', timeout: 15000, input: '' });
    } else {
      const f = path.join(dir, 't.cpp'); fs.writeFileSync(f, `#include <vector>\n#include <string>\n#include <tuple>\n${body}\n${task.test}\n`);
      const c = spawnSync('g++', ['-std=c++17', '-O1', '-o', path.join(dir, 'a'), f], { encoding: 'utf8', timeout: 60000 });
      if (c.status !== 0) return { pass: false, why: 'compile: ' + (c.stderr || '').split('\n').find((l) => /error/.test(l))?.slice(0, 160) };
      r = spawnSync(path.join(dir, 'a'), [], { encoding: 'utf8', timeout: 15000 });
    }
    const out = (r.stdout || '').trim();
    if (r.status === 0 && /(^|\n)ok$/.test(out)) return { pass: true, why: '' };
    const err = `${r.stderr || ''}\n${out}`.split('\n').map((l) => l.trim()).filter(Boolean);
    return { pass: false, why: (r.error?.code === 'ETIMEDOUT' ? 'timeout' : err.reverse().find((l) => /Error|error|assert|SystemExit|no /.test(l)) || err[0] || `exit ${r.status}`).slice(0, 160) };
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}

module.exports = { grade, extract };
