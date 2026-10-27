// Astro AI gateway (Supabase Edge Function), v10.
//
// 1) /credentials hands your free provider keys to your Astro AI server (memory only).
// 2) /store keeps a sync copy of your chats, notes and tasks in table public.astro_store.
// 3) /chat/completions relays OpenAI-compatible chat requests from the Astro AI C++ server to a hosted model
// (default: DeepSeek V4.1 Flash on NVIDIA's free developer endpoint). The model runs on the
// provider's GPUs; this function only relays bytes, so it stays within Edge Function limits.
//
// Secrets (Supabase dashboard -> Edge Functions -> Secrets):
//   NVIDIA_API_KEY         required  your nvapi-... key from build.nvidia.com (never sent to browsers)
//   WORKSPACE_PROXY_TOKEN  required  shared secret that only your Astro AI server knows
//   UPSTREAM_BASE_URL      optional  default https://integrate.api.nvidia.com/v1
//   UPSTREAM_MODEL         optional  default deepseek-ai/deepseek-v4.1-flash
//   OPENCODE_API_KEY       optional  OpenCode Console key, handed to the local OpenCode layer at startup
//   ATRIA_API_KEY, GROQ_API_KEY, GEMINI_API_KEY, CEREBRAS_API_KEY, OPENROUTER_API_KEY, MISTRAL_API_KEY, …
//                          optional  free-tier provider keys for the Astro AI brain (every key name in providers.json)
//   UPSTREAM_EXTRA_BODY    optional  JSON object merged into each request, e.g. provider-specific
//                                    switches such as {"chat_template_kwargs":{"thinking":false}}

const DEFAULT_BASE_URL = "https://integrate.api.nvidia.com/v1";
// Provider keys the Astro AI server may receive (memory only) through /credentials.
// Every key name from providers.json (Atria, Groq, Gemini, …) plus any other *_API_KEY / *_TOKEN /
// *_ACCOUNT_ID secret you add later (Supabase's own secrets are never handed out).
const PROVIDER_SECRETS = [
  "ACLIDE_API_KEY", "AGNES_API_KEY", "AINATIVE_API_KEY", "AION_API_KEY", "AIRFORCE_API_KEY",
  "ANYAPI_API_KEY", "ATRIA_API_KEY", "BAI_API_KEY", "BAZAARLINK_API_KEY", "BRAVE_API_KEY",
  "CEREBRAS_API_KEY", "CLOD_API_KEY", "CLOUDFLARE_ACCOUNT_ID", "CLOUDFLARE_API_TOKEN",
  "COHERE_API_KEY", "DREAMPROMPTING_API_KEY", "ELECTRONHUB_API_KEY", "EXPERIENTIAL_API_KEY",
  "FREEAI_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "HF_TOKEN", "KENARI_API_KEY", "KILO_API_KEY",
  "LLM7_API_KEY", "LOGFARE_API_KEY", "LONGCAT_API_KEY", "MISTRAL_API_KEY", "MODELSCOPE_API_KEY",
  "NARA_API_KEY", "NAVY_API_KEY", "NVIDIA_API_KEY", "OLLAMA_API_KEY", "OPENCODE_API_KEY",
  "OPENROUTER_API_KEY", "ORCAROUTER_API_KEY", "OVH_API_KEY", "POLLINATIONS_API_KEY", "QIANFAN_API_KEY",
  "RADEON_API_KEY", "REQUESTY_API_KEY", "ROUTER9_API_KEY", "ROUTEWAY_API_KEY", "SEALION_API_KEY",
  "SEPTOR_API_KEY", "SILICONFLOW_API_KEY", "SPEKA_API_KEY", "TAVILY_API_KEY", "UNOROUTER_API_KEY",
  "VOLCENGINE_API_KEY", "WATERFALL_API_KEY", "XFYUN_API_KEY", "XKIRO_API_KEY", "ZAI_API_KEY",
];
const NEVER_SHARE = /^(SUPABASE_|SB_|WORKSPACE_PROXY_TOKEN$|DENO_)/;
function providerSecretNames(): string[] {
  const names = new Set(PROVIDER_SECRETS);
  try {
    for (const name of Object.keys(Deno.env.toObject())) {
      if (/^[A-Z0-9_]+_(API_KEY|TOKEN|ACCOUNT_ID)$/.test(name) && !NEVER_SHARE.test(name)) names.add(name);
    }
  } catch { /* env listing not permitted: use the fixed list */ }
  return [...names];
}
const DEFAULT_MODEL = "deepseek-ai/deepseek-v4.1-flash";
const MAX_BODY_BYTES = 256_000;
const MAX_STORE_BYTES = 12_000_000;
const MAX_MESSAGES = 40;
const MAX_OUTPUT_TOKENS = 4096;

