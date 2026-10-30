-- Opt-in reports from the A.S.T.R.O AI app and website: anonymous error/crash reports (off by default in the app),
-- "Send feedback", beta-tester signups and the Pro waitlist. Insert-only for the public key: nobody can read rows back
-- without the service role. No IP addresses are stored. A global flood guard rejects inserts over 600 per hour.
create table if not exists public.astro_reports (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  kind text not null check (kind in ('error','crash','feedback','beta','waitlist')),
  category text not null default '' check (char_length(category) <= 40),
  message text not null default '' check (char_length(message) <= 4000),
  detail text not null default '' check (char_length(detail) <= 8000),
  contact text not null default '' check (char_length(contact) <= 200),
  platform text not null default '' check (char_length(platform) <= 80),
  app_version text not null default '' check (char_length(app_version) <= 20),
  lang text not null default '' check (char_length(lang) <= 40),
  source text not null default '' check (source in ('', 'app', 'desktop', 'website', 'android', 'cli'))
);
alter table public.astro_reports enable row level security;
revoke all on public.astro_reports from anon, authenticated;
grant insert (kind, category, message, detail, contact, platform, app_version, lang, source) on public.astro_reports to anon, authenticated;
drop policy if exists "astro_reports_insert_only" on public.astro_reports;
create policy "astro_reports_insert_only" on public.astro_reports for insert to anon, authenticated with check (true);
create index if not exists astro_reports_created_idx on public.astro_reports (created_at);

create or replace function public.astro_reports_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if (select count(*) from public.astro_reports where created_at > now() - interval '1 hour') >= 600 then
    raise exception 'Too many reports right now, please try again later.' using errcode = 'P0001';
  end if;
  return new;
end $$;
revoke all on function public.astro_reports_guard() from public, anon, authenticated;
drop trigger if exists astro_reports_guard on public.astro_reports;
create trigger astro_reports_guard before insert on public.astro_reports for each row execute function public.astro_reports_guard();
comment on table public.astro_reports is 'Opt-in error reports, feedback, beta signups and Pro waitlist from A.S.T.R.O AI. Insert-only for the public key.';
