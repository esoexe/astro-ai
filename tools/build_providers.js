#!/usr/bin/env node
// Builds providers.json for Astro AI from:
//   * tools/free_catalog.json  (public FreeLLMAPI catalog snapshot: free models per platform, limits, vision flags)
//   * the hand-verified entries already in providers.json (OpenCode, NVIDIA, Free.ai, Local, OpenCode paid)
//   * the PLATFORMS table below (base URLs, key names, sign-up pages, notes)
// Usage: node tools/build_providers.js            (refresh the catalog first with --fetch)
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const catalogPath = path.join(__dirname, 'free_catalog.json');

async function main() {
  if (process.argv.includes('--fetch')) {
    const res = await fetch('https://api.freellmapi.co/v1/latest');
    if (!res.ok) throw new Error(`catalog HTTP ${res.status}`);
    fs.writeFileSync(catalogPath, await res.text());
    console.log('catalog refreshed');
  }
  const catalog = JSON.parse(fs.readFileSync(catalogPath, 'utf8'));
  const current = JSON.parse(fs.readFileSync(path.join(root, 'providers.json'), 'utf8'));
  const keep = new Map(current.providers.map((p) => [p.id, p]));

  // id, name, base, key (null = keyless), signup, extra fields.
  // Order = Auto order (fast + reliable first). Keyless platforms work without any account.
  const PLATFORMS = [
    ['opencode', null],
    ['groq', 'Groq', 'https://api.groq.com/openai/v1', 'GROQ_API_KEY', 'https://console.groq.com/keys', { cat: 'groq', privacy: 'Not used for training' }],
    ['gemini', 'Google Gemini', 'https://generativelanguage.googleapis.com/v1beta/openai', 'GEMINI_API_KEY', 'https://aistudio.google.com/apikey', { cat: 'google', privacy: 'Free-tier prompts may be used to improve Google products (outside the EU/UK)' }],
    ['cerebras', 'Cerebras', 'https://api.cerebras.ai/v1', 'CEREBRAS_API_KEY', 'https://cloud.cerebras.ai', { discover: true, models: ['gpt-oss-120b', 'qwen-3-235b-a22b-instruct-2507', 'llama3.1-8b'], limits: 'Free tier with daily token caps; sign-up may ask for a card' }],
    ['nvidia', null],
    ['atria', 'Atria (Atria Dawn)', 'https://api.atria-asi.ai/v1', 'ATRIA_API_KEY', 'https://api.atria-asi.ai/console/keys', { models: ['Atria-Dawn'], labels: { 'Atria-Dawn': 'Atria Dawn 744B' }, wait_seconds: 120, limits: 'Free preview (Google sign-in, free token grant; quotas tightened over time)', privacy: 'Shanghai AI Lab preview service; check its terms' }],
    ['ovh', 'OVHcloud AI Endpoints', 'https://oai.endpoints.kepler.ai.cloud.ovh.net/v1', 'OVH_API_KEY', 'https://endpoints.ai.cloud.ovh.net', { keyless: true, cat: 'ovh', vision: ['Qwen2.5-VL-72B-Instruct'], limits: 'No key needed: about 2 requests/min per model per IP', privacy: 'EU (France) hosting' }],
    ['kilo', 'Kilo Gateway', 'https://api.kilo.ai/api/gateway/v1', 'KILO_API_KEY', 'https://kilo.ai', { keyless: true, cat: 'kilo', models: ['recursive/flint-v1.0:free', 'cohere/north-mini-code:free', 'poolside/laguna-s-2.1:free', 'dots-studio/dots-3-note-preview:free', 'xingchen-agi/xing4.0-29b-a4b:free'], trains: true, limits: 'No key needed: about 200 requests/hour per IP for :free routes', privacy: 'Anonymous free routes may log prompts and outputs for training' }],
    ['openrouter', 'OpenRouter', 'https://openrouter.ai/api/v1', 'OPENROUTER_API_KEY', 'https://openrouter.ai/keys', { cat: 'openrouter', discover: true, discover_suffix: ':free', shared_quota: true, trains: true, limits: '20 requests/min, 50 requests/day on :free models (1,000/day after a one-time $10 top-up)', privacy: 'Some free providers log prompts' }],
    ['mistral', 'Mistral', 'https://api.mistral.ai/v1', 'MISTRAL_API_KEY', 'https://console.mistral.ai/api-keys', { cat: 'mistral', trains: true, limits: 'Free "Experiment" plan with rate limits', privacy: 'Free plan may train unless you opt out' }],
    ['cohere', 'Cohere', 'https://api.cohere.ai/compatibility/v1', 'COHERE_API_KEY', 'https://dashboard.cohere.com/api-keys', { cat: 'cohere', limits: 'Trial key: about 1,000 calls/month' }],
    ['cloudflare', 'Cloudflare Workers AI', 'https://api.cloudflare.com/client/v4/accounts/${CLOUDFLARE_ACCOUNT_ID}/ai/v1', 'CLOUDFLARE_API_TOKEN', 'https://dash.cloudflare.com/profile/api-tokens', { cat: 'cloudflare', requires: ['CLOUDFLARE_ACCOUNT_ID'], shared_quota: true, limits: '10,000 Neurons/day free' }],
    ['zai', 'Z.ai (Zhipu)', 'https://api.z.ai/api/paas/v4', 'ZAI_API_KEY', 'https://z.ai/manage-apikey/apikey-list', { cat: 'zhipu', limits: 'Free GLM Flash models' }],
    ['ollama', 'Ollama Cloud', 'https://ollama.com/v1', 'OLLAMA_API_KEY', 'https://ollama.com/settings/keys', { cat: 'ollama', wait_seconds: 120, limits: 'Free cloud tier with hourly/daily usage caps' }],
    ['huggingface', 'Hugging Face Router', 'https://router.huggingface.co/v1', 'HF_TOKEN', 'https://huggingface.co/settings/tokens', { discover: true, models: ['openai/gpt-oss-120b', 'openai/gpt-oss-20b'], shared_quota: true, limits: 'About $0.10/month of free router credit' }],
    ['electronhub', 'ElectronHub', 'https://api.electronhub.ai/v1', 'ELECTRONHUB_API_KEY', 'https://playground.electronhub.ai', { cat: 'electronhub', shared_quota: true, limits: 'Shared weekly free credit pool' }],
    ['xkiro', 'xKiro', 'https://api.xkiro.com/v1', 'XKIRO_API_KEY', 'https://xkiro.com', { cat: 'xkiro', limits: 'Free plan: 5M tokens/day on free models' }],
    ['aclide', 'ACLIDE', 'https://aclide.com/v1', 'ACLIDE_API_KEY', 'https://aclide.com', { cat: 'aclide' }],
    ['airforce', 'Api.Airforce', 'https://api.airforce/v1', 'AIRFORCE_API_KEY', 'https://api.airforce', { cat: 'airforce' }],
    ['dreamprompting', 'DreamPrompting', 'https://dreamprompting.com/api/v1', 'DREAMPROMPTING_API_KEY', 'https://dreamprompting.com', { cat: 'dreamprompting' }],
    ['waterfall', 'Waterfall', 'https://api.getwaterfall.org/v1', 'WATERFALL_API_KEY', 'https://getwaterfall.org', { cat: 'waterfall' }],
    ['nara', 'NaraRouter', 'https://router.bynara.id/v1', 'NARA_API_KEY', 'https://bynara.id', { cat: 'nara', limits: 'Free plan (needs Telegram verification)' }],
    ['routeway', 'Routeway', 'https://api.routeway.ai/v1', 'ROUTEWAY_API_KEY', 'https://routeway.ai', { cat: 'routeway', shared_quota: true, limits: 'Free pool about 5–20 requests/min, 200/day' }],
    ['requesty', 'Requesty', 'https://router.requesty.ai/v1', 'REQUESTY_API_KEY', 'https://app.requesty.ai', { cat: 'requesty', shared_quota: true, limits: '200 requests/day shared across free models' }],
    ['agnes', 'Agnes AI', 'https://apihub.agnes-ai.com/v1', 'AGNES_API_KEY', 'https://agnes-ai.com', { cat: 'agnes', limits: 'Promotional $0 models' }],
    ['sealion', 'SEA-LION (AI Singapore)', 'https://api.sea-lion.ai/v1', 'SEALION_API_KEY', 'https://playground.sea-lion.ai', { cat: 'sealion', limits: '10 requests/min free' }],
    ['router9', 'Router9', 'https://api.router9.com/v1', 'ROUTER9_API_KEY', 'https://router9.com', { cat: 'router9' }],
    ['septor', 'Septor Labs', 'https://api.septorlabs.com/v1', 'SEPTOR_API_KEY', 'https://septorlabs.com', { cat: 'septor' }],
    ['aion', 'Aion Labs', 'https://api.aionlabs.ai/v1', 'AION_API_KEY', 'https://aionlabs.ai', { cat: 'aion' }],
    ['clod', 'CLōD', 'https://api.clod.io/v1', 'CLOD_API_KEY', 'https://clod.io', { cat: 'clod' }],
    ['speka', 'Speka', 'https://speka.me/v1', 'SPEKA_API_KEY', 'https://speka.me', { cat: 'speka' }],
    ['bazaarlink', 'BazaarLink', 'https://bazaarlink.ai/api/v1', 'BAZAARLINK_API_KEY', 'https://bazaarlink.ai', { cat: 'bazaarlink' }],
    ['unorouter', 'UnoRouter', 'https://api.unorouter.com/v1', 'UNOROUTER_API_KEY', 'https://unorouter.com', { cat: 'unorouter' }],
    ['radeon', 'AMD Radeon Cloud', 'https://developer.amd.com.cn/radeon/api/v1', 'RADEON_API_KEY', 'https://developer.amd.com.cn', { cat: 'radeon' }],
    ['logfare', 'Logfare', 'https://logfare.ai/v1', 'LOGFARE_API_KEY', 'https://logfare.ai', { cat: 'logfare' }],
    ['modelscope', 'ModelScope', 'https://api-inference.modelscope.cn/v1', 'MODELSCOPE_API_KEY', 'https://modelscope.cn/my/myaccesstoken', { cat: 'modelscope', limits: 'Needs an Alibaba Cloud China account with real-name verification' }],
    ['anyapi', 'AnyAPI', 'https://api.anyapi.ai/v1', 'ANYAPI_API_KEY', 'https://anyapi.ai', { discover: true, models: [], limits: '100K tokens/day on free models' }],
    ['ainative', 'AINative Studio', 'https://api.ainative.studio/api/v1', 'AINATIVE_API_KEY', 'https://ainative.studio', { discover: true, models: [], limits: 'Advertised ~10M tokens/month free' }],
    ['orcarouter', 'OrcaRouter', 'https://api.orcarouter.ai/v1', 'ORCAROUTER_API_KEY', 'https://orcarouter.ai', { discover: true, discover_contains: 'free', models: [] }],
    ['navy', 'NavyAI', 'https://api.navy/v1', 'NAVY_API_KEY', 'https://api.navy', { discover: true, models: [], limits: '150K tokens/day, 20 requests/min' }],
    ['longcat', 'LongCat (Meituan)', 'https://api.longcat.chat/openai/v1', 'LONGCAT_API_KEY', 'https://longcat.chat/platform', { models: ['LongCat-Flash-Chat', 'LongCat-Flash-Thinking'], limits: 'Daily free quota' }],
    ['experiential', 'Experiential Labs', 'https://api.experientiallabs.ai/v1', 'EXPERIENTIAL_API_KEY', 'https://experientiallabs.ai', { discover: true, models: [], shared_quota: true, limits: 'Shared monthly credit pool' }],
    ['kenari', 'Kenari', 'https://kenari.id/v1', 'KENARI_API_KEY', 'https://kenari.id', { discover: true, discover_contains: 'free', models: [] }],
    ['siliconflow', 'SiliconFlow', 'https://api.siliconflow.com/v1', 'SILICONFLOW_API_KEY', 'https://cloud.siliconflow.com/account/ak', { models: ['Qwen/Qwen2.5-7B-Instruct', 'THUDM/GLM-4-9B-0414'], limits: 'A few small models are free' }],
    ['qianfan', 'Baidu Qianfan', 'https://qianfan.baidubce.com/v2', 'QIANFAN_API_KEY', 'https://console.bce.baidu.com/qianfan', { models: ['ernie-speed-128k', 'ernie-lite-8k'], limits: 'Free ERNIE Speed/Lite; Chinese real-name verification' }],
    ['volcengine', 'Volcengine Ark', 'https://ark.cn-beijing.volces.com/api/v3', 'VOLCENGINE_API_KEY', 'https://console.volcengine.com/ark', { discover: true, models: [], limits: 'Daily free quota per model; Chinese real-name verification' }],
    ['xfyun', 'iFlytek Spark', 'https://spark-api-open.xf-yun.com/v1', 'XFYUN_API_KEY', 'https://console.xfyun.cn', { models: ['lite'], limits: 'Spark Lite is free' }],
    ['bai', 'B.AI', 'https://api.b.ai/v1', 'BAI_API_KEY', 'https://b.ai', { discover: true, models: [] }],
    ['llm7', 'LLM7', 'https://api.llm7.io/v1', 'LLM7_API_KEY', 'https://token.llm7.io', { keyless: true, cat: 'llm7', parallel: 1, limits: 'No key needed (1 request at a time); a free token raises the limits' }],
    ['pollinations', 'Pollinations', 'https://text.pollinations.ai/openai', 'POLLINATIONS_API_KEY', 'https://auth.pollinations.ai', { keyless: true, models: ['openai-fast'], labels: { 'openai-fast': 'Pollinations Fast' }, parallel: 1, limits: 'No key needed; anonymous tier is rate-limited' }],
    ['freeai', null],
    ['opencode-paid', null],
    ['local', null],
  ];

  const bad = /nemotron-3-super-120b-a12b:free|guard|safety|embed|whisper|tts|transcri|rerank|moderation|image|lyria|veo|video|ocr|clip|-fim|fim-|speech|audio/i;
  const out = [];
  let total = 0;
  for (const row of PLATFORMS) {
    const [id, name, base, key, signup, extra = {}] = row;
    if (name === null) { if (keep.has(id)) out.push(keep.get(id)); continue; }
    const p = { id, name, kind: 'openai', base_url: base, key };
    if (extra.keyless) p.keyless = true;
    if (extra.requires) p.requires = extra.requires;
    const prev = keep.get(id);
    let models = extra.models ? [...extra.models] : [];
    const vision = new Set(extra.vision || []);
    if (extra.cat) {
      const rows = catalog.models.filter((m) => m.platform === extra.cat && m.enabled !== false && !bad.test(m.modelId));
      rows.sort((a, b) => (a.speedRank ?? 999) - (b.speedRank ?? 999));
      for (const m of rows) { if (!models.includes(m.modelId)) models.push(m.modelId); if (m.supportsVision) vision.add(m.modelId); }
      if (!extra.limits) {
        const l = rows[0]?.limits;
        if (l && (l.rpm || l.rpd)) p.limits = [l.rpm ? `${l.rpm} requests/min` : '', l.rpd ? `${l.rpd} requests/day` : ''].filter(Boolean).join(', ') + ' (free tier)';
      }
    }
    if (prev && Array.isArray(prev.models)) for (const m of prev.models) if (!models.includes(m) && !bad.test(m)) models.push(m);
    models = models.slice(0, 25);
    if (extra.discover) p.discover = true;
    if (extra.discover_suffix) p.discover_suffix = extra.discover_suffix;
    if (extra.discover_contains) p.discover_contains = extra.discover_contains;
    p.models = models;
    if (extra.labels) p.labels = extra.labels;
    const vis = models.filter((m) => vision.has(m));
    if (vis.length) p.vision_models = vis;
    p.auto_models = extra.keyless ? 3 : 2;
    if (extra.shared_quota) p.shared_quota = true;
    if (extra.parallel) p.parallel = extra.parallel;
    if (extra.trains) p.trains = true;
    if (extra.wait_seconds) p.wait_seconds = extra.wait_seconds;
    p.signup = signup;
    p.limits = extra.limits || p.limits || 'Free tier (limits set by the provider)';
    p.privacy = extra.privacy || 'Check the provider\u2019s terms before sending private data';
    total += models.length;
    out.push(p);
  }
  // NVIDIA vision models (hand-verified list)
  const nv = out.find((p) => p.id === 'nvidia');
  if (nv) nv.vision_models = (nv.models || []).filter((m) => /vision|-vl|omni/i.test(m));
  const doc = {
    _comment: 'Astro AI brain: every free model platform. No secrets here. Keyless platforms work at once; the others turn on when their key exists (Settings \u2192 Providers, a Supabase secret, or an env var with the name in "key"). Order = Auto order. Generated by tools/build_providers.js from the public FreeLLMAPI catalog ' + (catalog.version || '') + ' plus hand-verified entries.',
    providers: out,
  };
  fs.writeFileSync(path.join(root, 'providers.json'), JSON.stringify(doc, null, 2) + '\n');
  console.log(`providers.json: ${out.length} platforms, ${total} catalog models (+ hand-verified NVIDIA/OpenCode/Free.ai/Local)`);
}
main().catch((e) => { console.error(e); process.exit(1); });
