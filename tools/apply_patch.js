#!/usr/bin/env node
// Applies FIND/REPLACE blocks to a file. Usage: node tools/apply_patch.js <target> <patchfile>
// Patch format:
//   @@@ FIND
//   exact text
//   @@@ REPLACE
//   new text
//   @@@ END
// Every FIND must occur exactly once in the target, otherwise nothing is written.
'use strict';
const fs = require('fs');
const [target, patchFile] = process.argv.slice(2);
if (!target || !patchFile) { console.error('usage: apply_patch.js <target> <patch>'); process.exit(2); }
let text = fs.readFileSync(target, 'utf8');
const patch = fs.readFileSync(patchFile, 'utf8');
const re = /^@@@ FIND\n([\s\S]*?)\n@@@ REPLACE\n([\s\S]*?)\n?@@@ END$/gm;
let m; let n = 0;
while ((m = re.exec(patch))) {
  const [_, find, repl] = m;
  const first = text.indexOf(find);
  if (first < 0) { console.error(`FIND #${n + 1} not found:\n${find.slice(0, 200)}`); process.exit(1); }
  if (text.indexOf(find, first + 1) >= 0) { console.error(`FIND #${n + 1} is not unique:\n${find.slice(0, 200)}`); process.exit(1); }
  text = text.slice(0, first) + repl + text.slice(first + find.length);
  n++;
}
if (!n) { console.error('no blocks found'); process.exit(1); }
fs.writeFileSync(target, text);
console.log(`${target}: applied ${n} block(s)`);
