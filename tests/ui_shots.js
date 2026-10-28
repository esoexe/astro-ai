// Screenshot tour of every A.S.T.R.O AI screen (light + dark + mobile), for design review.
// Usage: node tests/ui_shots.js [port] [prefix]   -> PNGs in .cache/shots/<prefix>-*.png
const path = require('path');
const fs = require('fs');
const PORT = Number(process.argv[2] || 4189);
const PREFIX = process.argv[3] || 'ui';
const BASE = `http://127.0.0.1:${PORT}`;
const root = path.join(__dirname, '..', '.cache', 'uitest');
const puppeteer = require(path.join(root, 'node_modules', 'puppeteer-core'));
const SHOTS = path.join(__dirname, '..', '.cache', 'shots');
fs.mkdirSync(SHOTS, { recursive: true });
const chromeDir = path.join(root, 'browsers', 'chrome-headless-shell');
const CHROME = path.join(chromeDir, fs.readdirSync(chromeDir).find((d) => d.startsWith('linux-')), 'chrome-headless-shell-linux64', 'chrome-headless-shell');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const now = Date.now();
const answer = [
  'Here is a fast way to do it in **C++20**: sort once, then binary-search each query.',
  '',
  '```cpp',
  '#include <algorithm>',
  '#include <vector>',
  '',
  'int count_less(const std::vector<int>& v, int x) {',
  '    return std::lower_bound(v.begin(), v.end(), x) - v.begin();',
  '}',
  '```',
  '',
  '### Why it is fast',
  '- Sorting costs **O(n log n)** once.',
  '- Each query is **O(log n)**, so 1M queries take milliseconds.',
  '- No extra memory beyond the vector itself.',
  '',
  '| Approach | Per query | Memory |',
  '|---|---|---|',
  '| Linear scan | O(n) | O(1) |',
  '| Sort + binary search | O(log n) | O(1) |',
].join('\n');
const chats = [
  { id: 'c1', title: 'Fast counting with binary search', updatedAt: now - 60e3, messages: [
    { role: 'user', content: 'How do I count how many numbers are less than x, for a million queries, quickly?' },
    { role: 'assistant', drafts: [{ content: answer, model: 'A.S.T.R.O AI team', provider: 'Team', ms: 8400, status: 'done' }], active: 0 },
  ] },
  { id: 'c2', title: 'Trip plan for Tamanrasset', updatedAt: now - 3600e3, messages: [{ role: 'user', content: 'Plan a 3-day trip' }, { role: 'assistant', drafts: [{ content: 'Day 1…', status: 'done' }], active: 0 }] },
  { id: 'c3', title: 'Explain transformers simply', updatedAt: now - 86400e3, messages: [{ role: 'user', content: 'Explain transformers' }, { role: 'assistant', drafts: [{ content: 'A transformer…', status: 'done' }], active: 0 }] },
  { id: 'c4', title: 'Email to the landlord', updatedAt: now - 3 * 86400e3, messages: [{ role: 'user', content: 'Write an email' }, { role: 'assistant', drafts: [{ content: 'Dear…', status: 'done' }], active: 0 }] },
];

(async () => {
  const browser = await puppeteer.launch({ executablePath: CHROME, args: ['--no-sandbox', '--disable-gpu'] });
  const errors = [];
  async function open(theme, vp = { width: 1440, height: 900 }) {
    const page = await browser.newPage();
    page.on('pageerror', (e) => errors.push(e.message));
    await page.setViewport(vp);
    await page.evaluateOnNewDocument((t, c) => {
      if (window !== window.top) return;
      localStorage.setItem('workspace_theme_v1', t);
      localStorage.setItem('aster_ai_conversations_v1', JSON.stringify(c));
      localStorage.setItem('workspace_name_v1', 'Amine');
    }, theme, chats);
    await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
    await sleep(900);
    return page;
  }
  const shot = async (page, name) => { await sleep(500); await page.screenshot({ path: path.join(SHOTS, `${PREFIX}-${name}.png`) }); console.log('  ' + name); };
  const click = (page, sel) => page.evaluate((s) => { const e = document.querySelector(s); if (e) e.click(); return !!e; }, sel);

  for (const theme of ['light', 'dark']) {
    const p = await open(theme);
    await shot(p, `${theme}-home`);
    await p.evaluate(() => { const b = [...document.querySelectorAll('#conversation-list *')].find((x) => /binary search/i.test(x.textContent) && x.closest('button,[role=button],a,.chat-item')); (b?.closest('button,[role=button],a,.chat-item') || b)?.click(); });
    await sleep(800);
    await shot(p, `${theme}-chat`);
    await click(p, '#nav-workspace'); await sleep(2500);
    await shot(p, `${theme}-workspace`);
    await click(p, '#nav-agent'); await sleep(2000);
    await shot(p, `${theme}-agent`);
    if (theme === 'light') {
      for (const v of ['notes', 'tasks', 'reader', 'lab']) { await click(p, `#nav-${v}`); await sleep(1200); await shot(p, `${theme}-${v}`); }
      await click(p, '#nav-assistant'); await sleep(400);
      await click(p, '#open-settings'); await sleep(900); await shot(p, `${theme}-settings`);
      await p.keyboard.press('Escape'); await sleep(300);
      await p.keyboard.down('Control'); await p.keyboard.press('k'); await p.keyboard.up('Control'); await sleep(700); await shot(p, `${theme}-palette`);
    }
    await p.close();
  }
  const m = await open('light', { width: 400, height: 860, isMobile: true, hasTouch: true, deviceScaleFactor: 1 });
  await shot(m, 'mobile-home');
  await click(m, '#open-sidebar'); await sleep(700); await shot(m, 'mobile-menu');
  await click(m, '#sidebar-backdrop'); await sleep(400);
  await click(m, '#nav-workspace'); await sleep(2500); await shot(m, 'mobile-workspace');
  await m.close();
  const md = await open('dark', { width: 400, height: 860, isMobile: true, hasTouch: true, deviceScaleFactor: 1 });
  await shot(md, 'mobile-dark-home');
  await md.close();
  await browser.close();
  console.log(errors.length ? 'PAGE ERRORS:\n' + errors.join('\n') : 'no page errors');
})().catch((e) => { console.error(e); process.exit(1); });
