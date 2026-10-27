// Astro AI API: one Astro key (astro_sk_...) and one OpenAI-compatible endpoint for every model the user
// has a key for. Endpoints (relative to .../functions/v1/astro-api):
//   GET  /v1/models              -> models as "provider/model" (+ "astro/auto")
//   POST /v1/chat/completions    -> OpenAI Chat Completions; streaming is passed straight through
const DAILY = Number(Deno.env.get('ASTRO_DAILY_LIMIT') ?? '5000');
const oaiErr = (status: number, message: string, type = 'invalid_request_error', code: string | null = null) => json(status, { error: { message, type, code } });

async function authKey(req: Request) {
  const m = (req.headers.get('authorization') ?? '').match(/^Bearer\s+(astro_sk_[A-Za-z0-9_-]{20,100})$/);
  if (!m) return null;
  const hash = await sha256hex(m[1]);
  const r = await rest(`astro_api_keys?key_hash=eq.${hash}&revoked_at=is.null&select=id,user_id`);
  if (!r.ok) return null;
  const rows = await r.json();
  if (!rows.length) return null;
  later(rest(`astro_api_keys?id=eq.${rows[0].id}`, { method: 'PATCH', body: JSON.stringify({ last_used_at: new Date().toISOString() }) }));
  return rows[0] as { id: string; user_id: string };
}
async function userProviders(uid: string) {
  const r = await rest(`astro_provider_keys?user_id=eq.${uid}&enabled=is.true&select=provider,ciphertext,iv,extra`);
  const rows = r.ok ? await r.json() : [];
  const out: (Prov & { key: string; base: string })[] = [];
  for (const row of rows) {
    const cat = CATALOG.find((c) => c.id === row.provider);
    if (!cat) continue;
    try {
      const key = await decrypt(row.ciphertext, row.iv, `${uid}:${row.provider}`);
      const base = expandBase(cat.base, row.extra ?? {});
      if (base) out.push({ ...cat, key, base });
    } catch { /* unreadable row: skip */ }
  }
  out.sort((a, b) => CATALOG.findIndex((c) => c.id === a.id) - CATALOG.findIndex((c) => c.id === b.id));
  return out;
}
async function bump(uid: string) {
  const r = await rest('rpc/astro_bump_usage', { method: 'POST', body: JSON.stringify({ p_user: uid }) });
  return r.ok ? Number(await r.json()) : 0;
}

Deno.serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: CORS });
  const path = new URL(req.url).pathname.replace(/\/+$/, '');
  if (/\/account$/.test(path)) return handleAccount(req);
  if (req.method === 'GET' && !/\/models$/.test(path))
    return json(200, { name: 'Astro AI API', openai_compatible: true, base_url: `${SB_URL}/functions/v1/astro-api/v1`, endpoints: ['GET /v1/models', 'POST /v1/chat/completions'], auth: 'Authorization: Bearer astro_sk_...', docs: 'https://esoexe.github.io/astro-ai/api.html' });
  const key = await authKey(req);
  if (!key) return oaiErr(401, 'Missing or invalid Astro API key. Create one at https://esoexe.github.io/astro-ai/account.html and send it as "Authorization: Bearer astro_sk_...".', 'authentication_error', 'invalid_api_key');
  const provs = await userProviders(key.user_id);

  if (req.method === 'GET') {
    const lists = await Promise.all(provs.map(async (p) => (await discover(p)).map((m) => ({ id: `${p.id}/${m}`, object: 'model', created: 0, owned_by: p.name, provider: p.id, paid: Boolean(p.paid) }))));
    const auto = { id: 'astro/auto', object: 'model', created: 0, owned_by: 'Astro AI', provider: 'astro', paid: false };
    return json(200, { object: 'list', data: [auto, ...lists.flat()] });
  }
  if (req.method !== 'POST' || !/\/chat\/completions$/.test(path)) return oaiErr(404, 'Unknown endpoint. Use GET /v1/models or POST /v1/chat/completions.', 'not_found_error');

  let body: any;
  try { body = await req.json(); } catch { return oaiErr(400, 'The body must be JSON.'); }
  if (!body || !Array.isArray(body.messages)) return oaiErr(400, '"messages" must be an array.');
  if ((await bump(key.user_id)) > DAILY) return oaiErr(429, `Daily Astro API limit reached (${DAILY} requests, resets 00:00 UTC). Your provider limits still apply separately.`, 'rate_limit_error', 'daily_limit');
  if (!provs.length) return oaiErr(400, 'No provider keys saved yet. Add your own keys (Groq, Gemini, OpenAI, ...) at https://esoexe.github.io/astro-ai/account.html.');

  const model = String(body.model ?? 'astro/auto');
  const allowPaid = Boolean(body.astro?.allow_paid);
  let candidates: [Prov & { key: string; base: string }, string][] = [];
  if (model === 'astro/auto' || model === 'auto') {
    for (const p of provs) if ((!p.paid || allowPaid) && p.models?.length) candidates.push([p, p.models[0]]);
    if (!candidates.length) return oaiErr(400, 'astro/auto uses the default model of your saved free providers, and none has one. Name a model as "provider/model" (see GET /v1/models), or pass "astro": {"allow_paid": true}.');
  } else {
    const i = model.indexOf('/');
    if (i < 1) return oaiErr(400, 'Use model ids like "groq/llama-3.3-70b-versatile" (provider/model). GET /v1/models lists yours.', 'invalid_request_error', 'model_not_found');
    const p = provs.find((x) => x.id === model.slice(0, i));
    if (!p) return oaiErr(404, `No saved key for provider "${model.slice(0, i)}". Add it in your Astro AI account.`, 'not_found_error', 'model_not_found');
    candidates = [[p, model.slice(i + 1)]];
  }
  const out = { ...body };
  delete out.astro;
  let last: Response | null = null;
  for (const [p, m] of candidates.slice(0, 4)) {
    out.model = m;
    let r: Response;
    try {
      r = await fetchT(`${p.base}/chat/completions`, { method: 'POST', headers: { ...provHeaders(p), 'content-type': 'application/json' }, body: JSON.stringify(out) }, 140000);
    } catch (e) {
      last = oaiErr(502, `${p.name} could not be reached (${(e as Error).name === 'AbortError' ? 'timeout' : 'network error'}).`, 'api_error');
      continue;
    }
    if (r.ok) {
      const h = new Headers(CORS);
      h.set('content-type', r.headers.get('content-type') ?? 'application/json');
      h.set('cache-control', 'no-store');
      h.set('x-astro-provider', p.id); h.set('x-astro-model', m);
      return new Response(r.body, { status: 200, headers: h });
    }
    const text = (await r.text()).slice(0, 4000);
    last = new Response(text, { status: r.status, headers: { ...CORS, 'content-type': r.headers.get('content-type') ?? 'application/json', 'x-astro-provider': p.id, 'x-astro-model': m } });
    if (!(r.status === 429 || r.status >= 500)) break; // only rate limits and outages fail over
  }
  return last ?? oaiErr(502, 'No provider answered.', 'api_error');
});
