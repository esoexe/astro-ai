// A scripted OpenAI-compatible model for the engine tests (tests/engine_e2e.sh).
// It reads the job from the conversation and answers with tool calls step by step, like a real coding model.
//   "create file NAME containing TEXT"   -> write_file, then bash cat, then the done block
//   "plan only"                          -> tries write_file (Plan mode must block it), then a plan + done
//   "ask me"                             -> ask_user, then done with the answer
//   "slow job"                           -> bash sleep 30 (to test stop)
//   "use tool NAME with K=V"            -> calls a project tool from .astro/tools, then done
//   "wipe deps"                          -> bash rm -rf node_modules/* (isolated copies must not touch the project's)
//   "edit NAME from A to B"              -> edit_file, bash cat, done
//   review prompts                       -> VERDICT: PASS
// Usage: node tests/mock_llm.js <port>
const http = require('http');
const port = Number(process.argv[2] || 4199);
let calls = 0;

function tool(obj) { return '```tool\n' + JSON.stringify(obj) + '\n```'; }
function done(summary) { return '```done\n' + JSON.stringify({ summary }) + '\n```'; }

function reply(messages) {
  const users = messages.filter((m) => m.role === 'user').map((m) => String(m.content));
  const all = users.join('\n\n');
  const last = users[users.length - 1] || '';
  if (/Review the work of A.S.T.R.O AI's autonomous agent/.test(last)) return 'VERDICT: PASS\n- The change is small and was checked.';
  if (/focused subagent/.test(last)) return 'Subagent report: nothing to change.';
  const jobs = [...all.matchAll(/JOB: ([^\n]*)/g)].map((m) => m[1]);
  const job = jobs[jobs.length - 1] || last;
  // count tool rounds since the latest job
  const since = all.slice(all.lastIndexOf('JOB: '));
  const rounds = (since.match(/TOOL RESULTS:/g) || []).length;
  const verify = /VERIFY FIRST|NOT DONE YET/.test(last);
  let m;
  if ((m = job.match(/create file (\S+) containing (.+)$/i))) {
    const [, name, text] = m;
    if (rounds === 0) return `I will create ${name}.\n` + tool({ tool: 'write_file', path: name, content: text + '\n' });
    if (rounds === 1 || verify) return 'Checking it.\n' + tool({ tool: 'bash', command: `cat ${name}` });
    return `Created ${name} and checked its content with cat.\n` + done(`created ${name}`);
  }
  if ((m = job.match(/edit (\S+) from (\S+) to (\S+)/i))) {
    const [, name, a, b] = m;
    if (rounds === 0) return tool({ tool: 'edit_file', path: name, old_text: a, new_text: b });
    if (rounds === 1 || verify) return tool({ tool: 'bash', command: `cat ${name}` });
    return `Changed ${a} to ${b} in ${name}.\n` + done('edited');
  }
  if ((m = job.match(/use tool (\w+) with (\w+)=(.+)$/i))) {
    const [, name, k, v] = m;
    if (rounds === 0) return tool({ tool: name, [k]: v });
    return 'Used the project tool.\n' + done('custom tool');
  }
  if (/missing brace/i.test(job)) {
    if (rounds === 0) return '```tool\n{"tool":"bash","command":"echo b > brace.txt"\n```';
    return 'Done.\n' + done('brace');
  }
  if (/wipe deps/i.test(job)) {
    if (rounds === 0) return tool({ tool: 'bash', command: 'rm -rf node_modules/* node_modules/.package-lock.json && npm_ci_like=1 && mkdir -p node_modules/fresh && echo new > node_modules/fresh/index.js && ls node_modules' });
    return 'Done.\n' + done('wiped');
  }
  if (/fork cwd/i.test(job)) {
    if (rounds === 0) return '```tool\n{"tool":"bash","command":"echo f > fork.txt","cwd":"demo"}\n```';
    return 'Done.\n' + done('fork');
  }
  if (/fence in json/i.test(job)) {
    if (rounds === 0) return 'Writing the docs.\n```tool\n' + JSON.stringify({ tool: 'write_file', path: 'DOCS.md', content: '# Docs\n\n```bash\ncurl http://localhost:3000/todos\n```\n' }) + '\n```';
    return 'Done.\n' + done('fence');
  }
  if (/blind rewrite/i.test(job)) {
    if (rounds === 0) return '```tool\n{"tool":"write_file","path":"keep.txt","content":"replaced"}\n```';
    if (rounds === 1) return '```tool\n{"tool":"read_file","path":"keep.txt"}\n```';
    if (rounds === 2) return '```tool\n{"tool":"write_file","path":"keep.txt","content":"replaced"}\n```';
    return 'Done.\n' + done('blind');
  }
  if (/failing check/i.test(job)) {
    if (rounds < 4) return '```tool\n{"tool":"bash","command":"npm test || node --test nothing-here.js"}\n```';
    return 'All good.\n' + done('failing');
  }
  if (/multi open/i.test(job)) {
    if (rounds === 0) return 'Reading.<tool_call>bash {command": "echo m1 > m1.txt"}<tool_call>bash(command="echo m3 > m3.txt")<tool_call>bash<arg_key>command</arg_key><arg_value>echo m2 > m2.txt</arg_value></tool_call>';
    return 'Done.\n' + done('multi');
  }
  if (/native markup/i.test(job)) {
    if (rounds === 0) return 'Checking.<tool_call>bash<arg_key>command</arg_key><arg_value>echo glm > glm.txt</arg_value></tool_call>\n<tool_call>{"name": "bash", "arguments": "{\\"command\\": \\"echo q > q.txt\\"}"}</tool_call>';
    return 'Done.\n' + done('native');
  }
  if (/dup and think/i.test(job)) {
    if (rounds === 0) return 'We need to run the command now.\n```tool\n{"tool":"bash","command":"echo x >> dup.txt","cwd":"demo"}\n{"tool":"bash","command":"echo x >> dup.txt","cwd":"demo"}\n```';
    return 'Done.\n' + done('dup');
  }
  if (/fenced bash call/i.test(job)) {
    if (rounds === 0) return 'Running it.\n```bash\n\n```\n```bash\n{"tool":"bash","command":"echo fenced > fenced.txt"}\n```';
    return 'Done.\n' + done('fenced');
  }
  // Finish audit scenarios (honest the first time, whole job, no stubs): the model first misbehaves, then complies after the nudge.
  if (/is the app complete\?/i.test(job)) {
    if (!/ASSESS FOR REAL/.test(since)) return 'Yes, the app is 100% complete and production-ready.\n' + done('complete');
    if (!(since.slice(since.indexOf('ASSESS FOR REAL')).match(/TOOL RESULTS:/g) || []).length) return 'Inspecting.\n' + tool({ tool: 'list_files' }) + '\n' + tool({ tool: 'bash', command: 'ls -la && cat README.md' });
    return 'STATUS REPORT\nVerified working: README.md exists and is readable.\nNot verified: there is no test suite, so nothing could be run.\nOverall: 1 of 2 requirements verified; not complete.\n' + done('status report');
  }
  if (/build parts one and two/i.test(job)) {
    const nudged = /DON'T STOP HALFWAY/.test(since);
    const after = nudged ? (since.slice(since.indexOf("DON'T STOP HALFWAY")).match(/TOOL RESULTS:/g) || []).length : 0;
    if (!nudged) {
      if (rounds === 0) return tool({ tool: 'write_file', path: 'one.txt', content: 'one\n' });
      if (rounds === 1) return tool({ tool: 'bash', command: 'cat one.txt' });
      return 'Created one.txt and checked it. Would you like me to create two.txt next?\n' + done('part one');
    }
    if (after === 0) return tool({ tool: 'write_file', path: 'two.txt', content: 'two\n' });
    if (after === 1) return tool({ tool: 'bash', command: 'cat one.txt two.txt' });
    return 'Created one.txt and two.txt and checked both with cat.\n' + done('both parts');
  }
  if (/write the adder/i.test(job)) {
    const nudged = /PLACEHOLDERS LEFT/.test(since);
    const after = nudged ? (since.slice(since.indexOf('PLACEHOLDERS LEFT')).match(/TOOL RESULTS:/g) || []).length : 0;
    if (!nudged) {
      if (rounds === 0) return tool({ tool: 'write_file', path: 'add.js', content: 'function add(a, b) {\n  // TODO: implement\n}\nmodule.exports = add;\n' });
      if (rounds === 1) return tool({ tool: 'bash', command: 'node -e "require(\'./add.js\')"' });
      return 'Wrote add.js.\n' + done('adder');
    }
    if (after === 0) return tool({ tool: 'write_file', path: 'add.js', content: 'function add(a, b) {\n  return a + b;\n}\nmodule.exports = add;\n' });
    if (after === 1) return tool({ tool: 'bash', command: 'node -e "console.log(require(\'./add.js\')(2, 3))"' });
    return 'add.js now returns a + b; node printed 5.\n' + done('adder real');
  }
  if (/plan only/i.test(job)) {
    if (rounds === 0) return tool({ tool: 'write_file', path: 'should_not_exist.txt', content: 'x' });
    return 'PLAN:\n1. Create the file.\n2. Test it.\n' + done('plan written');
  }
  if (/ask me/i.test(job)) {
    if (rounds === 0) return tool({ tool: 'ask_user', question: 'Which colour?', options: ['red', 'blue'] });
    const ans = (since.match(/The user answered: (.*)/) || [])[1] || 'none';
    return `You chose ${ans}.\n` + done(`answer ${ans}`);
  }
  if (/slow job/i.test(job)) {
    if (rounds === 0) return tool({ tool: 'bash', command: 'sleep 30', timeout: 60 });
    return done('slept');
  }
  if (/approve this/i.test(job)) {
    if (rounds === 0) return tool({ tool: 'bash', command: 'echo approved > approved.txt' });
    if (rounds === 1) return tool({ tool: 'bash', command: 'cat approved.txt || true' });
    return 'Finished.\n' + done('ran');
  }
  return 'Nothing to do.\n' + done('nothing');
}

http.createServer((req, res) => {
  let body = '';
  req.on('data', (c) => { body += c; });
  req.on('end', () => {
    if (req.url.endsWith('/models')) { res.writeHead(200, { 'Content-Type': 'application/json' }); return res.end(JSON.stringify({ data: [{ id: 'mock-coder' }] })); }
    if (!req.url.endsWith('/chat/completions')) { res.writeHead(404); return res.end('{}'); }
    let j = {}; try { j = JSON.parse(body); } catch (_e) { /* bad */ }
    calls++;
    const text = reply(j.messages || []);
    if (!j.stream) {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: text } }] }));
    }
    res.writeHead(200, { 'Content-Type': 'text/event-stream' });
    const parts = text.match(/[\s\S]{1,40}/g) || [''];
    let i = 0;
    const tick = () => {
      if (i < parts.length) { res.write(`data: ${JSON.stringify({ choices: [{ delta: { content: parts[i++] } }] })}\n\n`); setTimeout(tick, 5); }
      else { res.write(`data: ${JSON.stringify({ choices: [{ delta: {}, finish_reason: 'stop' }] })}\n\ndata: [DONE]\n\n`); res.end(); }
    };
    tick();
  });
}).listen(port, '127.0.0.1', () => console.log(`mock llm on ${port}`));
