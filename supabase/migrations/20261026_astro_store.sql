-- Astro AI sync store: one row per Astro AI server (id 'main').
-- Row-level security is ON with no policies, so the anon and authenticated roles cannot read
-- or write it. Only the Edge Function (service role) behind the proxy token can.
create table if not exists public.astro_store (
  id text primary key,
  updated bigint not null default 0,
  data jsonb not null default '{}'::jsonb,
  updated_at timestamptz not null default now()
);
alter table public.astro_store enable row level security;
revoke all on table public.astro_store from anon, authenticated;
