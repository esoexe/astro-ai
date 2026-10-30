-- Pro license plumbing (scaffolding: no licenses are issued or sold yet).
-- Only SHA-256 hashes of keys are stored; the seller webhook (Lemon Squeezy or Paddle, later) writes rows with the
-- service role. Apps call astro_license_check(key) with the public key; the table itself is not readable.
create extension if not exists pgcrypto with schema extensions;
create table if not exists public.astro_licenses (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  key_hash text not null unique check (char_length(key_hash) = 64),
  plan text not null default 'pro' check (plan in ('pro','team')),
  status text not null default 'active' check (status in ('active','cancelled','refunded','expired')),
  expires_at timestamptz,
  seller text not null default '' check (char_length(seller) <= 40),
  seller_ref text not null default '' check (char_length(seller_ref) <= 200)
);
alter table public.astro_licenses enable row level security;
revoke all on public.astro_licenses from anon, authenticated;
create or replace function public.astro_license_check(p_key text) returns jsonb
language plpgsql stable security definer set search_path = public, extensions as $$
declare r public.astro_licenses;
begin
  if p_key is null or char_length(p_key) < 16 or char_length(p_key) > 200 then
    return jsonb_build_object('valid', false, 'reason', 'malformed');
  end if;
  select * into r from public.astro_licenses where key_hash = encode(extensions.digest(p_key, 'sha256'), 'hex');
  if not found then return jsonb_build_object('valid', false, 'reason', 'unknown'); end if;
  if r.status <> 'active' or (r.expires_at is not null and r.expires_at < now()) then
    return jsonb_build_object('valid', false, 'reason', case when r.status <> 'active' then r.status else 'expired' end, 'plan', r.plan);
  end if;
  return jsonb_build_object('valid', true, 'plan', r.plan, 'expires_at', r.expires_at);
end $$;
revoke all on function public.astro_license_check(text) from public;
grant execute on function public.astro_license_check(text) to anon, authenticated;
