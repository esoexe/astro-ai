// A.S.T.R.O AI — agent finish audit (honesty + completeness).
// Runs when the agent sends its ```done block, before the run is accepted as finished. It catches the patterns users
// complained about: "yes, 100% complete" without looking (then "no" when asked to be honest), stopping halfway with
// "want me to continue?", and stubs/TODOs left in the files it wrote. Each rule nudges the agent at most once.
// The same rules are ported to C++ in astro_engine.inc (finish_audit) — keep them in sync.
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.AstroAudit = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // The job asks about the state of the work ("is the app complete?", "what is missing?", "audit the project").
  const ASSESS_JOB = /\b(is|are|was|were|does|do|did|has|have)\b[^?\n]{0,80}\b(complete|completed|finished|done|ready|working|work|works|perfect|bug[- ]?free|production[- ]?ready|missing|broken|implemented)\b|\bhow (complete|far along|much is left|close)\b|\bwhat('s| is| remains)? ?(left|missing|not done|unfinished|remaining)\b|\b(audit|assess|evaluate|status of|progress of|completeness)\b|\bbe honest\b|\bhonest(ly)? (answer|assessment|opinion)\b/i;
  // Absolute success claims that need proof.
  const ABSOLUTE = /\b100\s?%|\b(fully|completely|totally|entirely) (complete|completed|done|finished|implemented|functional|working|ready)\b|\b(nothing|no (work|features?|tasks?|items?)) (is |are )?(left|missing|remaining)\b|\bproduction[- ]ready\b|\bbug[- ]free\b|\b(everything|all (features|requirements|tasks|parts)) (is |are )?(done|complete|completed|working|implemented|finished)\b|\bworks perfectly\b|\bperfect(ly)? (working|complete)\b/i;
  // A sentence that hedges or negates the claim is honest, not a claim ("it is not 100% complete").
  const HEDGE = /n['\u2019]t\b|\b(not|never|far from|except|but|however|almost|nearly|mostly|partly|partially|once|after|when|if|until|unless|would|will be|should be)\b/i;
  // Ending the job by offering to do more instead of doing it.
  const LAZY = /\b(would you like me to|do you want me to|shall i\b|should i (continue|proceed|go ahead|also)|let me know if you('d| would)? (like|want)|if you('d| would) like,? i (can|could|will)|i can (also )?(continue|implement|add|finish|build|do) (the rest|the remaining|the other|this|that|it|them)\b[^.\n]{0,40}\b(next|later|if you|in a follow[- ]?up)|in a (future|next|follow[- ]?up|later) (step|iteration|session|pass|run|update)|left as an exercise|for (brevity|now|simplicity),? (i|we)('ve| have)? (only|skipped|left|omitted|stubbed)|(remaining|next) steps? (for you|you can take)|you (can|could|may) (now )?(implement|add|finish|complete) the (rest|remaining))/i;
  // Words that make a left-over item legitimately blocked (not laziness).
  const BLOCKED = /\b(api key|token|credential|account|password|sign[- ]?in|log[- ]?in|billing|payment|hardware|device|physical|permission|access|admin|sudo|your (decision|choice|approval)|only you|cannot (be done|run) here|not available (here|in this)|no (sdk|network|internet))\b/i;
  // Stubs in code the agent wrote. HTML/CSS placeholder attributes are not stubs.
  const STUB = /(\/\/|#|\/\*|<!--|--|;)\s*(TODO|FIXME|XXX|HACK)\b|\bnot (yet )?implemented\b|\bimplement (this|me|later)\b|lorem ipsum|(\/\/|#|\/\*)\s*\.\.\.\s*(rest|existing|more|remaining|other|same)\b|\b(stub|dummy|fake|mock(ed)?) (data|implementation|response|value)s?\b|\bplaceholder\b/i;
  const NOT_STUB = /placeholder\s*[=:]|::?-?(webkit-|moz-)?(input-)?placeholder|placeholder-|-placeholder|placeholder(Text|Color)|\.placeholder\b|placeholder\s*\(|['"`]placeholder['"`]/i;

  function sentences(text) {
    return String(text || '').replace(/```[\s\S]*?```/g, ' ').split(/(?<=[.!?])\s+|\n+/).map((x) => x.trim()).filter(Boolean);
  }
  // A status question needs question form (a "?" or a leading is/are/how/what…) or an explicit audit/honesty request,
  // so "fix the bug where login does not work" stays a build job.
  const ASSESS_EXPLICIT = /\b(audit|assess|evaluate|be honest|honest(ly)? (answer|assessment|opinion)|status of|progress of|completeness)\b/i;
  const BUILD_VERB = /\b(fix|build|implement|add|make|create|finish it|finish the|write|update|refactor|change|improve|port|migrate|deploy|install|set up|setup)\b/i;
  function isAssessJob(job) {
    const j = String(job || '');
    if (ASSESS_EXPLICIT.test(j)) return true;
    return ASSESS_JOB.test(j) && (j.includes('?') || /^\s*(is|are|was|were|does|do|did|has|have|how|what)\b/i.test(j));
  }
  // Only a question (nothing to build): the lazy-ending rule does not apply.
  function questionOnly(job) { return isAssessJob(job) && !BUILD_VERB.test(String(job || '')); }
  function absoluteClaim(report) {
    for (const s of sentences(report)) if (ABSOLUTE.test(s) && !HEDGE.test(s)) return s.slice(0, 160);
    return '';
  }
  function lazyEnding(report, done) {
    const r = String(report || '');
    for (const s of sentences(r)) if (LAZY.test(s) && !BLOCKED.test(s)) return s.slice(0, 160);
    const st = String(done?.status || '').toLowerCase();
    const rem = Array.isArray(done?.remaining) ? done.remaining.map(String).filter((x) => x.trim()) : [];
    if (/partial|incomplete|in[_ -]?progress|unfinished/.test(st) && rem.some((x) => !BLOCKED.test(x))) return `status "${st}" with remaining: ${rem.slice(0, 3).join('; ').slice(0, 160)}`;
    return '';
  }
  // Lines of newly written text that look like stubs. `written` = [{path, text, before?}]
  function findStubs(written) {
    const out = [];
    for (const w of written || []) {
      if (!w || !w.text || /\.(md|txt|rst)$/i.test(w.path || '') || /(^|\/)(test|tests|__tests__|spec|fixtures?|mocks?)\//i.test(w.path || '')) continue;
      const before = new Set(String(w.before || '').split('\n').map((l) => l.trim()));
      const lines = String(w.text).split('\n');
      for (let i = 0; i < lines.length && out.length < 8; i++) {
        const l = lines[i];
        if (!STUB.test(l) || NOT_STUB.test(l) || before.has(l.trim())) continue;
        out.push({ path: w.path, line: (w.line || 1) + i, text: l.trim().slice(0, 120) });
      }
    }
    return out;
  }

  const MSG = {
    assess: 'ASSESS FOR REAL: the job asks about the real state of the work, but you answered without inspecting it. Never answer a status question from memory or assumption. Now: list the files, read the key ones, run the build and the tests (and the app if it has one), and check every requirement of the original request one by one. Then reply with a STATUS REPORT — Verified working: … / Not working: … / Missing: … / Not verified: … — and an honest overall status based on that checklist (for example "7 of 10 requirements verified"). Only say "complete" if every requirement is verified. Then the ```done block.',
    claim: 'UNSUPPORTED CLAIM: your report says "%CLAIM%", but nothing run in this job proves it (no passing build or test run after the last change). Do not overstate. Either run the checks now and confirm each requirement with evidence, or rewrite the report as a calibrated status: what is verified (and how), what is not verified, what is missing. Give the honest answer now, so the user never has to ask "be honest".',
    lazy: 'DON\'T STOP HALFWAY: your report leaves part of the job for later ("%CLAIM%"). The user wants the WHOLE job done in this run, without back-and-forth. Do every remaining part that is possible here now (track them with the todo tool), verify it, then finish. Leave something out only when it is truly blocked (needs the user\'s account, key, hardware or a decision only they can make) and say exactly why in the report. Do not end with offers like "want me to continue?".',
    stub: 'PLACEHOLDERS LEFT: files you wrote in this job still contain stubs:\n%STUBS%\nImplement them for real now and verify. If a line is intentional (or was already there before your change), keep it and say so in the report.',
  };

  // ctx: { job, report, done, mode, inspected, checkRan, checkOk, written, stepsLeft, used:Set }
  // returns { kind, message } or null. Mark ctx.used with the kind you sent.
  function audit(ctx) {
    const used = ctx.used || new Set();
    if (String(ctx.mode || '') === 'plan') return null;
    if (used.size >= 3) return null;
    const job = String(ctx.job || ''); const report = String(ctx.report || '');
    const assess = isAssessJob(job);
    if (assess && !used.has('assess') && (ctx.inspected || 0) < 2) return { kind: 'assess', message: MSG.assess };
    const claim = absoluteClaim(report);
    if (claim && !used.has('claim') && !(ctx.checkRan && ctx.checkOk)) return { kind: 'claim', message: MSG.claim.replace('%CLAIM%', () => claim) };
    if (!questionOnly(job) && !used.has('lazy') && (ctx.stepsLeft === undefined || ctx.stepsLeft > 3)) {
      const lazy = lazyEnding(report, ctx.done);
      if (lazy) return { kind: 'lazy', message: MSG.lazy.replace('%CLAIM%', () => lazy) };
    }
    if (!used.has('stub')) {
      const stubs = findStubs(ctx.written);
      if (stubs.length) return { kind: 'stub', message: MSG.stub.replace('%STUBS%', () => stubs.map((x) => `- ${x.path}:${x.line}  ${x.text}`).join('\n')) };
    }
    return null;
  }

  return { audit, isAssessJob, questionOnly, absoluteClaim, lazyEnding, findStubs, MSG };
});
