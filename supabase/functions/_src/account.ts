// Astro AI account API at .../astro-api/account (used by the website's account page). Auth: the user's Supabase session token.
// POST {action, ...}: catalog (public) | list | create_key | revoke_key | save_provider | delete_provider |
//                     toggle_provider | test_provider | delete_account
async function currentUser(req: Request) {
  const token = (req.headers.get('authorization') ?? '').replace(/^Bearer\s+/i, '');
  if (!token || token.split('.').length !== 3) return null;
  const r = await fetch(`${SB_URL}/auth/v1/user`, { headers: { apikey: SERVICE, Authorization: `Bearer ${token}` } });
  if (!r.ok) return null;
  const u = await r.json();
  return u?.id ? u as { id: string; email?: string } : null;
}
const publicCatalog = () => CATALOG.map((p) => ({ id: p.id, name: p.name, key: p.keyName, requires: p.requires ?? [], paid: Boolean(p.paid), signup: p.signup ?? '' }));
function randomKey() {
  const b = crypto.getRandomValues(new Uint8Array(32));
  return 'astro_sk_' + btoa(String.fromCharCode(...b)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

async function handleAccount(req: Request): Promise<Response> {
  if (req.method !== 'POST') return json(405, { error: 'Use POST.' });
  let body: any = {};
  try { body = await req.json(); } catch { return json(400, { error: 'The body must be JSON.' }); }
  const action = String(body.action ?? '');
  if (action === 'catalog') return json(200, { providers: publicCatalog() }, { 'cache-control': 'public, max-age=3600' });

  const user = await currentUser(req);
  if (!user) return json(401, { error: 'Please sign in again.' });
  const uid = user.id;

  if (action === 'list') {
    const today = new Date().toISOString().slice(0, 10);
    const [k, p, u] = await Promise.all([
      rest(`astro_api_keys?user_id=eq.${uid}&select=id,name,prefix,created_at,last_used_at,revoked_at&order=created_at.desc`),
      rest(`astro_provider_keys?user_id=eq.${uid}&select=provider,last4,extra,enabled,updated_at`),
      rest(`astro_usage?user_id=eq.${uid}&select=day,requests&order=day.desc&limit=30`),
    ]);
    const usage = u.ok ? await u.json() : [];
    return json(200, { user: { id: uid, email: user.email ?? '' }, keys: k.ok ? await k.json() : [], providers: p.ok ? await p.json() : [], usage, today: usage.find((x: any) => x.day === today)?.requests ?? 0, catalog: publicCatalog(), endpoint: `${SB_URL}/functions/v1/astro-api/v1` });
  }
  if (action === 'create_key') {
    const name = String(body.name ?? '').trim().slice(0, 60) || 'My key';
    const active = await rest(`astro_api_keys?user_id=eq.${uid}&revoked_at=is.null&select=id`);
    if (active.ok && (await active.json()).length >= 10) return json(400, { error: 'You already have 10 active keys. Revoke one first.' });
    const secret = randomKey();
    const r = await rest('astro_api_keys', { method: 'POST', headers: { Prefer: 'return=representation' }, body: JSON.stringify({ user_id: uid, name, prefix: secret.slice(0, 14), key_hash: await sha256hex(secret) }) });
    if (!r.ok) return json(500, { error: 'Could not create the key.' });
    const [row] = await r.json();
    return json(200, { key: secret, id: row.id, name: row.name, prefix: row.prefix, note: 'Copy it now: it is shown only once.' });
  }
  if (action === 'revoke_key') {
    const id = String(body.id ?? '');
    if (!/^[0-9a-f-]{36}$/.test(id)) return json(400, { error: 'Bad key id.' });
    const r = await rest(`astro_api_keys?id=eq.${id}&user_id=eq.${uid}`, { method: 'PATCH', body: JSON.stringify({ revoked_at: new Date().toISOString() }) });
    return r.ok ? json(200, { ok: true }) : json(500, { error: 'Could not revoke the key.' });
  }
  if (action === 'save_provider') {
    const cat = CATALOG.find((c) => c.id === body.provider);
    if (!cat) return json(400, { error: 'Unknown provider.' });
    const key = String(body.key ?? '').trim();
    if (key.length < 8 || key.length > 600 || /[\s\x00-\x1f]/.test(key)) return json(400, { error: 'That key looks invalid.' });
    const extra: Record<string, string> = {};
    for (const name of cat.requires ?? []) {
      const v = String(body.extra?.[name] ?? '').trim();
      if (!SAFE_EXTRA.test(v)) return json(400, { error: `${name} is required (letters, digits, dot, dash, underscore).` });
      extra[name] = v;
    }
    const enc = await encrypt(key, `${uid}:${cat.id}`);
    const r = await rest('astro_provider_keys', { method: 'POST', headers: { Prefer: 'resolution=merge-duplicates' }, body: JSON.stringify({ user_id: uid, provider: cat.id, ...enc, last4: key.slice(-4), extra, enabled: true, updated_at: new Date().toISOString() }) });
    return r.ok ? json(200, { ok: true, provider: cat.id, last4: key.slice(-4) }) : json(500, { error: 'Could not save the key.' });
  }
  if (action === 'delete_provider' || action === 'toggle_provider' || action === 'test_provider') {
    const pid = String(body.provider ?? '');
    if (!/^[a-z0-9-]{1,40}$/.test(pid)) return json(400, { error: 'Bad provider.' });
    if (action === 'delete_provider') {
      const r = await rest(`astro_provider_keys?user_id=eq.${uid}&provider=eq.${pid}`, { method: 'DELETE' });
      return r.ok ? json(200, { ok: true }) : json(500, { error: 'Could not delete.' });
    }
    if (action === 'toggle_provider') {
      const r = await rest(`astro_provider_keys?user_id=eq.${uid}&provider=eq.${pid}`, { method: 'PATCH', body: JSON.stringify({ enabled: Boolean(body.enabled) }) });
      return r.ok ? json(200, { ok: true }) : json(500, { error: 'Could not update.' });
    }
    const r = await rest(`astro_provider_keys?user_id=eq.${uid}&provider=eq.${pid}&select=ciphertext,iv,extra`);
    const [row] = r.ok ? await r.json() : [];
    const cat = CATALOG.find((c) => c.id === pid);
    if (!row || !cat) return json(404, { error: 'No saved key for that provider.' });
    const p = { ...cat, key: await decrypt(row.ciphertext, row.iv, `${uid}:${pid}`), base: expandBase(cat.base, row.extra ?? {}) };
    const t0 = Date.now();
    const models = await discover({ ...p, discover: true });
    return json(200, { ok: models.length > 0, models: models.length, sample: models.slice(0, 5), ms: Date.now() - t0 });
  }
  if (action === 'delete_account') {
    if (body.confirm !== 'DELETE') return json(400, { error: 'Type DELETE to confirm.' });
    const r = await fetch(`${SB_URL}/auth/v1/admin/users/${uid}`, { method: 'DELETE', headers: { apikey: SERVICE, Authorization: `Bearer ${SERVICE}` } });
    return r.ok ? json(200, { ok: true }) : json(500, { error: 'Could not delete the account.' });
  }
  return json(400, { error: 'Unknown action.' });
}
