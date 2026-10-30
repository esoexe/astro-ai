// Unit tests for web/agent_audit.js — the agent's finish audit (honest the first time, whole job, no stubs).
// The C++ port (astro_engine.inc finish_audit) is covered end to end in tests/engine_e2e.sh.
const assert = require('assert');
const A = require('../web/agent_audit.js');
let n = 0; const t = (name, fn) => { fn(); n++; };

t('status questions are detected', () => {
  for (const j of ['is the app complete?', 'Is it done', 'how complete is the project?', 'be honest, is it finished', 'what is missing?', 'audit the repo', 'does everything work?'])
    assert.ok(A.isAssessJob(j), j);
});
t('build jobs are not status questions', () => {
  for (const j of ['Fix the bug where login does not work', 'build a todo app', 'make the tests pass', 'add dark mode and make sure it works'])
    assert.ok(!A.isAssessJob(j), j);
});
t('audit-and-fix still gets the lazy rule', () => { assert.ok(A.isAssessJob('audit the code and fix the bugs')); assert.ok(!A.questionOnly('audit the code and fix the bugs')); });
t('absolute claims are found', () => {
  for (const r of ['Yes. The app is 100% complete.', 'Everything is done and working.', 'It is production-ready.', 'All features are implemented.', 'The project is fully complete.'])
    assert.ok(A.absoluteClaim(r), r);
});
t('hedged or negated statements are not claims', () => {
  for (const r of ['It is not 100% complete yet.', "It isn't production-ready: the auth flow is missing.", 'Almost fully complete, but the export is missing.', 'It will be production-ready once tests exist.', 'Coverage: 100 tests ran.'])
    assert.strictEqual(A.absoluteClaim(r), '', r);
});
t('lazy endings are found', () => {
  for (const r of ['Built the page. Would you like me to add the API next?', 'Done with part 1. Let me know if you want me to continue.', 'I can implement the rest later if you want.', 'For brevity, I only stubbed the export.', 'Shall I also write the tests?'])
    assert.ok(A.lazyEnding(r), r);
  assert.ok(A.lazyEnding('Done.', { status: 'partial', remaining: ['the export page'] }));
});
t('blocked leftovers are not laziness', () => {
  assert.strictEqual(A.lazyEnding('Deploying needs your API key; would you like me to deploy once you add it?'), '');
  assert.strictEqual(A.lazyEnding('Done.', { status: 'blocked', remaining: ['publishing needs your npm account'] }), '');
  assert.strictEqual(A.lazyEnding('Done.', { status: 'partial', remaining: ['signing needs your hardware token'] }), '');
  assert.strictEqual(A.lazyEnding('Built and tested the whole app. Run it with npm start.'), '');
});
t('stubs are found, placeholder attributes are not', () => {
  const s = A.findStubs([{ path: 'src/a.js', text: 'function f() {\n  // TODO: implement\n  throw new Error("not implemented");\n}\n' }]);
  assert.strictEqual(s.length, 2); assert.strictEqual(s[0].line, 2);
  assert.strictEqual(A.findStubs([{ path: 'index.html', text: '<input placeholder="Search">\n<style>input::placeholder{color:#999}</style>' }]).length, 0);
  assert.strictEqual(A.findStubs([{ path: 'app.js', text: 'input.placeholder = t("search");\nconst todos = [];\naddTodo(text);' }]).length, 0);
  assert.strictEqual(A.findStubs([{ path: 'README.md', text: 'TODO: write docs' }]).length, 0, 'docs are not code');
  assert.strictEqual(A.findStubs([{ path: 'a.js', text: '// TODO: old', before: '// TODO: old' }]).length, 0, 'pre-existing lines are not new stubs');
});
t('audit order and once-per-kind', () => {
  const used = new Set();
  const a1 = A.audit({ job: 'is the app complete?', report: 'Yes, 100% complete!', inspected: 0, used });
  assert.strictEqual(a1.kind, 'assess'); used.add(a1.kind);
  const a2 = A.audit({ job: 'is the app complete?', report: 'Yes, 100% complete!', inspected: 5, used });
  assert.strictEqual(a2.kind, 'claim'); assert.ok(a2.message.includes('100% complete')); used.add(a2.kind);
  assert.strictEqual(A.audit({ job: 'is the app complete?', report: 'Yes, 100% complete!', inspected: 5, used }), null, 'never loops');
  assert.strictEqual(A.audit({ job: 'is the app complete?', report: 'Fully complete.', inspected: 5, checkRan: true, checkOk: true }), null, 'proven claims pass');
  assert.strictEqual(A.audit({ job: 'build x', report: 'Would you like me to add y?', mode: 'plan' }), null, 'plan mode is exempt');
  assert.strictEqual(A.audit({ job: 'build x', report: 'Would you like me to add y?', stepsLeft: 2 }), null, 'no lazy nudge without steps left');
  assert.strictEqual(A.audit({ job: 'build x and y', report: 'Built x. Want me to do y? Would you like me to continue?' }).kind, 'lazy');
  assert.strictEqual(A.audit({ job: 'build x', report: 'Built x, ran npm test: 12 passed.', written: [{ path: 'x.js', text: 'export const x = 1;' }] }), null);
  assert.strictEqual(A.audit({ job: 'build x', report: 'Built x.', written: [{ path: 'x.js', text: '// FIXME later' }] }).kind, 'stub');
});
t('messages contain no unreplaced markers', () => {
  const m = A.audit({ job: 'b', report: 'x', written: [{ path: 'x.js', text: '// TODO $& $1' }] }).message;
  assert.ok(m.includes('$&') && !m.includes('%STUBS%'));
});
console.log(`agent audit: ${n} tests passed`);
