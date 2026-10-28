#!/usr/bin/env node
// Checks web/app.js isCodeRequest(): code questions turn on the coding standard, everyday chat does not.
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'web', 'app.js'), 'utf8');
const grab = (name) => { const m = src.match(new RegExp(`  const ${name} = .*`)); if (!m) throw new Error(`${name} not found`); return m[0].trim().replace(/^const /, 'var '); };
eval(grab('CODE_WORDS')); eval(grab('CODE_SIGNS')); eval(grab('ARABIC_FRENCH_CODE'));
const start = src.indexOf('  function isCodeRequest');
eval(src.slice(start, src.indexOf('\n  }\n', start) + 4));
const yes = ['write a python script that renames files by date', 'why does this give TypeError: undefined is not a function', 'fix my server.js',
  'How do I center a div with CSS?', 'اكتب لي كود بايثون لحساب المتوسط', 'écris une fonction qui trie une liste', '```\nint main(){}\n```',
  'make a todo app in React', 'my SQL query returns duplicates', 'implement binary search', 'def add(a, b): return a+b  why None?'];
const no = ['what is the capital of Japan?', 'write a poem about the sea', 'give me a recipe for couscous', 'how far is the moon',
  'translate hello to Arabic', 'tips for a job interview', 'summarize the french revolution', 'what is the weather like in Adrar'];
let bad = 0;
for (const t of yes) if (!isCodeRequest(t)) { bad++; console.log('MISSED code request:', t); }
for (const t of no) if (isCodeRequest(t)) { bad++; console.log('FALSE positive:', t); }
console.log(bad ? `FAIL ${bad}` : `PASS ${yes.length + no.length}/${yes.length + no.length}`);
process.exit(bad ? 1 : 0);
