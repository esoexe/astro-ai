// A scripted OpenAI-compatible model for the engine tests (tests/engine_e2e.sh).
// It reads the job from the conversation and answers with tool calls step by step, like a real coding model.
//   "create file NAME containing TEXT"   -> write_file, then bash cat, then the done block
//   "plan only"                          -> tries write_file (Plan mode must block it), then a plan + done
//   "ask me"                             -> ask_user, then done with the answer
//   "slow job"                           -> bash sleep 30 (to test stop)
//   "use tool NAME with K=V"            -> calls a project tool from .astro/tools, then done
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
  if (/Review the work of Astro AI's autonomous agent/.test(last)) return 'VERDICT: PASS\n- The change is small and was checked.';
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
  if (/fenced bash call/i.test(job)) {
    if (rounds === 0) return 'Running it.\n```bash\n\n```\n```bash\n{"tool":"bash","command":"echo fenced > fenced.txt"}\n```';
    return 'Done.\n' + done('fenced');
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
