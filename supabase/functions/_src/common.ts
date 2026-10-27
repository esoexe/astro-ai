// Provider catalog generated from Astro AI's providers.json: cloud platforms only (no local servers, no OpenCode).
// BYOK: every request uses ONLY the calling user's own saved provider keys. No shared or owner keys are read here.
const CATALOG: Prov[] = /*CATALOG*/[];
type Prov = { id: string; name: string; base: string; keyName: string; requires?: string[]; paid?: boolean; models?: string[]; discover?: boolean; headers?: Record<string, string>; suffix?: string; exclude?: string; contains?: string; max?: number; signup?: string; limits?: string };
const SB_URL = Deno.env.get('SUPABASE_URL') ?? '';
const SERVICE = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '';
const KMS = Deno.env.get('ASTRO_KMS_KEY') ?? '';
const CORS: Record<string, string> = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, content-type, apikey, x-client-info',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Expose-Headers': 'x-astro-provider, x-astro-model',
};
const json = (status: number, body: unknown, extra: Record<string, string> = {}) =>
  new Response(JSON.stringify(body), { status, headers: { ...CORS, 'content-type': 'application/json', ...extra } });
const later = (p: Promise<unknown>) => { try { (globalThis as any).EdgeRuntime?.waitUntil(p.catch(() => {})); } catch { /* ignore */ } };

function rest(path: string, init: RequestInit = {}) {
  return fetch(`${SB_URL}/rest/v1/${path}`, { ...init, headers: { apikey: SERVICE, Authorization: `Bearer ${SERVICE}`, 'content-type': 'application/json', ...(init.headers as Record<string, string> ?? {}) } });
}
async function sha256hex(s: string) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}
const b64e = (u: Uint8Array) => btoa(String.fromCharCode(...u));
const b64d = (s: string) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
let kmsKey: CryptoKey | null = null;
async function kms() {
  if (!kmsKey) {
    const raw = b64d(KMS);
    if (raw.length !== 32) throw new Error('ASTRO_KMS_KEY must be 32 bytes, base64');
    kmsKey = await crypto.subtle.importKey('raw', raw, 'AES-GCM', false, ['encrypt', 'decrypt']);
  }
  return kmsKey;
}
async function encrypt(plain: string, aad: string) {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ct = await crypto.subtle.encrypt({ name: 'AES-GCM', iv, additionalData: new TextEncoder().encode(aad) }, await kms(), new TextEncoder().encode(plain));
  return { ciphertext: b64e(new Uint8Array(ct)), iv: b64e(iv) };
}
async function decrypt(ct: string, iv: string, aad: string) {
  const pt = await crypto.subtle.decrypt({ name: 'AES-GCM', iv: b64d(iv), additionalData: new TextEncoder().encode(aad) }, await kms(), b64d(ct));
  return new TextDecoder().decode(pt);
}
// Extra values (Azure resource, AWS region, Cloudflare account id) are spliced into fixed catalog URLs,
// so they are limited to host-safe characters.
const SAFE_EXTRA = /^[A-Za-z0-9._-]{1,80}$/;
function expandBase(base: string, extra: Record<string, string>) {
  let ok = true;
  const out = base.replace(/\$\{([A-Z0-9_]+)\}/g, (_m, k) => { const v = extra?.[k]; if (!v || !SAFE_EXTRA.test(v)) { ok = false; return ''; } return v; });
  return ok ? out : '';
}
function provHeaders(p: Prov & { key: string }) {
  return { Authorization: `Bearer ${p.key}`, ...(p.headers ?? {}) } as Record<string, string>;
}
async function fetchT(url: string, init: RequestInit, ms: number) {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), ms);
  try { return await fetch(url, { ...init, signal: ctl.signal }); } finally { clearTimeout(t); }
}
const BAD = /embed|whisper|tts|audio|imagen|image-|-image|veo|guard|moderation|rerank|transcribe|-live|aqa|learnlm|computer-use|robotics|lyria|orpheus|playai|babbage|davinci|dall-e|sora|realtime|ocr/i;
async function discover(p: Prov & { key: string; base: string }) {
  let ids: string[] = [];
  if (p.discover !== false) {
    try {
      const r = await fetchT(`${p.base}/models`, { headers: provHeaders(p) }, 7000);
      if (r.ok) {
        const j = await r.json();
        const arr = Array.isArray(j) ? j : (j.data ?? j.models ?? []);
        ids = arr.map((m: any) => (typeof m === 'string' ? m : (m.id ?? m.name ?? ''))).filter(Boolean).map((s: string) => s.replace(/^models\//, ''));
      }
    } catch { /* fall back to the catalog list */ }
  }
  if (!ids.length) ids = [...(p.models ?? [])];
  ids = ids.filter((m) => !BAD.test(m));
  if (p.suffix) ids = ids.filter((m) => m.endsWith(p.suffix!));
  if (p.exclude) ids = ids.filter((m) => !m.endsWith(p.exclude!));
  if (p.contains) ids = ids.filter((m) => m.includes(p.contains!));
  return [...new Set(ids)].slice(0, p.max ?? 200);
}
