// Unit test for the agent's tool-call parser (functions taken straight from web/app.js).
// Usage: node tests/agent_parse_test.js
const fs = require('fs'); const path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'web', 'app.js'), 'utf8');
const grab = (name) => { const i = src.indexOf(`  function ${name}(`); if (i < 0) throw new Error('missing ' + name); const j = src.indexOf('\n  }\n', i); return src.slice(i, j + 4); };
const alias = src.match(/const AG_ALIAS = (\{[\s\S]*?\});/);
const nativeMark = (src.match(/^ *const AG_NATIVE_MARK = .*;$/m) || ['const AG_NATIVE_MARK = /$^/g;'])[0].trim();
const code = `${alias ? 'const AG_ALIAS = ' + alias[1] + ';' : 'const AG_ALIAS = {};'}
${nativeMark}
const cleanPath = (p) => String(p || '').replace(/^\\.?\\//, '');
${grab('stripReasoningLeak')}${grab('agJson')}${grab('agJsonAll')}${grab('agNorm')}${grab('agParse')}
module.exports = { agParse, stripReasoningLeak };`;
const m = { exports: {} }; new Function('module', code)(m);
const { agParse, stripReasoningLeak } = m.exports;
let pass = 0, fail = 0; const ok = (c, msg) => { if (c) { pass++; console.log('  ok  ' + msg); } else { fail++; console.log('  FAIL ' + msg); } };
const T = '```';
let r = agParse(`Plan.\n${T}tool\n{"tool":"read_file","path":"a.js"}\n${T}`);
ok(r.calls.length === 1 && r.calls[0].tool === 'read_file' && r.calls[0].path === 'a.js', 'single call');
r = agParse(`${T}tool\n{"tool":"read_file","path":"src/limit.js"} {"tool":"read_file","path":"src/server.js"}\n${T}`);
ok(r.calls.length === 2 && r.calls[1].path === 'src/server.js', 'two objects in one block are split');
r = agParse(`${T}tool\n{"tool":"write_file","path":"a.txt","content":"x { y } \\"z\\""}\n{"tool":"bash","command":"echo }"}\n${T}`);
ok(r.calls.length === 2 && r.calls[0].content === 'x { y } "z"' && r.calls[1].command === 'echo }', 'braces and quotes inside strings');
r = agParse(`${T}tool\n[{"tool":"read_file","path":"a"},{"tool":"read_file","path":"b"}]\n${T}`);
ok(r.calls.length === 2 && r.calls[1].path === 'b', 'JSON array of calls');
r = agParse(`${T}tool\n{"tool":"read_file","path": oops}\n${T}`);
ok(r.calls.length === 1 && r.calls[0].tool === '_bad', 'broken JSON still reported');
r = agParse(`${T}tool\n{"tool":"write_file","path":"a.js"}\n${T}`);
ok(r.calls.length === 1 && r.calls[0].tool === 'write_file' && r.calls[0].content === '', 'write_file without content parses (guard rejects it at run time)');
ok(/has no "content"/.test(src) && /Never wipe a file/.test(src), 'write_file guard present');
ok(/let unverified = false/.test(src) && /VERIFY FIRST/.test(src) && /so your report is a guess/.test(src), 'done is held back until changed files were checked by a run, and never accepted in the same reply as tool calls');
ok(stripReasoningLeak('analysisThe user wants a file. assistantfinalHere you go.') === 'Here you go.', 'gpt-oss reasoning leak is removed');
ok(stripReasoningLeak('Normal answer about analysis.') === 'Normal answer about analysis.', 'normal text is untouched');
{ // MiniMax-style native tool-call markup must never reach the user
  const r = agParse('I will read the file now.\n<minimax:tool_call>\n<invoke name="read_file">\n<parameter name="path">src/a.js</parameter>\n</invoke>\n</minimax:tool_call>');
  const shown = JSON.stringify(r && (r.narrative ?? r.text ?? ''));
  ok(!/minimax|tool_call|invoke|parameter/.test(shown), 'MiniMax tool-call markup is hidden from the reply');
  ok(r.calls.length === 1 && r.calls[0].tool === 'read_file' && r.calls[0].path === 'src/a.js', 'MiniMax XML call becomes a real read_file call');
}
// 1.7: OpenAI-style native calls carry arguments as a JSON string (gpt-oss): the content must survive
r = agParse(`${T}tool\n{"name":"write_file","arguments":"{\\"path\\":\\"a.js\\",\\"content\\":\\"let x = 1;\\"}"}\n${T}`);
ok(r.calls.length === 1 && r.calls[0].tool === 'write_file' && r.calls[0].path === 'a.js' && r.calls[0].content === 'let x = 1;', 'string arguments (OpenAI style) are unpacked: write_file keeps its content');
r = agParse(`${T}tool\n{"tool":"write_file","path":"b.txt","content":"","file_text":"hello"}\n${T}`);
ok(r.calls[0].content === 'hello', 'an empty "content" does not hide the real text in an alias (file_text)');
r = agParse(`${T}tool\n{"tool":"edit_file","path":"c.js","old_string":"a","new_string":""}\n${T}`);
ok(r.calls[0].old_text === 'a' && r.calls[0].new_text === '', 'old_string/new_string aliases; an empty new_text stays empty');
ok(/HANDOVER: the previous model stopped/.test(src) && /kept sending empty file writes/.test(src), 'a model that stops after failed steps hands the job to the next model');
ok(/nothingDone/.test(src) && /Needs attention: nothing was changed/.test(src), 'a run whose file changes all failed is shown as Needs attention, never Done');
ok(/AG_REASONING\.test/.test(src), 'leaked reasoning text is hidden on the Agent page');
console.log(`${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
