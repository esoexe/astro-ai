// A.S.T.R.O AI SDK for JavaScript (Node 18+, Deno, Bun, browsers): a thin client for the agent engine API (/api/agent/*).
// No dependencies. MIT OR Apache-2.0 OR GPL-3.0.
//
//   import { Astro } from './index.mjs';
//   const astro = new Astro();                                  // finds the local engine (ASTRO_URL, 47612, 47611, 4173)
//   const project = await astro.openFolder('/path/to/repo');   // or use an existing project name
//   const run = await astro.start({ project, prompt: 'add a /health endpoint', isolate: true });
//   for await (const e of run.events()) if (e.type === 'tool_done') console.log(e.label);
//   console.log(await run.diff()); await run.merge();

const LIVE = new Set(['running', 'waiting', 'queued']);
const env = (k) => (typeof process !== 'undefined' && process.env ? process.env[k] : undefined);

export class AstroError extends Error {
  constructor(message, status, data) { super(message); this.name = 'AstroError'; this.status = status; this.data = data; }
}

export class Astro {
  /** @param {{url?: string, token?: string, fetch?: typeof fetch}} [opts] */
  constructor(opts = {}) {
    this.url = (opts.url || env('ASTRO_URL') || '').replace(/\/+$/, '');
    this.token = opts.token || env('ASTRO_TOKEN') || '';
    this._fetch = opts.fetch || globalThis.fetch.bind(globalThis);
  }

  /** Finds a running engine when no URL was given. */
  async connect() {
    if (this.url) return this.url;
    for (const port of [47612, 47611, 4173]) {
      const u = `http://127.0.0.1:${port}`;
      try {
        const r = await this._fetch(`${u}/api/status`, { headers: { 'X-Astro-Client': '1' }, signal: AbortSignal.timeout(2000) });
        if (r.status === 200 || r.status === 401) { this.url = u; return u; }
      } catch (_e) { /* next port */ }
    }
    throw new AstroError('No A.S.T.R.O AI engine found. Open the A.S.T.R.O AI app, run `astro server start`, or pass {url}.', 0);
  }

  /** Raw call: POST /api/<route> with a JSON body. */
  async call(route, body = {}, signal) {
    const url = await this.connect();
    const headers = { 'Content-Type': 'application/json', 'X-Astro-Client': '1' };
    if (this.token) headers['X-Astro-Token'] = this.token;
    const r = await this._fetch(`${url}/api/${route}`, { method: 'POST', headers, body: JSON.stringify(body), signal });
    let j = {};
    try { j = await r.json(); } catch (_e) { /* empty body */ }
    if (!r.ok) throw new AstroError(j.error || `HTTP ${r.status}`, r.status, j);
    return j;
  }

  /** Opens a folder on the engine's computer in place; returns the project name to use. */
  async openFolder(path) { return (await this.call('dev/fs/open', { path })).name; }
  /** Coding models, best first: {ranked, all}. */
  models() { return this.call('agent/models'); }
  /** Agent sessions: {sessions, running, max}. */
  list(opts = {}) { return this.call('agent/list', opts); }
  /** A session handle for an existing id. */
  session(id) { return new Run(this, id); }

  /**
   * Starts an agent.
   * @param {{project: string, prompt: string, mode?: 'auto'|'ask'|'plan', model?: string, review?: boolean,
   *          isolate?: boolean, interactive?: boolean, max_steps?: number}} opts
   */
  async start(opts) {
    const r = await this.call('agent/start', opts);
    const run = new Run(this, r.id);
    Object.assign(run, { project: r.project, isolated: !!r.isolated, git: !!r.git });
    return run;
  }

  /** Starts one agent per prompt, each in an isolated copy; resolves when all finish. */
  async parallel(project, prompts, opts = {}) {
    const runs = await Promise.all(prompts.map((prompt) => this.start({ ...opts, project, prompt, isolate: true, interactive: opts.interactive ?? false })));
    return Promise.all(runs.map(async (run) => ({ run, result: await run.wait() })));
  }
}

export class Run {
  constructor(astro, id) { this.astro = astro; this.id = id; this.seq = 0; }
  _a(action, extra = {}, signal) { return this.astro.call(`agent/${action}`, { id: this.id, ...extra }, signal); }

  /**
   * Streams events as they happen (long polling). Ends when the session stops running.
   * Pass onPrompt to answer approvals/questions: async (event) => 'Allow' | 'Deny' | 'any text'.
   */
  async *events({ since = 0, signal, onPrompt } = {}) {
    let cursor = since; let live = ''; const pending = new Map(); const asked = new Set();
    for (;;) {
      const r = await this._a('events', { since: cursor, wait: 25, live }, signal);
      for (const e of r.events || []) {
        cursor = Math.max(cursor, e.seq); this.seq = cursor;
        if (e.type === 'approval' || e.type === 'question') pending.set(e.id, e);
        yield e;
      }
      live = r.live || '';
      if (onPrompt && r.waiting && !asked.has(r.waiting) && pending.has(r.waiting)) {
        asked.add(r.waiting);
        const ans = await onPrompt(pending.get(r.waiting));
        await this.answer(ans || 'Deny', r.waiting);
      }
      if (!LIVE.has(r.status) && !r.running) return;
    }
  }
  /** Waits for the end; returns {status, text, files, steps, ms, model, review}. */
  async wait(opts = {}) {
    let done = null; let review = null; let error = null;
    for await (const e of this.events(opts)) {
      if (e.type === 'done') done = e;
      if (e.type === 'review') review = e;
      if (e.type === 'error') error = e.text;
    }
    const s = await this.get({ since: 1e12 });
    return { status: s.status, text: done?.text || '', files: done?.files || [], steps: done?.steps ?? s.steps, ms: done?.ms, model: done?.model, review, error };
  }
  get(opts = {}) { return this._a('get', opts); }
  send(prompt) { return this._a('send', { prompt }); }
  answer(answer, qid) { return this._a('answer', { answer, ...(qid ? { qid } : {}) }); }
  stop() { return this._a('stop'); }
  setMode(mode) { return this._a('mode', { mode }); }
  changes() { return this._a('changes'); }
  async diff() { return (await this._a('diff')).diff || ''; }
  /** Applies an isolated agent's changes. {force} overwrites conflicts, {partial} skips them, {keep} keeps the copy. */
  merge({ force = false, partial = false, keep = false } = {}) { return this._a('merge', { force, partial, discard: !keep }); }
  discard() { return this._a('discard'); }
  undo() { return this._a('undo'); }
  delete() { return this._a('delete'); }
}

export default Astro;
