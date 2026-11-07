// =============================================================================================
// A.S.T.R.O AI Pro: Executive HUD, Autonomous Sentinel, Dual-Presence Bridge & Proactive Contact
// =============================================================================================
(function () {
  'use strict';

  let proState = {
    pro: true,
    edition: 'A.S.T.R.O AI Pro',
    version: '2.0.0-PRO',
    uptime_sec: 0,
    dual_presence: {
      pc_online: true,
      server_mode: 'hub_primary',
      failover_policy: 'instant_zero_drop',
      devices_connected: 1,
      devices: []
    },
    watcher: {
      active: true,
      status: 'sentinel_armed',
      monitored_targets: ['build_integrity', 'test_runner', 'agent_milestones', 'permission_gate'],
      total_events: 0
    },
    swarm: [],
    contact_info: {
      total_dispatched: 0,
      emergency_phone: '+12025550199'
    },
    events: []
  };

  let radarAnimId = null;
  let audioCtx = null;

  function getAudioContext() {
    if (!audioCtx) {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (AudioCtx) audioCtx = new AudioCtx();
    }
    if (audioCtx && audioCtx.state === 'suspended') audioCtx.resume();
    return audioCtx;
  }

  // Synthesize executive tactical HUD audio chime (dual harmonic chime)
  function playHudChime(kind = 'alert') {
    try {
      const ctx = getAudioContext();
      if (!ctx) return;
      const now = ctx.currentTime;
      const osc1 = ctx.createOscillator();
      const osc2 = ctx.createOscillator();
      const gain = ctx.createGain();

      gain.connect(ctx.destination);
      osc1.connect(gain);
      osc2.connect(gain);

      if (kind === 'alert') {
        osc1.frequency.setValueAtTime(880, now);
        osc1.frequency.exponentialRampToValueAtTime(1760, now + 0.12);
        osc2.frequency.setValueAtTime(587.33, now);
        osc2.frequency.exponentialRampToValueAtTime(1174.66, now + 0.12);
        gain.gain.setValueAtTime(0.2, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.45);
        osc1.start(now);
        osc2.start(now);
        osc1.stop(now + 0.45);
        osc2.stop(now + 0.45);
      } else {
        // Confirmation chime
        osc1.frequency.setValueAtTime(523.25, now); // C5
        osc1.frequency.setValueAtTime(659.25, now + 0.08); // E5
        osc1.frequency.setValueAtTime(783.99, now + 0.16); // G5
        gain.gain.setValueAtTime(0.15, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.4);
        osc1.start(now);
        osc1.stop(now + 0.4);
      }
    } catch (_) {}
  }

  async function fetchProStatus() {
    try {
      const r = await fetch('/api/pro/status', { headers: { 'x-astro-client': '1' } });
      if (r.ok) {
        const data = await r.json();
        proState = { ...proState, ...data };
        updateTopPill();
      }
    } catch (_) {}

    try {
      const re = await fetch('/api/pro/events', { headers: { 'x-astro-client': '1' } });
      if (re.ok) {
        const d = await re.json();
        if (Array.isArray(d.events)) proState.events = d.events;
      }
    } catch (_) {}
  }

  function updateTopPill() {
    const pill = document.getElementById('top-pro-pill');
    const label = document.getElementById('top-pro-bridge-text');
    if (!pill || !label) return;

    const devs = proState.dual_presence?.devices_connected || 1;
    label.textContent = devs > 1 ? `Pro · ${devs} Nodes Synced` : 'A.S.T.R.O Pro';
  }

  // Tactical Vector Radar Canvas
  function initRadarCanvas(canvas) {
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let angle = 0;
    const nodes = [
      { id: 'hub', label: 'Primary Hub (PC)', r: 0.15, theta: 0, color: '#f59e0b', pulse: 0 },
      { id: 'phone', label: 'Android Bridge', r: 0.48, theta: 1.1, color: '#10b981', pulse: 0 },
      { id: 'sentinel', label: 'Autonomous Sentinel', r: 0.65, theta: 3.2, color: '#06b6d4', pulse: 0 },
      { id: 'swarm', label: 'Agent Swarm', r: 0.78, theta: 4.8, color: '#a855f7', pulse: 0 },
      { id: 'contact', label: 'Telephony Gate', r: 0.35, theta: 2.3, color: '#ef4444', pulse: 0 }
    ];

    function draw() {
      const w = canvas.width = canvas.parentElement?.clientWidth || 400;
      const h = canvas.height = canvas.parentElement?.clientHeight || 280;
      const cx = w / 2;
      const cy = h / 2;
      const maxR = Math.min(cx, cy) - 20;

      ctx.clearRect(0, 0, w, h);

      // Background grid
      ctx.strokeStyle = 'rgba(245, 158, 11, 0.08)';
      ctx.lineWidth = 1;

      // Range circles
      for (let i = 1; i <= 4; i++) {
        const r = (maxR * i) / 4;
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.stroke();

        ctx.fillStyle = 'rgba(245, 158, 11, 0.3)';
        ctx.font = '9px JetBrains Mono, monospace';
        ctx.fillText(`${i * 25}%`, cx + 4, cy - r + 11);
      }

      // Crosshairs
      ctx.beginPath();
      ctx.moveTo(cx, cy - maxR);
      ctx.lineTo(cx, cy + maxR);
      ctx.moveTo(cx - maxR, cy);
      ctx.lineTo(cx + maxR, cy);
      ctx.stroke();

      // Rotating Radar Beam
      angle = (angle + 0.02) % (Math.PI * 2);
      const grad = ctx.createConicGradient(angle, cx, cy);
      grad.addColorStop(0, 'rgba(245, 158, 11, 0.28)');
      grad.addColorStop(0.12, 'rgba(245, 158, 11, 0.03)');
      grad.addColorStop(0.2, 'transparent');
      grad.addColorStop(1, 'transparent');

      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(cx, cy, maxR, 0, Math.PI * 2);
      ctx.fill();

      // Sweep line
      ctx.strokeStyle = 'rgba(245, 158, 11, 0.85)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + Math.cos(angle) * maxR, cy + Math.sin(angle) * maxR);
      ctx.stroke();

      // Draw nodes
      nodes.forEach((n) => {
        const nx = cx + Math.cos(n.theta) * (n.r * maxR);
        const ny = cy + Math.sin(n.theta) * (n.r * maxR);

        // Diff angle
        const diff = Math.abs((angle - n.theta + Math.PI * 2) % (Math.PI * 2));
        if (diff < 0.2) n.pulse = 1;
        if (n.pulse > 0) n.pulse -= 0.015;

        // Blinking / Pulsing halo
        if (n.pulse > 0) {
          ctx.beginPath();
          ctx.arc(nx, ny, 6 + (1 - n.pulse) * 16, 0, Math.PI * 2);
          ctx.strokeStyle = n.color;
          ctx.lineWidth = 1.5;
          ctx.stroke();
        }

        // Node core
        ctx.beginPath();
        ctx.arc(nx, ny, 3.5, 0, Math.PI * 2);
        ctx.fillStyle = n.color;
        ctx.fill();

        // Node label
        ctx.fillStyle = '#f8fafc';
        ctx.font = '10px Inter, sans-serif';
        ctx.fillText(n.label, nx + 7, ny + 3);
      });

      radarAnimId = requestAnimationFrame(draw);
    }

    if (radarAnimId) cancelAnimationFrame(radarAnimId);
    draw();
  }

  // Render Pro View
  function renderProView() {
    const el = document.getElementById('pro-view');
    if (!el) return;

    fetchProStatus().then(() => {
      buildProHtml(el);
      const canvas = document.getElementById('pro-radar-canvas');
      if (canvas) initRadarCanvas(canvas);
    });
  }

  function buildProHtml(el) {
    const isFailoverReady = proState.dual_presence?.failover_policy === 'instant_zero_drop';
    const emergencyPhone = proState.contact_info?.emergency_phone || '+12025550199';

    el.innerHTML = `
      <div class="pro-shell">
        <header class="pro-header">
          <div class="pro-badge-row">
            <span class="pro-badge-chip"><svg class="pro-ico"><use href="#i-pro-badge"/></svg> A.S.T.R.O AI PRO</span>
            <span class="pro-edition-tag">VERSION ${proState.version || '2.0.0-PRO'}</span>
            <span class="pro-sentinel-tag active"><span class="dot pulse"></span> SENTINEL ACTIVE</span>
          </div>
          <div class="pro-header-sub">
            <h1>Executive Mission Control & Dual-Presence HUD</h1>
            <p>Autonomous project sentinel, zero-delay failover bridge, and real-time proactive contact gateway.</p>
          </div>
          <div class="pro-theme-switcher">
            <span class="pro-theme-label">HUD Theme:</span>
            <button class="pro-theme-btn active" data-theme="obsidian_gold" type="button">Obsidian Gold</button>
            <button class="pro-theme-btn" data-theme="cyber_pro" type="button">Cyber Stealth</button>
            <button class="pro-theme-btn" data-theme="titanium" type="button">Titanium Slate</button>
          </div>
        </header>

        <!-- Top Telemetry Row -->
        <div class="pro-telemetry-grid">
          <div class="pro-metric-card">
            <div class="pro-metric-head">
              <span class="pro-metric-title">DUAL-PRESENCE BRIDGE</span>
              <svg class="pro-metric-ico"><use href="#i-signal"/></svg>
            </div>
            <div class="pro-metric-val">PC Studio Online</div>
            <div class="pro-metric-sub">
              <span class="pro-ok-dot"></span> Port 4173 Hub · ${proState.dual_presence?.devices_connected || 1} Device(s) Synced
            </div>
          </div>

          <div class="pro-metric-card">
            <div class="pro-metric-head">
              <span class="pro-metric-title">AUTO-FAILOVER HANDOVER</span>
              <svg class="pro-metric-ico"><use href="#i-refresh"/></svg>
            </div>
            <div class="pro-metric-val">${isFailoverReady ? 'Zero-Drop Active' : 'Standby'}</div>
            <div class="pro-metric-sub">
              Android switches to local engine instantly if PC turns off
            </div>
          </div>

          <div class="pro-metric-card">
            <div class="pro-metric-head">
              <span class="pro-metric-title">PROACTIVE CONTACT GATE</span>
              <svg class="pro-metric-ico"><use href="#i-phone"/></svg>
            </div>
            <div class="pro-metric-val">Voice & SMS Ready</div>
            <div class="pro-metric-sub">
              Target: ${emergencyPhone} (${proState.contact_info?.total_dispatched || 0} alerts sent)
            </div>
          </div>

          <div class="pro-metric-card">
            <div class="pro-metric-head">
              <span class="pro-metric-title">AUTONOMOUS SENTINEL</span>
              <svg class="pro-metric-ico"><use href="#i-radar"/></svg>
            </div>
            <div class="pro-metric-val">Watcher Armed</div>
            <div class="pro-metric-sub">
              Monitoring builds, tests, stalls, milestones
            </div>
          </div>
        </div>

        <!-- Middle Columns: Tactical Radar + Contact Dispatcher -->
        <div class="pro-columns">
          <!-- Tactical Vector Radar -->
          <div class="pro-panel">
            <div class="pro-panel-head">
              <h2><svg class="pro-ico"><use href="#i-radar"/></svg> Tactical Multi-Node Radar</h2>
              <span class="pro-badge-sm">LIVE TELEMETRY</span>
            </div>
            <div class="pro-radar-wrap">
              <canvas id="pro-radar-canvas"></canvas>
            </div>
            <div class="pro-radar-legend">
              <span><i style="background:#f59e0b"></i> PC Primary Hub</span>
              <span><i style="background:#10b981"></i> Android Phone</span>
              <span><i style="background:#06b6d4"></i> Autonomous Sentinel</span>
              <span><i style="background:#a855f7"></i> Agent Swarm</span>
              <span><i style="background:#ef4444"></i> Contact Gate</span>
            </div>
          </div>

          <!-- Proactive Contact Skill Dispatcher -->
          <div class="pro-panel">
            <div class="pro-panel-head">
              <h2><svg class="pro-ico"><use href="#i-phone"/></svg> Proactive Contact Skill Dispatcher</h2>
              <span class="pro-badge-sm">HUMAN-IN-THE-LOOP</span>
            </div>
            <p class="pro-panel-desc">Directly test or execute autonomous contact calls, SMS alerts, or priority rings to your mobile device.</p>

            <form class="pro-form" id="pro-contact-form">
              <div class="pro-form-group">
                <label for="pro-contact-reason">Event / Reason for Contact</label>
                <input id="pro-contact-reason" class="pro-input" type="text" placeholder="e.g. Critical test failure in payment module" value="Project milestone completed - deployment authorization requested" required>
              </div>

              <div class="pro-form-row">
                <div class="pro-form-group">
                  <label for="pro-contact-chan">Contact Channel</label>
                  <select id="pro-contact-chan" class="pro-select">
                    <option value="call" selected>Voice Phone Call (Bland / Phone Ring)</option>
                    <option value="sms">SMS Text Alert</option>
                    <option value="alert">High-Priority Device Notification</option>
                    <option value="auto">Auto (Call if Critical, else Alert)</option>
                  </select>
                </div>
                <div class="pro-form-group">
                  <label for="pro-contact-urgency">Urgency Level</label>
                  <select id="pro-contact-urgency" class="pro-select">
                    <option value="urgent" selected>Urgent (Immediate Wake / Call)</option>
                    <option value="high">High Priority</option>
                    <option value="normal">Normal</option>
                  </select>
                </div>
              </div>

              <div class="pro-form-group">
                <label for="pro-contact-phone">Destination Phone</label>
                <input id="pro-contact-phone" class="pro-input" type="tel" value="${emergencyPhone}">
              </div>

              <div class="pro-btn-row">
                <button type="submit" class="pro-btn pro-btn-primary" id="btn-dispatch-contact">
                  <svg class="pro-btn-ico"><use href="#i-phone"/></svg>
                  <span>Dispatch Proactive Contact</span>
                </button>
                <button type="button" class="pro-btn pro-btn-secondary" id="btn-test-chime">
                  <svg class="pro-btn-ico"><use href="#i-bell"/></svg>
                  <span>Test Audio Ring</span>
                </button>
              </div>
              <div class="pro-feedback" id="pro-contact-feedback" hidden></div>
            </form>
          </div>
        </div>

        <!-- Lower Section: Autonomous Project Watcher Matrix & Swarm -->
        <div class="pro-columns-equal">
          <!-- Watcher Matrix -->
          <div class="pro-panel">
            <div class="pro-panel-head">
              <h2><svg class="pro-ico"><use href="#i-eye"/></svg> Project Watcher Sentinel Matrix</h2>
              <span class="pro-badge-sm">AUTONOMOUS TRIGGERS</span>
            </div>
            <p class="pro-panel-desc">Autonomous conditions that automatically trigger the Contact Skill when human intervention is needed:</p>

            <div class="pro-watcher-triggers">
              <div class="pro-trigger-item">
                <div class="pro-trigger-info">
                  <strong>Build Failure Sentinel</strong>
                  <span>Triggers contact if compilation or bundling exits with fatal error</span>
                </div>
                <button type="button" class="pro-trigger-btn" data-trigger="build_error">Simulate</button>
              </div>

              <div class="pro-trigger-item">
                <div class="pro-trigger-info">
                  <strong>Test Suite Regression</strong>
                  <span>Dispatches phone call or SMS if integration tests fail</span>
                </div>
                <button type="button" class="pro-trigger-btn" data-trigger="test_failure">Simulate</button>
              </div>

              <div class="pro-trigger-item">
                <div class="pro-trigger-info">
                  <strong>Task Stall Watcher</strong>
                  <span>Alerts user if an autonomous subagent stalls for more than 5 minutes</span>
                </div>
                <button type="button" class="pro-trigger-btn" data-trigger="task_stalled">Simulate</button>
              </div>

              <div class="pro-trigger-item">
                <div class="pro-trigger-info">
                  <strong>Milestone Authorization Gate</strong>
                  <span>Calls user when work is finished for signoff before deployment</span>
                </div>
                <button type="button" class="pro-trigger-btn" data-trigger="milestone_complete">Simulate</button>
              </div>
            </div>
          </div>

          <!-- Multi-Agent Swarm Status -->
          <div class="pro-panel">
            <div class="pro-panel-head">
              <h2><svg class="pro-ico"><use href="#i-agent"/></svg> Autonomous Pro Swarm Coordinator</h2>
              <span class="pro-badge-sm">5 SPECIALIZED ENGINES</span>
            </div>
            <div class="pro-swarm-list">
              <div class="pro-swarm-item">
                <div class="pro-swarm-dot green"></div>
                <div class="pro-swarm-info">
                  <strong>System Architect</strong>
                  <span>Plan decomposition & multi-file execution strategy</span>
                </div>
                <span class="pro-swarm-status">ACTIVE</span>
              </div>
              <div class="pro-swarm-item">
                <div class="pro-swarm-dot cyan"></div>
                <div class="pro-swarm-info">
                  <strong>Autonomous Sentinel</strong>
                  <span>Continuous background project watcher & self-healing</span>
                </div>
                <span class="pro-swarm-status">WATCHING</span>
              </div>
              <div class="pro-swarm-item">
                <div class="pro-swarm-dot gold"></div>
                <div class="pro-swarm-info">
                  <strong>Proactive Contact Bridge</strong>
                  <span>Bland AI telephony, SMS gateway & Android Heads-Up pusher</span>
                </div>
                <span class="pro-swarm-status">READY</span>
              </div>
              <div class="pro-swarm-item">
                <div class="pro-swarm-dot purple"></div>
                <div class="pro-swarm-info">
                  <strong>Security & Code Auditor</strong>
                  <span>OWASP Top 10 auditing, memory safety, mitigation verification</span>
                </div>
                <span class="pro-swarm-status">STANDBY</span>
              </div>
              <div class="pro-swarm-item">
                <div class="pro-swarm-dot blue"></div>
                <div class="pro-swarm-info">
                  <strong>Dual-Presence Sync Daemon</strong>
                  <span>Continuous zero-drop failover state syncing between PC & Android</span>
                </div>
                <span class="pro-swarm-status">SYNCED</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Recent Logs Section -->
        <div class="pro-panel" style="margin-top:20px;">
          <div class="pro-panel-head">
            <h2><svg class="pro-ico"><use href="#i-terminal"/></svg> Proactive Sentinel Event Logs</h2>
            <button class="tiny-btn" id="pro-refresh-logs" type="button" title="Refresh logs"><svg><use href="#i-refresh"/></svg></button>
          </div>
          <div class="pro-log-table" id="pro-log-table">
            ${renderLogRows()}
          </div>
        </div>
      </div>
    `;

    bindProEvents(el);
  }

  function renderLogRows() {
    const list = proState.events || [];
    if (!list.length) {
      return '<p class="pro-empty-logs">No sentinel watcher events recorded yet. Triggers will log here automatically.</p>';
    }
    return `
      <table class="pro-table">
        <thead>
          <tr>
            <th>TIMESTAMP</th>
            <th>PROJECT</th>
            <th>EVENT TYPE</th>
            <th>DETAILS</th>
            <th>ACTION TAKEN</th>
          </tr>
        </thead>
        <tbody>
          ${list.slice(0, 15).map((ev) => `
            <tr>
              <td>${new Date(ev.timestamp || Date.now()).toLocaleTimeString()}</td>
              <td><code>${escapeHtml(ev.project || 'astro')}</code></td>
              <td><span class="pro-tag">${escapeHtml(ev.event_type || 'event')}</span></td>
              <td>${escapeHtml(ev.details || '')}</td>
              <td><span class="pro-status-tag">${escapeHtml(ev.action_taken || 'logged')}</span></td>
            </tr>
          `).join('')}
        </tbody>
      </table>
    `;
  }

  function escapeHtml(s) {
    return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function bindProEvents(el) {
    // Form submit
    const form = document.getElementById('pro-contact-form');
    if (form) {
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const reason = document.getElementById('pro-contact-reason')?.value || 'System event';
        const channel = document.getElementById('pro-contact-chan')?.value || 'call';
        const urgency = document.getElementById('pro-contact-urgency')?.value || 'urgent';
        const phone = document.getElementById('pro-contact-phone')?.value || '+12025550199';
        const feedback = document.getElementById('pro-contact-feedback');

        playHudChime('alert');

        if (feedback) {
          feedback.hidden = false;
          feedback.className = 'pro-feedback pending';
          feedback.textContent = `Dispatching ${channel.toUpperCase()} to ${phone}…`;
        }

        try {
          const res = await fetch('/api/pro/contact', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-astro-client': '1' },
            body: JSON.stringify({ reason, channel, urgency, phone })
          });
          const json = await res.json();
          if (json.ok) {
            playHudChime('confirm');
            if (feedback) {
              feedback.className = 'pro-feedback success';
              feedback.textContent = `Successfully initiated ${json.channel.toUpperCase()} to ${json.recipient} (Contact ID: ${json.contact_id})`;
            }
            fetchProStatus().then(() => {
              const table = document.getElementById('pro-log-table');
              if (table) table.innerHTML = renderLogRows();
            });
          } else {
            if (feedback) {
              feedback.className = 'pro-feedback error';
              feedback.textContent = `Dispatch failed: ${json.error || 'Server error'}`;
            }
          }
        } catch (err) {
          if (feedback) {
            feedback.className = 'pro-feedback error';
            feedback.textContent = `Network error: ${err.message}`;
          }
        }
      });
    }

    // Test Chime
    document.getElementById('btn-test-chime')?.addEventListener('click', () => {
      playHudChime('alert');
    });

    // Theme Switchers
    el.querySelectorAll('.pro-theme-btn').forEach((btn) => {
      btn.addEventListener('click', () => {
        el.querySelectorAll('.pro-theme-btn').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        const theme = btn.getAttribute('data-theme') || 'obsidian_gold';
        document.documentElement.setAttribute('data-pro-theme', theme);
      });
    });

    // Sentinel Trigger Simulations
    el.querySelectorAll('.pro-trigger-btn').forEach((btn) => {
      btn.addEventListener('click', async () => {
        const type = btn.getAttribute('data-trigger');
        playHudChime('alert');
        btn.disabled = true;
        btn.textContent = 'Triggered!';

        try {
          await fetch('/api/pro/watch', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-astro-client': '1' },
            body: JSON.stringify({
              project: 'Astro-AI-Core',
              event_type: type,
              details: `Sentinel simulation: ${type.replace('_', ' ')} detected in autonomous loop`,
              auto_contact: true
            })
          });
          fetchProStatus().then(() => {
            const table = document.getElementById('pro-log-table');
            if (table) table.innerHTML = renderLogRows();
          });
        } catch (_) {}

        setTimeout(() => {
          btn.disabled = false;
          btn.textContent = 'Simulate';
        }, 3000);
      });
    });

    // Refresh logs button
    document.getElementById('pro-refresh-logs')?.addEventListener('click', () => {
      fetchProStatus().then(() => {
        const table = document.getElementById('pro-log-table');
        if (table) table.innerHTML = renderLogRows();
      });
    });
  }

  // Periodic heartbeat / polling
  setInterval(fetchProStatus, 15000);
  setTimeout(fetchProStatus, 1000);

  // Top pill click handler
  document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('top-pro-pill')?.addEventListener('click', () => {
      if (typeof window.astroSwitchView === 'function') {
        window.astroSwitchView('pro');
      } else {
        const nav = document.getElementById('nav-pro');
        if (nav) nav.click();
      }
    });
  });

  window.AstroPro = {
    renderProView,
    fetchProStatus,
    playHudChime
  };
})();
