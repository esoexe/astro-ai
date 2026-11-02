/* A.S.T.R.O AI - Reverse page (Astro RE). Talks to /api/re/* in the engine; nothing is executed, files are only read.
   app.js mounts it: window.AstroRE.show(container, { api, icon, esc, ask, openTools, toast }). */
(() => {
  'use strict';
  const S = { path: '', name: '', tab: 'overview', ov: null, loading: false, error: '', prov: null, cache: {}, recent: [] };
  try { S.recent = JSON.parse(localStorage.getItem('astro_re:recent') || '[]').slice(0, 8); } catch (_e) { S.recent = []; }
  let C = null; let root = null;
  const fmtBytes = (n) => { n = Number(n) || 0; const u = ['B', 'KB', 'MB', 'GB']; let i = 0; while (n >= 1024 && i < 3) { n /= 1024; i++; } return `${n.toFixed(i ? 1 : 0)} ${u[i]}`; };
  const e = (s) => C.esc(String(s ?? ''));
  const TABS = [['overview', 'Overview'], ['sections', 'Sections'], ['imports', 'Imports'], ['exports', 'Exports'], ['strings', 'Strings'], ['hex', 'Hex'], ['disasm', 'Disassembly'], ['decompile', 'Decompile'], ['archive', 'Archive']];

  async function inspect(path, name) {
    Object.assign(S, { path, name: name || path.split(/[\\/]/).pop(), ov: null, error: '', loading: true, tab: 'overview', cache: {} });
    render();
    try {
      S.ov = await C.api('/api/re/inspect', { path });
      if (!path.startsWith('upload:') || name) { S.recent = [{ path, name: S.name }, ...S.recent.filter((r) => r.path !== path)].slice(0, 8); localStorage.setItem('astro_re:recent', JSON.stringify(S.recent)); }
    } catch (err) { S.error = err.message; }
    S.loading = false; render();
  }
  async function upload(file) {
    const CH = 6 * 1024 * 1024; let id = ''; let path = '';
    S.loading = true; S.error = ''; S.ov = null; S.name = file.name; render();
    try {
      for (let off = 0; off < file.size || off === 0; off += CH) {
        const buf = new Uint8Array(await file.slice(off, off + CH).arrayBuffer());
        let bin = ''; for (let i = 0; i < buf.length; i += 0x8000) bin += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
        const j = await C.api('/api/re/upload', { id, name: file.name, offset: off, data: btoa(bin) });
        id = j.id; path = j.path;
        const pct = Math.min(100, Math.round(((off + buf.length) / Math.max(1, file.size)) * 100));
        const pr = root?.querySelector('.re-progress'); if (pr) pr.textContent = `Uploading… ${pct}%`;
        if (file.size === 0) break;
      }
      await inspect(path, file.name);
    } catch (err) { S.loading = false; S.error = err.message; render(); }
  }
  async function loadTab(tab) {
    if (!S.ov || S.cache[tab] !== undefined) return;
    const p = S.path;
    try {
      if (tab === 'imports') S.cache[tab] = await C.api('/api/re/symbols', { path: p, where: 'import', limit: 3000 });
      else if (tab === 'exports') S.cache[tab] = await C.api('/api/re/symbols', { path: p, where: 'export', limit: 3000 });
      else if (tab === 'strings') S.cache[tab] = await C.api('/api/re/strings', { path: p, interesting: true, limit: 1500 });
      else if (tab === 'hex') S.cache[tab] = await C.api('/api/re/hex', { path: p, at: '0', length: 1024 });
      else if (tab === 'archive') S.cache[tab] = await C.api('/api/re/zip', { path: p });
      else return;
    } catch (err) { S.cache[tab] = { error: err.message }; }
    if (S.tab === tab) render();
  }
  async function providers() { try { S.prov = await C.api('/api/re/providers', {}); } catch (_e) { S.prov = {}; } render(); }

  const kv = (k, v) => (v === undefined || v === null || v === '' ? '' : `<div class="re-kv"><span>${e(k)}</span><b>${e(typeof v === 'object' ? JSON.stringify(v) : v)}</b></div>`);
  const list = (title, arr, cls = '') => (arr && arr.length ? `<div class="re-card ${cls}"><h4>${e(title)}</h4><ul>${arr.map((x) => `<li>${e(typeof x === 'object' ? JSON.stringify(x) : x)}</li>`).join('')}</ul></div>` : '');
  const table = (cols, rows, max = 2000) => `<div class="re-table-wrap"><table class="re-table"><thead><tr>${cols.map((c) => `<th>${e(c[1])}</th>`).join('')}</tr></thead><tbody>${rows.slice(0, max).map((r) => `<tr>${cols.map((c) => `<td${c[2] ? ` class="${c[2]}"` : ''}>${e(r[c[0]] ?? '')}</td>`).join('')}</tr>`).join('')}</tbody></table>${rows.length > max ? `<p class="re-muted">Showing ${max} of ${rows.length}.</p>` : ''}</div>`;

  function head() {
    const p = S.prov || {};
    const chip = (k, label) => `<span class="re-chip ${p[k] ? 'on' : ''}" title="${p[k] ? e(p[k]) : 'Not installed'}">${e(label)}</span>`;
    return `<div class="re-head"><div><h2>${C.icon('i-wrench')} Reverse <small>Astro RE</small></h2><p class="re-muted">Understand how an app, library or file works: format, code, imports, strings, signatures. Astro only reads the file; it never runs it.</p></div>
      <div class="re-prov">${chip('rizin', 'rizin')}${chip('llvm-objdump', 'llvm-objdump')}${chip('objdump', 'objdump')}${chip('ghidra', 'Ghidra')}${chip('jadx', 'JADX')}<button class="btn small ghost" type="button" id="re-tools">${C.icon('i-download')}Get analyzers</button><button class="btn small ghost" type="button" id="re-rea" title="Optional: adds the open-source REA MCP server (morluto/rea, MIT) so the agent can also use its analyzers. Needs Node.js 22.23+.">${C.icon('i-puzzle')}Add REA (MCP)</button></div></div>
      <form class="re-bar" id="re-form"><label class="field-search grow">${C.icon('i-file')}<input id="re-path" class="input" placeholder="Full path to a file, e.g. C:\\Program Files\\App\\app.exe or /usr/bin/ls" value="${e(S.path.startsWith('upload:') ? '' : S.path)}" spellcheck="false"></label>
      <button class="btn primary" type="submit">Inspect</button><button class="btn" type="button" id="re-open">${C.icon('i-upload')}Open a file</button><input type="file" id="re-file" hidden></form>
      ${S.recent.length && !S.ov && !S.loading ? `<div class="re-recent">${S.recent.map((r) => `<button type="button" class="re-chip link" data-recent="${e(r.path)}" data-name="${e(r.name)}">${e(r.name)}</button>`).join('')}</div>` : ''}`;
  }
  function overview(o) {
    const sum = o.symbol_counts || {};
    const mit = o.mitigations ? Object.entries(o.mitigations).map(([k, v]) => `<span class="re-chip ${v === true || v === 'yes' || v === 'full' || String(v).startsWith('yes') ? 'on' : v === false || v === 'no' || v === 'none' || v === 'not seen' ? 'off' : ''}">${e(k.replace(/_/g, ' '))}: ${e(v)}</span>`).join('') : '';
    const skip = new Set(['path', 'bytes', 'sha256', 'format', 'kind', 'arch', 'bits', 'os', 'endian', 'language', 'entry', 'image_base', 'entropy', 'sections', 'segments', 'libraries', 'symbol_counts', 'exports_preview', 'mitigations', 'findings', 'notes', 'limitations', 'unknowns', 'evidence_rule', 'manifest_xml', 'listing', 'schema', 'sha256_note']);
    const extra = Object.entries(o).filter(([k]) => !skip.has(k));
    return `<div class="re-grid"><div class="re-card"><h4>File</h4>${kv('Format', `${o.format}${o.kind ? ` · ${o.kind}` : ''}`)}${kv('Architecture', o.arch && `${o.arch}${o.bits ? ` (${o.bits}-bit, ${o.endian} endian)` : ''}`)}${kv('System', o.os)}${kv('Language / runtime', o.language)}${kv('Size', fmtBytes(o.bytes))}${kv('Entry point', o.entry)}${kv('Image base', o.image_base)}${kv('Entropy', o.entropy)}${kv('SHA-256', o.sha256)}</div>
      ${o.package ? `<div class="re-card"><h4>App</h4>${kv('Package', o.package)}${kv('Version', o.version_name && `${o.version_name} (${o.version_code})`)}${kv('Android', o.min_sdk && `min SDK ${o.min_sdk}, target ${o.target_sdk}`)}${kv('Permissions', (o.permissions || []).length)}${kv('Components', (o.components || []).length)}${kv('Signer (SHA-256)', (o.signers || [])[0]?.cert_sha256 || (o.v1_signature_files ? 'v1 only' : 'unsigned'))}${kv('Scheme', (o.signers || []).map((x) => x.scheme).join(', '))}</div>` : ['ELF', 'PE', 'Mach-O', 'WebAssembly'].includes(o.format) ? `<div class="re-card"><h4>Code</h4>${kv('Symbols', sum.total)}${kv('Imports', sum.imports)}${kv('Exports', sum.exports)}${kv('Defined functions', sum.defined_functions)}${kv('Sections', (o.sections || []).length)}${kv('Libraries', (o.libraries || []).length)}${mit ? `<div class="re-chips">${mit}</div>` : ''}</div>` : ''}</div>
      ${list('Findings', o.findings, 'good')}${list('Libraries it loads', o.libraries)}${list('Notes', o.notes)}
      ${extra.length ? `<div class="re-card"><h4>Details</h4>${extra.map(([k, v]) => (Array.isArray(v) || (v && typeof v === 'object') ? `<details class="re-det"><summary>${e(k.replace(/_/g, ' '))} ${Array.isArray(v) ? `(${v.length})` : ''}</summary><pre>${e(JSON.stringify(v, null, 2).slice(0, 60000))}</pre></details>` : kv(k.replace(/_/g, ' '), v))).join('')}</div>` : ''}
      ${o.manifest_xml ? `<details class="re-card re-det"><summary>AndroidManifest.xml (decoded)</summary><pre>${e(o.manifest_xml)}</pre></details>` : ''}
      ${o.schema ? `<div class="re-card"><h4>Database schema</h4>${table([['type', 'Type'], ['name', 'Name'], ['sql', 'SQL', 'mono']], o.schema)}</div>` : ''}
      ${o.listing && !o.schema ? `<div class="re-card"><h4>Contents</h4>${table([['path', 'Path', 'mono'], ['bytes', 'Bytes']], o.listing)}</div>` : ''}
      ${list('Limitations (what Astro could not read)', o.limitations, 'warn')}${list('Unknowns', o.unknowns, 'warn')}`;
  }

  function tabBody() {
    const o = S.ov; const t = S.tab; const d = S.cache[t];
    if (t === 'overview') return overview(o);
    if (t === 'sections') {
      const rows = [...(o.sections || []), ...(o.segments || []).map((s) => ({ ...s, name: `[segment] ${s.name}` }))];
      return rows.length ? table([['name', 'Name', 'mono'], ['addr', 'Address', 'mono'], ['offset', 'Offset', 'mono'], ['size', 'Size'], ['flags', 'Flags', 'mono'], ['entropy', 'Entropy']], rows) : '<p class="re-muted">This format has no sections.</p>';
    }
    if (t === 'disasm' || t === 'decompile') {
      const res = S.cache[t];
      const ph = t === 'disasm' ? 'Symbol name or address (blank = entry point)' : (o.format === 'ZIP' || o.format === 'DEX' || o.format === 'Java class' ? 'Class name, e.g. com.example.MainActivity (blank = list packages)' : 'Function name or address (blank = entry point)');
      return `<form class="re-bar" id="re-code-form"><input class="input grow mono" id="re-target" placeholder="${e(ph)}" value="${e(S.target || '')}">${t === 'disasm' ? '<input class="input" id="re-count" type="number" min="1" max="400" value="80" style="width:90px" title="Instructions">' : ''}<button class="btn primary" type="submit">${t === 'disasm' ? 'Disassemble' : 'Decompile'}</button></form>
        ${t === 'decompile' ? '<p class="re-muted">Decompiling uses Ghidra (native code) or JADX (Android and Java). The first run on a big file can take several minutes.</p>' : ''}
        ${res === 'loading' ? '<div class="shimmer-bars"><i></i><i></i><i></i></div>' : res?.error ? `<div class="error-card"><div>${e(res.error)}</div></div>` : res ? `<p class="re-muted">${e(res.provider || '')} ${res.target ? `· ${e(res.target)}` : ''} ${res.address ? `· ${e(res.address)}` : ''}</p>${res.limitation ? `<div class="re-card warn">${e(res.limitation)}</div>` : ''}<pre class="re-code">${e(res.listing || res.pseudocode || res.source || res.bytes_hex || (res.packages ? res.packages.join('\n') : ''))}</pre>${res.caveat ? `<p class="re-muted">${e(res.caveat)}</p>` : ''}` : ''}`;
    }
    if (d === undefined) { loadTab(t); return '<div class="shimmer-bars"><i></i><i></i><i></i></div>'; }
    if (d.error) return `<div class="error-card"><div>${e(d.error)}</div></div>`;
    if (t === 'imports' || t === 'exports') return `<p class="re-muted">${d.total} ${t}.</p>${table([['name', 'Name', 'mono'], ['lib', 'Library'], ['addr', 'Address', 'mono'], ['kind', 'Kind']], d.symbols || [])}`;
    if (t === 'strings') return `<form class="re-bar" id="re-str-form"><input class="input grow" id="re-str-q" placeholder="Filter strings (all strings, not only the flagged ones)" value="${e(S.strQ || '')}"><button class="btn" type="submit">Search</button></form><p class="re-muted">${d.total} strings${S.strQ ? ' match' : ' look interesting (URLs, keys, paths, crypto, formats)'}.</p>${table([['offset', 'Offset', 'mono'], ['section', 'Section'], ['tag', 'Tag'], ['text', 'Text', 'mono']], d.strings || [])}`;
    if (t === 'hex') return `<form class="re-bar" id="re-hex-form"><input class="input mono" id="re-hex-at" placeholder="Offset, e.g. 0x400" value="${e(d.offset || '0')}"><button class="btn" type="submit">Jump</button></form><pre class="re-code">${e(d.dump)}</pre>`;
    if (t === 'archive') return `<p class="re-muted">${d.entries} entries. Click one to inspect it.</p><div class="re-table-wrap"><table class="re-table"><thead><tr><th>Entry</th><th>Size</th></tr></thead><tbody>${(d.listing || []).map((z) => `<tr><td class="mono"><a href="#" data-entry="${e(z.name)}">${e(z.name)}</a></td><td>${fmtBytes(z.bytes)}</td></tr>`).join('')}</tbody></table></div>`;
    return '';
  }

  function render() {
    if (!root) return;
    const o = S.ov;
    const native = o && ['ELF', 'PE', 'Mach-O', 'WebAssembly'].includes(o.format);
    const tabs = o ? TABS.filter(([id]) => !['sections', 'imports', 'exports'].includes(id) || native).filter(([id]) => id !== 'archive' || o.format === 'ZIP').filter(([id]) => !['disasm'].includes(id) || ['ELF', 'PE', 'Mach-O'].includes(o.format)).filter(([id]) => id !== 'decompile' || ['ELF', 'PE', 'Mach-O', 'DEX', 'Java class'].includes(o.format) || /APK|JAR|AAB/.test(o.kind || '')) : [];
    root.innerHTML = `<div class="re-wrap">${head()}
      ${S.loading ? `<div class="re-card"><p class="re-progress">Reading ${e(S.name)}…</p><div class="shimmer-bars"><i></i><i></i><i></i></div></div>` : ''}
      ${S.error ? `<div class="error-card"><div><strong>Couldn't inspect that</strong>${e(S.error)}</div></div>` : ''}
      ${o ? `<div class="re-file"><div><b>${e(S.name)}</b><span class="re-muted"> · ${e(o.format)}${o.arch ? ` · ${e(o.arch)}` : ''} · ${fmtBytes(o.bytes)}</span></div><div class="re-actions"><button class="btn small primary" type="button" id="re-ask">${C.icon('i-sparkles')}Ask Astro about it</button><button class="btn small ghost" type="button" id="re-copy">${C.icon('i-copy')}Copy report</button></div></div>
        <div class="re-tabs" role="tablist">${tabs.map(([id, l]) => `<button type="button" role="tab" class="re-tab ${S.tab === id ? 'active' : ''}" data-tab="${id}">${e(l)}</button>`).join('')}</div>
        <div class="re-body">${tabBody()}</div>` : ''}
      ${!o && !S.loading ? `<div class="re-empty">${C.icon('i-wrench')}<p>Open an app (.exe, .dll, .so, .dylib, .apk, .ipa, .jar, .wasm, .asar), a model (.gguf, .safetensors), a database (.db, .sqlite) or any file. Astro shows what it is made of, with evidence. The agent can dig deeper with its “re” tool.</p><p class="re-muted">Use it on software you own or are allowed to study. Astro RE will not help crack licences, bypass DRM or anti-cheat, or build malware.</p></div>` : ''}</div>`;
    bind();
  }

  function bind() {
    const $ = (s) => root.querySelector(s);
    $('#re-form')?.addEventListener('submit', (ev) => { ev.preventDefault(); const p = $('#re-path').value.trim().replace(/^"(.*)"$/, '$1'); if (p) inspect(p); });
    $('#re-open')?.addEventListener('click', () => $('#re-file').click());
    $('#re-file')?.addEventListener('change', (ev) => { const f = ev.target.files[0]; if (f) upload(f); });
    $('#re-tools')?.addEventListener('click', () => C.openTools?.());
    $('#re-rea')?.addEventListener('click', async () => {
      try {
        const r = await C.api('/api/dev/mcp/config_get', {});
        let j = {}; try { j = JSON.parse(r.text) || {}; } catch (_e) { j = {}; }
        j.mcpServers = j.mcpServers || {};
        if (j.mcpServers.rea) { C.toast?.('REA is already in your MCP servers'); return; }
        j.mcpServers.rea = { command: 'npx', args: ['-y', 'rea-agents@6.8.0', 'mcp'] };
        await C.api('/api/dev/mcp/config_set', { text: JSON.stringify(j, null, 2) });
        C.toast?.('REA added. The agent can use its tools next run (first start downloads it with npx).');
      } catch (err) { C.toast?.(err.message, 'error'); }
    });
    root.querySelectorAll('[data-recent]').forEach((b) => b.addEventListener('click', () => inspect(b.dataset.recent, b.dataset.name)));
    root.querySelectorAll('[data-tab]').forEach((b) => b.addEventListener('click', () => { S.tab = b.dataset.tab; render(); }));
    $('#re-code-form')?.addEventListener('submit', async (ev) => {
      ev.preventDefault(); const t = S.tab; S.target = $('#re-target').value.trim();
      const body = { path: S.path, target: S.target }; if (t === 'disasm') body.count = Number($('#re-count').value) || 80;
      S.cache[t] = 'loading'; render();
      try { S.cache[t] = await C.api(`/api/re/${t}`, body); } catch (err) { S.cache[t] = { error: err.message }; }
      if (S.tab === t) render();
    });
    $('#re-str-form')?.addEventListener('submit', async (ev) => {
      ev.preventDefault(); S.strQ = $('#re-str-q').value.trim(); S.cache.strings = undefined; render();
      try { S.cache.strings = await C.api('/api/re/strings', S.strQ ? { path: S.path, filter: S.strQ, limit: 2000, min: 4 } : { path: S.path, interesting: true, limit: 1500 }); } catch (err) { S.cache.strings = { error: err.message }; }
      render();
    });
    $('#re-hex-form')?.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      try { S.cache.hex = await C.api('/api/re/hex', { path: S.path, at: $('#re-hex-at').value.trim() || '0', length: 1024 }); } catch (err) { S.cache.hex = { error: err.message }; }
      render();
    });
    root.querySelectorAll('[data-entry]').forEach((a) => a.addEventListener('click', async (ev) => {
      ev.preventDefault();
      try { const j = await C.api('/api/re/zip', { path: S.path, entry: a.dataset.entry }); await inspect(j.path, `${S.name} › ${j.name}`); } catch (err) { C.toast?.(err.message, 'error'); }
    }));
    $('#re-copy')?.addEventListener('click', () => { navigator.clipboard?.writeText(JSON.stringify(S.ov, null, 2)); C.toast?.('Report copied'); });
    $('#re-ask')?.addEventListener('click', () => {
      const o = S.ov;
      C.ask?.(`Reverse engineer ${S.name} (path: ${S.path}). It is a ${o.format}${o.arch ? ` ${o.arch}` : ''} ${o.kind || ''}. Use the re tool: start with inspect, then strings, symbols, disasm or decompile as needed. Explain what it does and how it works, with evidence (offsets, addresses, sections), and list what stays unknown.`);
    });
  }

  window.AstroRE = {
    show(container, ctx) { C = ctx; root = container; render(); if (!S.prov) providers(); },
    inspect: (p) => inspect(p),
  };
})();
