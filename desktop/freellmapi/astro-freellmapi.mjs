// A.S.T.R.O AI: runs the bundled FreeLLMAPI router (MIT, https://github.com/tashfeenahmed/freellmapi) headless,
// next to the engine, and keeps it filled with the provider keys you saved in A.S.T.R.O AI.
//
// The desktop app starts it (only when a freellmapi/ folder is bundled, i.e. the personal edition) as
//   ELECTRON_RUN_AS_NODE=1 freellmapi/app/FreeLLMAPI.exe freellmapi/astro-freellmapi.mjs
// so FreeLLMAPI's own Electron build runs as plain Node, with its own native SQLite module. No window, no tray.
//
// Environment (set by the desktop app):
//   ASTRO_FL_DATA   FreeLLMAPI's state folder (database + its encryption key file). Default: ./data
//   ASTRO_FL_PORT   preferred port on 127.0.0.1. Default 47631
//   ASTRO_FL_KEYS   A.S.T.R.O AI's keys file (.astro_keys.env in the app's data folder)
//
// What it does on every start:
//   1. starts FreeLLMAPI (OpenAI-compatible /v1 and Anthropic-compatible /v1/messages, dashboard at /)
//   2. sets FreeLLMAPI's unified API key to FREELLMAPI_API_KEY from the keys file, so the A.S.T.R.O AI
//      provider "FreeLLMAPI on this computer" can call it
//   3. adds, updates or removes the keys it manages (label "A.S.T.R.O AI") so they match the keys file.
//      Keys you add yourself in the FreeLLMAPI dashboard are never touched.
// Keys are never printed. FreeLLMAPI stores them encrypted (AES-256-GCM) in its own database.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const resources = process.env.ASTRO_FL_RESOURCES || path.join(here, 'app', 'resources');
const dataDir = process.env.ASTRO_FL_DATA || path.join(here, 'data');
const preferredPort = Number(process.env.ASTRO_FL_PORT || 47631);
const keysFile = process.env.ASTRO_FL_KEYS || '';
const LABEL = 'A.S.T.R.O AI';

// A.S.T.R.O AI key names -> FreeLLMAPI platform ids (only providers both apps know).
export const PLATFORM_OF = {
  AGNES_API_KEY: 'agnes', AION_API_KEY: 'aion', AIRFORCE_API_KEY: 'airforce', BAZAARLINK_API_KEY: 'bazaarlink',
  CEREBRAS_API_KEY: 'cerebras', CLOD_API_KEY: 'clod', CLOUDFLARE_API_KEY: null, COHERE_API_KEY: 'cohere',
  DREAMPROMPTING_API_KEY: 'dreamprompting', ELECTRONHUB_API_KEY: 'electronhub', GEMINI_API_KEY: 'google',
  GOOGLE_API_KEY: 'google', GROQ_API_KEY: 'groq', HF_TOKEN: 'huggingface', MISTRAL_API_KEY: 'mistral',
  NVIDIA_API_KEY: 'nvidia', OLLAMA_API_KEY: 'ollama', OPENROUTER_API_KEY: 'openrouter', ORCAROUTER_API_KEY: 'orcarouter',
  REKA_API_KEY: 'reka', REQUESTY_API_KEY: 'requesty', ROUTER9_API_KEY: 'router9', ROUTEWAY_API_KEY: 'routeway',
  SAMBANOVA_API_KEY: 'sambanova', SEPTOR_API_KEY: 'septor', SILICONFLOW_API_KEY: 'siliconflow', SPEKA_API_KEY: 'speka',
  UNOROUTER_API_KEY: 'unorouter', XKIRO_API_KEY: 'xkiro', ZAI_API_KEY: 'zhipu', ZHIPU_API_KEY: 'zhipu',
};

export function readEnvFile(file) {
  const out = {};
  if (!file) return out;
  let text = '';
  try { text = fs.readFileSync(file, 'utf8'); } catch { return out; }
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq <= 0) continue;
    const k = line.slice(0, eq).trim().replace(/^export\s+/, '');
    let v = line.slice(eq + 1).trim();
    if (v.length >= 2 && ((v[0] === '"' && v.endsWith('"')) || (v[0] === "'" && v.endsWith("'")))) v = v.slice(1, -1);
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(k) && v) out[k] = v;
  }
  return out;
}