function json(status: number, payload: unknown): Response {
  return new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" },
  });
}

function timingSafeEqual(a: string, b: string): boolean {
  const encoder = new TextEncoder();
  const left = encoder.encode(a);
  const right = encoder.encode(b);
  let diff = left.length ^ right.length;
  const length = Math.max(left.length, right.length);
  for (let i = 0; i < length; i++) diff |= (left[i] ?? 0) ^ (right[i] ?? 0);
  return diff === 0;
}

Deno.serve(async (request: Request): Promise<Response> => {
  const url = new URL(request.url);
  const apiKey = Deno.env.get("NVIDIA_API_KEY") ?? "";
  const proxyToken = Deno.env.get("WORKSPACE_PROXY_TOKEN") ?? "";
  const baseUrl = (Deno.env.get("UPSTREAM_BASE_URL") || DEFAULT_BASE_URL).replace(/\/+$/, "");
  const model = Deno.env.get("UPSTREAM_MODEL") || DEFAULT_MODEL;

  // Health check reveals only whether setup is complete — never secret values.
  if (request.method === "GET" && url.pathname.endsWith("/health")) {
    const providers: Record<string, boolean> = {};
    for (const name of providerSecretNames()) providers[name] = (Deno.env.get(name) ?? "").length > 0;
    return json(200, { ok: true, model, upstream_key_configured: apiKey.length > 0, proxy_token_configured: proxyToken.length > 0, providers });
  }

  // Credential hand-off for the trusted Astro AI server. Supabase free-plan functions are cut off
  // after 150 s, but NVIDIA's free queue can be slower, so the server calls the provider directly
  // and keeps the key in memory only. Requires the same proxy token as chat requests.
  if (request.method === "POST" && url.pathname.endsWith("/credentials")) {
    if (!proxyToken) return json(503, { error: { message: "WORKSPACE_PROXY_TOKEN secret is missing." } });
    const credAuth = request.headers.get("Authorization") ?? "";
    const credToken = credAuth.startsWith("Bearer ") ? credAuth.slice(7).trim() : "";
    if (!credToken || !timingSafeEqual(credToken, proxyToken)) return json(401, { error: { message: "Unauthorized." } });
    const keys: Record<string, string> = {};
    for (const name of providerSecretNames()) {
      const value = Deno.env.get(name) ?? "";
      if (value) keys[name] = value;
    }
    return json(200, { base_url: baseUrl, model, api_key: apiKey, opencode_api_key: keys.OPENCODE_API_KEY ?? "", keys });
  }

  // Sync store for the trusted Astro AI server: one row (id "main") in public.astro_store.
  // The table has row-level security with no policies, so only the service role used here can touch it.
  if (url.pathname.endsWith("/store") && (request.method === "GET" || request.method === "POST")) {
    if (!proxyToken) return json(503, { error: { message: "WORKSPACE_PROXY_TOKEN secret is missing." } });
    const storeAuth = request.headers.get("Authorization") ?? "";
    const storeToken = storeAuth.startsWith("Bearer ") ? storeAuth.slice(7).trim() : "";
    if (!storeToken || !timingSafeEqual(storeToken, proxyToken)) return json(401, { error: { message: "Unauthorized." } });
    const supabaseUrl = (Deno.env.get("SUPABASE_URL") ?? "").replace(/\/+$/, "");
    const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? "";
    if (!supabaseUrl || !serviceKey) return json(503, { error: { message: "Supabase service credentials are unavailable." } });
    const rest = `${supabaseUrl}/rest/v1/astro_store`;
    const headers = { apikey: serviceKey, Authorization: `Bearer ${serviceKey}`, "Content-Type": "application/json" };
    if (request.method === "GET") {
      const r = await fetch(`${rest}?id=eq.main&select=updated,data`, { headers });
      if (!r.ok) return json(502, { error: { message: `Store read failed (HTTP ${r.status}).` } });
      const rows = await r.json();
      return json(200, Array.isArray(rows) && rows[0] ? { updated: Number(rows[0].updated) || 0, data: rows[0].data } : { updated: 0 });
    }
    const raw = await request.text();
    if (raw.length > MAX_STORE_BYTES) return json(413, { error: { message: "Sync data is too large." } });
    let doc: { updated?: unknown; data?: unknown };
    try { doc = JSON.parse(raw); } catch { return json(400, { error: { message: "Invalid JSON." } }); }
    if (typeof doc.updated !== "number" || !doc.data || typeof doc.data !== "object") return json(400, { error: { message: "Send {updated, data}." } });
    const r = await fetch(`${rest}?on_conflict=id`, {
      method: "POST",
      headers: { ...headers, Prefer: "resolution=merge-duplicates,return=minimal" },
      body: JSON.stringify({ id: "main", updated: Math.floor(doc.updated), data: doc.data, updated_at: new Date().toISOString() }),
    });
    if (!r.ok) return json(502, { error: { message: `Store write failed (HTTP ${r.status}).` } });
    return json(200, { ok: true, updated: doc.updated });
  }

  if (request.method !== "POST" || !url.pathname.endsWith("/chat/completions")) {
    return json(404, { error: { message: "Not found. POST to /chat/completions." } });
  }
  if (!proxyToken) return json(503, { error: { message: "Gateway is not configured: WORKSPACE_PROXY_TOKEN secret is missing." } });

  const auth = request.headers.get("Authorization") ?? "";
  const presented = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
  if (!presented || !timingSafeEqual(presented, proxyToken)) {
    return json(401, { error: { message: "Unauthorized." } });
  }
  if (!apiKey) {
    return json(503, { error: { message: "Gateway is not configured: add your NVIDIA_API_KEY secret in the Supabase dashboard (Edge Functions → Secrets)." } });
  }

  const raw = await request.text();
  if (raw.length > MAX_BODY_BYTES) return json(413, { error: { message: "Request is too large." } });
  let body: Record<string, unknown>;
  try {
    body = JSON.parse(raw);
  } catch {
    return json(400, { error: { message: "Invalid JSON." } });
  }
  if (!Array.isArray(body.messages) || body.messages.length === 0) {
    return json(400, { error: { message: "messages must be a non-empty array." } });
  }

  // Only forward known fields; the model is pinned server-side.
  const stream = body.stream === true;
  const requestedMax = typeof body.max_tokens === "number" ? body.max_tokens : 1024;
  const upstreamBody: Record<string, unknown> = {
    model,
    messages: body.messages.slice(-MAX_MESSAGES),
    temperature: typeof body.temperature === "number" ? Math.min(Math.max(body.temperature, 0), 1.5) : 0.6,
    max_tokens: Math.min(Math.max(Math.floor(requestedMax), 16), MAX_OUTPUT_TOKENS),
    stream,
  };
  const extra = Deno.env.get("UPSTREAM_EXTRA_BODY");
  if (extra) {
    try {
      const parsed = JSON.parse(extra);
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
        for (const [key, value] of Object.entries(parsed)) {
          if (key !== "model" && key !== "messages") upstreamBody[key] = value;
        }
      }
    } catch { /* ignore malformed optional setting */ }
  }

  let upstream: Response;
  try {
    upstream = await fetch(`${baseUrl}/chat/completions`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Accept: stream ? "text/event-stream" : "application/json",
        Authorization: `Bearer ${apiKey}`,
      },
      body: JSON.stringify(upstreamBody),
      signal: request.signal, // client pressed Stop -> cancel upstream generation
    });
  } catch (_error) {
    return json(502, { error: { message: "Could not reach the model provider." } });
  }

  if (!upstream.ok) {
    let message = `Model provider returned HTTP ${upstream.status}.`;
    try {
      const detail = await upstream.json();
      const text = detail?.error?.message ?? detail?.detail ?? detail?.message;
      if (typeof text === "string" && text) message = text.slice(0, 400);
    } catch { /* non-JSON error */ }
    if (upstream.status === 429) message = "The free model quota is busy or rate-limited (about 40 requests/minute). Wait a moment and try again.";
    return json(upstream.status === 401 || upstream.status === 403 ? 502 : upstream.status, { error: { message } });
  }

  // Pass the provider's body straight through (SSE stream or JSON).
  return new Response(upstream.body, {
    status: 200,
    headers: {
      "Content-Type": upstream.headers.get("Content-Type") ?? (stream ? "text/event-stream" : "application/json"),
      "Cache-Control": "no-store",
      "X-Accel-Buffering": "no",
    },
  });
});
