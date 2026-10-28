// SDK test against a running engine with the mock model. Usage: ASTRO_URL=... node tests/sdk_test.mjs <folder>
import fs from 'node:fs';
import path from 'node:path';
import { Astro, AstroError } from '../sdk/js/index.mjs';
const folder = process.argv[2]; fs.mkdirSync(folder, { recursive: true });
let pass = 0, fail = 0;
const ok = (n, c, x = '') => { if (c) { pass++; console.log('  ok  ', n); } else { fail++; console.log('  FAIL', n, x); } };
const astro = new Astro();
const project = await astro.openFolder(folder);
ok('openFolder', typeof project === 'string' && project.length > 0);
const run = await astro.start({ project, prompt: 'create file sdk.txt containing hi sdk' });
const seen = [];
for await (const e of run.events()) seen.push(e.type);
ok('events stream to the end', seen.includes('tool_done') && seen.includes('done'), seen.join(','));
ok('file written', fs.readFileSync(path.join(folder, 'sdk.txt'), 'utf8').trim() === 'hi sdk');
const q = await astro.start({ project, prompt: 'ask me a colour' });
const r = await q.wait({ onPrompt: async () => 'green' });
ok('onPrompt answers questions', r.status === 'done' && /green/.test(r.text + JSON.stringify(await q.get())), JSON.stringify(r).slice(0, 200));
const res = await astro.parallel(project, ['create file s1.txt containing 1', 'create file s2.txt containing 2']);
ok('parallel finishes', res.length === 2 && res.every((x) => x.result.status === 'done'));
ok('isolated until merged', !fs.existsSync(path.join(folder, 's1.txt')));
ok('diff', /\+1/.test(await res[0].run.diff()));
for (const { run: p } of res) await p.merge();
ok('merged', fs.existsSync(path.join(folder, 's1.txt')) && fs.existsSync(path.join(folder, 's2.txt')));
await res[0].run.undo();
ok('undo merge', !fs.existsSync(path.join(folder, 's1.txt')));
try { await astro.session('nope').get(); ok('errors are AstroError', false); } catch (e) { ok('errors are AstroError', e instanceof AstroError && e.status === 404); }
console.log(`sdk: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