// platform -> key, first name wins (GEMINI_API_KEY before GOOGLE_API_KEY, ZAI before ZHIPU)
export function wantedKeys(env) {
  const want = {};
  for (const [name, platform] of Object.entries(PLATFORM_OF)) if (platform && env[name] && !want[platform]) want[platform] = env[name];
  return want;
}

const fp = (key) => crypto.createHash('sha256').update(key).digest('hex').slice(0, 16);
const log = (...a) => console.log(new Date().toISOString(), '[astro-freellmapi]', ...a);

async function main() {
  fs.mkdirSync(dataDir, { recursive: true });
  process.env.FREEAPI_VERSION ||= 'bundled';
  const server = await import(pathToFileURL(path.join(resources, 'app.asar', 'build', 'server.mjs')).href);
  const { port } = await server.startServer({
    dbPath: path.join(dataDir, 'freeapi.db'),
    clientDist: path.join(resources, 'client-dist'),
    host: '127.0.0.1',
    preferredPort,
  });
  if (port !== preferredPort) log(`WARNING: port ${preferredPort} was busy, using ${port}. Set FREELLMAPI_URL=http://127.0.0.1:${port}/v1 in Settings > Keys.`);
  fs.writeFileSync(path.join(dataDir, 'astro-status.json'), JSON.stringify({ port, pid: process.pid, started: new Date().toISOString() }));

  const env = readEnvFile(keysFile);
  const unified = env.FREELLMAPI_API_KEY || '';
  if (/^freellmapi-[0-9a-f]{48}$/.test(unified)) {
    if (server.getUnifiedApiKey() !== unified) {
      server.getDb().prepare("UPDATE settings SET value = ? WHERE key = 'unified_api_key'").run(unified);
      log('unified API key set from the A.S.T.R.O AI keys file');
    }
  } else {
    log('no FREELLMAPI_API_KEY in the keys file; A.S.T.R.O AI cannot call FreeLLMAPI until it is set');
  }

  // Sync the managed keys through FreeLLMAPI's own admin API (same validation as its dashboard).
  const token = server.ensureSessionToken();
  const base = `http://127.0.0.1:${port}`;
  const api = async (p, method = 'GET', body) => {
    const r = await fetch(base + p, {
      method,
      headers: { 'content-type': 'application/json', authorization: `Bearer ${token}` },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    let json = null;
    try { json = await r.json(); } catch { /* empty */ }
    return { status: r.status, json };
  };
  const statePath = path.join(dataDir, 'astro-sync.json');
  let state = {};
  try { state = JSON.parse(fs.readFileSync(statePath, 'utf8')) || {}; } catch { state = {}; }
  const listed = await api('/api/keys');
  const rows = Array.isArray(listed.json) ? listed.json : (listed.json?.keys || listed.json?.data || []);
  const liveIds = new Set(rows.filter((r) => r && r.id != null).map((r) => Number(r.id)));

  const want = wantedKeys(env);
  let added = 0, kept = 0, removed = 0, failed = 0;
  for (const [platform, s] of Object.entries(state)) {
    if (want[platform] && s.fp === fp(want[platform]) && liveIds.has(Number(s.id))) continue;
    if (s.id != null && liveIds.has(Number(s.id))) { await api(`/api/keys/${Number(s.id)}`, 'DELETE'); removed++; }
    delete state[platform];
  }
  for (const [platform, key] of Object.entries(want)) {
    if (state[platform]) { kept++; continue; }
    const r = await api('/api/keys', 'POST', { platform, key, label: LABEL });
    if (r.status >= 200 && r.status < 300 && r.json && r.json.id != null) { state[platform] = { id: Number(r.json.id), fp: fp(key) }; added++; }
    else { failed++; log(`could not add the ${platform} key (HTTP ${r.status}: ${String(r.json?.error?.message || '').slice(0, 160)})`); }
  }
  fs.writeFileSync(statePath, JSON.stringify(state, null, 2));
  log(`FreeLLMAPI ready on ${base}/v1 (dashboard ${base}/) - keys: ${added} added, ${kept} unchanged, ${removed} removed, ${failed} failed`);
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  main().catch((e) => { console.error('[astro-freellmapi] failed to start:', e && e.stack ? e.stack : e); process.exit(1); });
}
