/* A.S.T.R.O AI Library, schema 3 reader (shared by the desktop app, the website Console and the indexer).
   The community catalog is too big to download at once, so schema 3 splits it into small gzip files:
     manifest.json            counts, kind ranges, sources (plain JSON)
     list/<kind>-<n>.json.gz  rows in rank order, SHARD rows per file
     detail/<hhh>.json.gz     install details keyed by id (hhh = first 3 hex of sha256(id))
     q/<xx>.json.gz           search index: token -> row numbers (xx = first 2 characters of the token)
   Row numbers are global: kind k occupies [kinds[k].start, kinds[k].start + kinds[k].count).
   This file must stay dependency-free; the indexer (library/import/index_all.js) requires it so that the tokenizer
   used to build the index is exactly the one used to search it. */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.AstroLib3 = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  const KINDS = ['mcp', 'skill', 'plugin', 'tool'];
  const STOP = new Set(('a an and are as at be by can for from has have how in into is it its of on or our that the their this to use used uses using via was what when which while will with you your all any but not new also more one only other out so than then these those they them get set make just like such'.split(' ')));
  const MAX_TOKEN = 24;

  // Lowercase ASCII-ish tokens: accents are folded, anything else splits. Tokens shorter than 2 are dropped.
  function tokenize(text) {
    const s = String(text || '').normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
    const out = [];
    for (const t of s.split(/[^a-z0-9]+/)) if (t.length >= 2 && !STOP.has(t)) out.push(t.length > MAX_TOKEN ? t.slice(0, MAX_TOKEN) : t);
    return out;
  }
  // Index file of a token: its first two characters.
  const qFile = (token) => 'q/' + token.slice(0, 2) + '.json.gz';

  // Posting lists: ascending row numbers as base-36 gaps joined by commas ("5,1,a" = 5, 6, 16).
  function encodePostings(sorted) {
    let prev = -1; const parts = new Array(sorted.length);
    for (let i = 0; i < sorted.length; i++) { parts[i] = (sorted[i] - prev - 1).toString(36); prev = sorted[i]; }
    return parts.join(',');
  }
  function decodePostings(s) {
    if (!s) return [];
    const parts = s.split(','), out = new Array(parts.length);
    let prev = -1;
    for (let i = 0; i < parts.length; i++) { prev = prev + 1 + parseInt(parts[i], 36); out[i] = prev; }
    return out;
  }
  function intersect(a, b) {
    const out = []; let i = 0, j = 0;
    while (i < a.length && j < b.length) { if (a[i] === b[j]) { out.push(a[i]); i++; j++; } else if (a[i] < b[j]) i++; else j++; }
    return out;
  }
  function union(lists) {
    if (lists.length === 1) return lists[0];
    const set = new Set(); for (const l of lists) for (const x of l) set.add(x);
    return Array.from(set).sort((x, y) => x - y);
  }
  // Same order the indexer uses inside a kind: reviewed, not deprecated, installable, more stars.
  function rankCmp(a, b) {
    const fa = a[4] || '', fb = b[4] || '';
    return (fb.includes('r') - fa.includes('r')) || (fa.includes('x') - fb.includes('x')) || (fa.includes('l') - fb.includes('l')) || ((b[7] || 0) - (a[7] || 0));
  }
  // Words of a row that go into the search index.
  function rowTokens(row) {
    const set = new Set();
    for (const t of tokenize(row[0])) set.add(t);
    for (const t of tokenize(row[1])) set.add(t);
    for (const t of tokenize(row[3])) set.add(t);
    for (const t of tokenize(row[5])) set.add(t);
    for (const t of tokenize(row[2]).slice(0, 24)) set.add(t);
    return set;
  }

  // Reader. fetchFile(path) must resolve to the parsed JSON of a catalog file (gzip already undone) or reject.
  function Reader(fetchFile) {
    this.fetchFile = fetchFile;
    this.man = null; this.shards = new Map(); this.q = new Map(); this.details = new Map();
  }
  Reader.prototype.open = async function () {
    if (!this.man) {
      const m = await this.fetchFile('manifest.json');
      if (!m || m.schema !== 3) throw new Error('not a schema 3 catalog');
      this.man = m;
    }
    return this.man;
  };
  Reader.prototype.count = function (kind) {
    const m = this.man; if (!m) return 0;
    if (kind === 'all') return m.total || 0;
    return (m.kinds[kind] || {}).count || 0;
  };
  Reader.prototype.kindOf = function (n) {
    for (const k of KINDS) { const r = this.man.kinds[k]; if (r && n >= r.start && n < r.start + r.count) return k; }
    return null;
  };
  Reader.prototype.shard = function (kind, i) {
    const key = kind + '-' + i;
    if (!this.shards.has(key)) {
      const p = this.fetchFile('list/' + key + '.json.gz').then((j) => (j && j.rows) || []);
      p.catch(() => this.shards.delete(key));
      this.shards.set(key, p);
    }
    return this.shards.get(key);
  };
  // Rows for global row numbers, in the order given.
  Reader.prototype.rowsAt = async function (nums) {
    const size = this.man.shard;
    const need = new Map();
    for (const n of nums) { const k = this.kindOf(n); if (!k) continue; const local = n - this.man.kinds[k].start; const key = k + '-' + Math.floor(local / size); if (!need.has(key)) need.set(key, [k, Math.floor(local / size)]); }
    const loaded = new Map();
    await Promise.all(Array.from(need.entries()).map(async ([key, [k, i]]) => { loaded.set(key, await this.shard(k, i).catch(() => [])); }));
    const out = [];
    for (const n of nums) {
      const k = this.kindOf(n); if (!k) continue;
      const local = n - this.man.kinds[k].start, i = Math.floor(local / size);
      const row = (loaded.get(k + '-' + i) || [])[local % size];
      if (row) out.push({ kind: k, row: row });
    }
    return out;
  };
  // The first `limit` rows of a kind (or of all kinds, merged by rank), optionally filtered.
  Reader.prototype.browse = async function (kind, limit, filter) {
    const kinds = kind === 'all' ? KINDS : [kind];
    const size = this.man.shard;
    const per = await Promise.all(kinds.map(async (k) => {
      const r = this.man.kinds[k]; const out = [];
      if (!r || !r.count) return out;
      const shards = Math.ceil(r.count / size);
      for (let i = 0; i < shards && out.length < limit; i++) {
        const rows = await this.shard(k, i).catch(() => []);
        for (const row of rows) { if (!filter || filter(row, k)) out.push({ kind: k, row: row }); if (out.length >= limit) break; }
        if (i >= 24 && !out.length) break;   // a filter that matches nothing: stop after 25 files
      }
      return out;
    }));
    const all = [].concat(...per);
    if (kinds.length > 1) all.sort((a, b) => rankCmp(a.row, b.row));
    return all.slice(0, limit);
  };
  Reader.prototype.postings = async function (token) {
    const f = qFile(token);
    const have = this.man && this.man.search && this.man.search.prefixes;
    if (have && !(this.prefixSet || (this.prefixSet = new Set(have))).has(token.slice(0, 2))) return [];   // no word starts like this
    if (!this.q.has(f)) { const p = this.fetchFile(f).catch(() => ({})); this.q.set(f, p); }
    const idx = (await this.q.get(f)) || {};
    const lists = [];
    if (idx[token]) lists.push(decodePostings(idx[token]));
    let extra = 0;
    for (const t in idx) { if (t !== token && t.startsWith(token)) { lists.push(decodePostings(idx[t])); if (++extra >= 300) break; } }
    return lists.length ? union(lists) : [];
  };
  // Search: every query word must match the start of a word in the item. Returns {total, items}.
  Reader.prototype.search = async function (query, kind, limit, filter) {
    const toks = Array.from(new Set(tokenize(query)));
    if (!toks.length) return { total: 0, items: [] };
    let hits = null;
    for (const t of toks.sort((a, b) => b.length - a.length)) {
      const p = await this.postings(t);
      hits = hits === null ? p : intersect(hits, p);
      if (!hits.length) break;
    }
    hits = hits || [];
    const kinds = kind === 'all' ? KINDS : [kind];
    const out = []; let total = 0;
    for (const k of kinds) {
      const r = this.man.kinds[k]; if (!r) continue;
      const mine = hits.filter((n) => n >= r.start && n < r.start + r.count);
      total += mine.length;
      // load in pages so a filter can skip rows without fetching every shard
      let taken = 0;
      // matches are spread over many list files, so fetch only about as many rows as are still needed
      const page = Math.max(limit, 24);
      for (let i = 0; i < mine.length && taken < limit; i += page) {
        const got = await this.rowsAt(mine.slice(i, i + Math.max(page, limit - taken)));
        for (const g of got) { if (!filter || filter(g.row, g.kind)) { out.push(g); if (++taken >= limit) break; } }
        if (i >= 4000) break;
      }
    }
    if (kinds.length > 1) out.sort((a, b) => rankCmp(a.row, b.row));
    return { total, items: out.slice(0, limit) };
  };
  Reader.prototype.detailFile = function (hex3) { return 'detail/' + hex3 + '.json.gz'; };

  return { KINDS, STOP, tokenize, qFile, encodePostings, decodePostings, intersect, union, rankCmp, rowTokens, Reader };
});
