// coder_suite.js - Advanced Developer Tools: HTTP Client, DB Inspector (SQLite), Multi-Port Preview, Diagnostics Auto-Healer
(function() {
  'use strict';

  const esc = (s) => String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

  // ---------- 1. HTTP / API Scratchpad ----------
  function renderHttpScratchpad(container) {
    container.innerHTML = `
      <div class="scratchpad-box">
        <div class="ws-head">
          <span class="ws-head-title">API & HTTP Scratchpad</span>
          <span class="grow"></span>
          <button type="button" class="ws-btn sm primary" id="api-send-btn">Send Request</button>
        </div>
        <div class="api-request-bar">
          <select class="api-method-select" id="api-method">
            <option value="GET">GET</option>
            <option value="POST">POST</option>
            <option value="PUT">PUT</option>
            <option value="PATCH">PATCH</option>
            <option value="DELETE">DELETE</option>
          </select>
          <input type="text" class="input api-url-input" id="api-url" placeholder="http://127.0.0.1:4173/api/status" value="http://127.0.0.1:4173/api/status" />
        </div>
        <div class="api-tabs-bar">
          <button type="button" class="tab-chip active" data-atab="body">Body</button>
          <button type="button" class="tab-chip" data-atab="headers">Headers</button>
        </div>
        <div class="api-tab-content" id="api-tab-body">
          <textarea class="api-textarea" id="api-req-body" placeholder="JSON payload (for POST/PUT/PATCH)..."></textarea>
        </div>
        <div class="api-tab-content" id="api-tab-headers" hidden>
          <textarea class="api-textarea" id="api-req-headers" placeholder="X-Custom-Header: value&#10;Authorization: Bearer token"></textarea>
        </div>
        <div class="api-response-block" id="api-res-block">
          <div class="api-res-meta">
            <span class="res-status" id="api-res-status">Status: ---</span>
            <span class="res-time" id="api-res-time">Time: ---</span>
          </div>
          <pre class="api-res-body" id="api-res-body">Response will appear here...</pre>
        </div>
      </div>
    `;

    const sendBtn = container.querySelector('#api-send-btn');
    const methodSel = container.querySelector('#api-method');
    const urlInput = container.querySelector('#api-url');
    const bodyInput = container.querySelector('#api-req-body');
    const headersInput = container.querySelector('#api-req-headers');
    const resStatus = container.querySelector('#api-res-status');
    const resTime = container.querySelector('#api-res-time');
    const resBody = container.querySelector('#api-res-body');

    container.querySelectorAll('[data-atab]').forEach(b => {
      b.onclick = () => {
        container.querySelectorAll('[data-atab]').forEach(x => x.classList.toggle('active', x === b));
        container.querySelector('#api-tab-body').hidden = b.dataset.atab !== 'body';
        container.querySelector('#api-tab-headers').hidden = b.dataset.atab !== 'headers';
      };
    });

    sendBtn.onclick = async () => {
      sendBtn.disabled = true;
      sendBtn.textContent = 'Sending…';
      resStatus.textContent = 'Connecting…';

      try {
        const rawHeaders = {};
        headersInput.value.split('\n').forEach(line => {
          const idx = line.indexOf(':');
          if (idx > 0) rawHeaders[line.slice(0, idx).trim()] = line.slice(idx + 1).trim();
        });

        const r = await window.Astro.apiJson('/api/dev/http/exec', {
          method: methodSel.value,
          url: urlInput.value.trim(),
          body: bodyInput.value,
          headers: rawHeaders
        });

        resStatus.textContent = `Status: ${r.status} ${r.status >= 200 && r.status < 300 ? 'OK' : ''}`;
        resStatus.className = `res-status ${r.status >= 200 && r.status < 300 ? 'status-ok' : 'status-err'}`;
        resTime.textContent = `Latency: ${r.duration_ms} ms`;
        try {
          const parsed = JSON.parse(r.body);
          resBody.textContent = JSON.stringify(parsed, null, 2);
        } catch {
          resBody.textContent = r.body || '(empty response)';
        }
      } catch (err) {
        resStatus.textContent = 'Error';
        resStatus.className = 'res-status status-err';
        resBody.textContent = err.message;
      } finally {
        sendBtn.disabled = false;
        sendBtn.textContent = 'Send Request';
      }
    };
  }

  // ---------- 2. Database Inspector (SQLite) ----------
  function renderDbInspector(container) {
    container.innerHTML = `
      <div class="db-inspector-box">
        <div class="ws-head">
          <span class="ws-head-title">Database Inspector (SQLite)</span>
          <span class="grow"></span>
          <button type="button" class="ws-btn sm" id="db-refresh-tables">Refresh Tables</button>
          <button type="button" class="ws-btn sm primary" id="db-run-btn">Execute SQL</button>
        </div>
        <div class="db-controls-bar">
          <input type="text" class="input db-file-input" id="db-file" placeholder="Database path (e.g. app.db)" value="app.db" />
          <div class="db-tables-list" id="db-tables-list"></div>
        </div>
        <div class="db-editor-wrap">
          <textarea class="db-sql-input" id="db-sql" placeholder="SELECT * FROM users LIMIT 50;"></textarea>
        </div>
        <div class="db-results-wrap" id="db-results-wrap">
          <div class="empty-state">Run an SQL statement to view records.</div>
        </div>
      </div>
    `;

    const runBtn = container.querySelector('#db-run-btn');
    const refreshBtn = container.querySelector('#db-refresh-tables');
    const fileInput = container.querySelector('#db-file');
    const sqlInput = container.querySelector('#db-sql');
    const tablesList = container.querySelector('#db-tables-list');
    const resultsWrap = container.querySelector('#db-results-wrap');

    const loadTables = async () => {
      try {
        const project = window.Astro?.ws?.project || '';
        const r = await window.Astro.apiJson('/api/dev/db/tables', { project, file: fileInput.value.trim() });
        const tbls = r.tables || [];
        if (!tbls.length) {
          tablesList.innerHTML = '<span class="hint">No tables found</span>';
          return;
        }
        tablesList.innerHTML = tbls.map(t => `
          <button type="button" class="chip" data-tbl="${esc(t.name)}">${esc(t.name)}</button>
        `).join('');

        tablesList.querySelectorAll('[data-tbl]').forEach(b => {
          b.onclick = () => {
            sqlInput.value = `SELECT * FROM ${b.dataset.tbl} LIMIT 50;`;
            runSql();
          };
        });
      } catch (err) {
        tablesList.innerHTML = `<span class="hint error">${esc(err.message)}</span>`;
      }
    };

    const runSql = async () => {
      const sql = sqlInput.value.trim();
      if (!sql) return;
      resultsWrap.innerHTML = '<div class="ag-spin"></div>';

      try {
        const project = window.Astro?.ws?.project || '';
        const r = await window.Astro.apiJson('/api/dev/db/query', { project, file: fileInput.value.trim(), sql });
        const cols = r.columns || [];
        const rows = r.rows || [];

        if (!cols.length && !rows.length) {
          resultsWrap.innerHTML = `<div class="empty-state">Query executed. Rows affected: ${r.changes || 0}</div>`;
          return;
        }

        resultsWrap.innerHTML = `
          <div class="table-scroll">
            <table class="db-table">
              <thead><tr>${cols.map(c => `<th>${esc(c)}</th>`).join('')}</tr></thead>
              <tbody>
                ${rows.map(row => `<tr>${row.map(val => `<td>${esc(val === null ? 'NULL' : String(val))}</td>`).join('')}</tr>`).join('')}
              </tbody>
            </table>
          </div>
          <div class="db-row-count">${rows.length} row(s) returned · ${r.changes || 0} change(s)</div>
        `;
      } catch (err) {
        resultsWrap.innerHTML = `<div class="ws-note error">${esc(err.message)}</div>`;
      }
    };

    runBtn.onclick = runSql;
    refreshBtn.onclick = loadTables;
    loadTables();
  }

  // ---------- 3. Multi-Port Live Preview Switcher ----------
  function renderPortSwitcher(container) {
    container.innerHTML = `
      <div class="ports-switcher-box">
        <div class="ws-head">
          <span class="ws-head-title">Multi-Port Live Previews</span>
          <span class="grow"></span>
          <button type="button" class="ws-btn sm" id="ports-refresh-btn">Scan Active Ports</button>
        </div>
        <div class="ports-list" id="ports-list">Scanning...</div>
        <div class="preview-frame-wrap" id="preview-frame-wrap" hidden>
          <div class="preview-toolbar">
            <span class="preview-url-lbl" id="preview-url-lbl"></span>
            <span class="grow"></span>
            <button type="button" class="ws-btn sm" id="preview-new-tab">Open in Tab ↗</button>
          </div>
          <iframe class="preview-iframe" id="multi-preview-iframe" sandbox="allow-scripts allow-forms allow-same-origin"></iframe>
        </div>
      </div>
    `;

    const portsList = container.querySelector('#ports-list');
    const refreshBtn = container.querySelector('#ports-refresh-btn');
    const frameWrap = container.querySelector('#preview-frame-wrap');
    const iframe = container.querySelector('#multi-preview-iframe');
    const urlLbl = container.querySelector('#preview-url-lbl');
    const newTabBtn = container.querySelector('#preview-new-tab');

    const scanPorts = async () => {
      try {
        const r = await window.Astro.apiJson('/api/dev/ports/list', {});
        const ports = (r.ports || []).map(p => p.port).filter((p, i, a) => a.indexOf(p) === i).sort((a,b)=>a-b);
        if (!ports.length) {
          portsList.innerHTML = '<div class="empty-state">No active listening ports detected.</div>';
          return;
        }

        portsList.innerHTML = ports.map(port => `
          <button type="button" class="port-card" data-port="${port}">
            <div class="port-number">:${port}</div>
            <div class="port-tag">${port === 4173 ? 'Astro Backend' : port === 8080 ? 'App Preview' : port === 3000 ? 'Dev Server' : 'Active Service'}</div>
          </button>
        `).join('');

        portsList.querySelectorAll('[data-port]').forEach(btn => {
          btn.onclick = () => {
            const p = btn.dataset.port;
            const targetUrl = window.location.protocol + '//' + window.location.hostname + (p === '80' ? '' : `:${p}`);
            urlLbl.textContent = targetUrl;
            iframe.src = targetUrl;
            frameWrap.hidden = false;
            newTabBtn.onclick = () => window.open(targetUrl, '_blank');
          };
        });
      } catch (err) {
        portsList.innerHTML = `<div class="ws-note error">${esc(err.message)}</div>`;
      }
    };

    refreshBtn.onclick = scanPorts;
    scanPorts();
  }

  // ---------- 4. Diagnostic Auto-Heal Loop (Devin/Claude Code style) ----------
  async function autoHealProblems() {
    const a = window.Astro;
    if (!a || !a.ws?.project) return;
    a.toast('Diagnosing project syntax & types…');

    try {
      const r = await a.apiJson('/api/dev/diagnostics', { project: a.ws.project });
      const probs = (r.problems || []).filter(p => p.severity === 'error');
      if (!probs.length) {
        a.toast('All files verified. Zero syntax or type errors detected!');
        return;
      }

      a.toast(`Detected ${probs.length} error(s). Activating autonomous auto-healing…`);
      const errorSummary = probs.slice(0, 10).map(p => `- ${p.path}:${p.line}:${p.col}: ${p.message}`).join('\n');
      const prompt = `Autonomous Auto-Heal Directive:\nThe project has the following syntax/compiler diagnostics:\n${errorSummary}\n\nInspect the affected files, fix the root causes, and re-verify.`;

      a.switchView('agent');
      if (window.AstroDev?.agentInput) {
        await window.AstroDev.agentInput(a.AG.cur ? a.AG.sessions.get(a.AG.cur) : null, prompt);
      }
    } catch (err) {
      a.toast(err.message, 'error');
    }
  }

  window.AstroCoderSuite = {
    renderHttpScratchpad,
    renderDbInspector,
    renderPortSwitcher,
    autoHealProblems
  };
})();
