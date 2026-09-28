'use strict';
// Test-stand GUI: WebSocket client (spec §5.5).
// Served by the daemon, so the socket lives on the same host:port.

const statusEl = document.getElementById('status');
let ws = null;
let reconnectTimer = null;

function setConnected(on) {
  statusEl.textContent = on ? 'Connected' : 'Disconnected';
  statusEl.className = on ? 'connected' : 'disconnected';
  if (!on) markAllUnknown();
  if (!on) {
    // Stale volts are worse than none.
    document.querySelectorAll('.voltread .vval').forEach(el => {
      el.textContent = '—';
    });
  }
}

// "VSYS:12.34 24V_IN1:24.10 ..." -> overlay readouts. Elements are static
// (draggable in ?edit); channels that never arrive stay hidden.
function renderAdc(payload) {
  payload.split(' ').filter(x => x).forEach(pair => {
    const idx = pair.indexOf(':');
    if (idx < 0) return;
    const el = document.querySelector(`.voltread[data-volt="${pair.slice(0, idx)}"]`);
    if (!el) return;
    el.querySelector('.vval').textContent = pair.slice(idx + 1) + 'V';
    el.classList.add('live');
  });
}

// Firmware replies that carry UI state (not STATUS/ADC): version string and
// interlock mode. Returns true when consumed.
function handleFwResp(payload) {
  if (payload.startsWith('FW ')) {
    document.getElementById('fwver').textContent = payload.trim();
    return true;
  }
  const pwr = document.getElementById('pwrstate');
  if (payload.startsWith('STATE ')) {
    if (pwr) pwr.textContent = payload.slice(6).trim();
    return true;
  }
  if (payload === 'OK INTERLOCK ON' || payload === 'INTERLOCK ON') {
    box.checked = true;
    return true;
  }
  if (payload === 'OK INTERLOCK OFF' || payload === 'INTERLOCK OFF') {
    box.checked = false;
    return true;
  }
  return false;
}

function markAllUnknown() {
  document.querySelectorAll('.indicator').forEach(el => {
    el.classList.remove('on', 'off');
    el.classList.add('unknown');
  });
}

// "PIN_PA4:OUT:ON PIN_PA8:IN:OFF ..." -> update checkboxes + LEDs.
function parseStatus(payload) {
  const pins = payload.split(' ').filter(x => x);
  pins.forEach(pinStr => {
    const [pinName, dir, state] = pinStr.split(':');
    if (dir === 'OUT') {
      const box = document.querySelector(`input[data-pin="${pinName}"][data-dir="OUT"]`);
      if (box) box.checked = (state === 'ON');
    } else if (dir === 'IN') {
      const led = document.querySelector(`.indicator[data-pin="${pinName}"][data-dir="IN"]`);
      if (led) {
        led.classList.remove('on', 'off', 'unknown');
        if (state === 'ON') led.classList.add('on');
        else if (state === 'OFF') led.classList.add('off');
        else led.classList.add('unknown');
      }
    }
  });
}

// VSYS trend chart: 0..6 V vertical, 5 min sliding window, 60% fill.
// History accumulates locally from ADC frames (1 Hz); reconnect restarts it.
const CHART_SPAN_MS = 5 * 60 * 1000;
const CHART_VMAX = 6;
const charts = {};
function chartInit() {
  document.querySelectorAll('.chart').forEach(el => {
    const canvas = el.querySelector('canvas');
    charts[el.dataset.chart] = {el, canvas, ctx: canvas.getContext('2d'), data: []};
  });
  chartDrawAll(Date.now());
}
function chartPush(name, v, now) {
  const c = charts[name];
  if (!c) return;
  c.data.push({t: now, v});
  const cut = now - CHART_SPAN_MS;
  while (c.data.length && c.data[0].t < cut) c.data.shift();
  chartDraw(c, now);
}
function chartDrawAll(now) {
  Object.values(charts).forEach(c => chartDraw(c, now));
}
function chartDraw(c, now) {
  const dpr = window.devicePixelRatio || 1;
  const W = Math.max(50, c.canvas.clientWidth), H = Math.max(40, c.canvas.clientHeight);
  if (!W || !H) return;
  if (c.canvas.width !== Math.round(W * dpr) || c.canvas.height !== Math.round(H * dpr)) {
    c.canvas.width = Math.round(W * dpr);
    c.canvas.height = Math.round(H * dpr);
  }
  const g = c.ctx;
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  // Plot area reserves room for tick labels.
  const px0 = 30, px1 = W - 6, py0 = 6, py1 = H - 16;
  const yOf = (v) => py1 - Math.min(v, CHART_VMAX) / CHART_VMAX * (py1 - py0);
  const xOf = (t) => px0 + (1 - (now - t) / CHART_SPAN_MS) * (px1 - px0);
  g.font = '10px monospace';
  g.lineWidth = 1;
  // Horizontal gridlines each 1 V.
  for (let v = 0; v <= CHART_VMAX; v++) {
    const y = yOf(v);
    g.strokeStyle = v === 0 ? '#555' : '#333';
    g.beginPath(); g.moveTo(px0, y); g.lineTo(px1, y); g.stroke();
    g.fillStyle = '#ccc';
    g.fillText(v + 'V', 4, y + 3);
  }
  // Vertical gridlines: minor each 30 s, labeled each 60 s as age.
  for (let s = 0; s <= CHART_SPAN_MS / 1000; s += 30) {
    const x = xOf(now - s * 1000);
    if (x < px0) continue;
    const major = s % 60 === 0;
    g.strokeStyle = major ? '#444' : '#262626';
    g.beginPath(); g.moveTo(x, py0); g.lineTo(x, py1); g.stroke();
    if (major) {
      g.fillStyle = '#ccc';
      const label = s === 0 ? 'now' : '-' + Math.floor(s / 60) + 'm' + (s % 60 ? (s % 60) + 's' : '');
      g.fillText(label, x - 8, H - 4);
    }
  }
  if (c.data.length < 2) return;
  // 60% fill down to 0 V, bright trace on top.
  g.beginPath();
  g.moveTo(Math.max(xOf(c.data[0].t), px0), yOf(c.data[0].v));
  c.data.forEach(p => g.lineTo(Math.max(xOf(p.t), px0), yOf(p.v)));
  const lx = Math.max(xOf(c.data[c.data.length - 1].t), px0);
  const fx = Math.max(xOf(c.data[0].t), px0);
  g.lineTo(lx, yOf(0)); g.lineTo(fx, yOf(0)); g.closePath();
  g.fillStyle = 'rgba(0, 230, 0, 0.6)';
  g.fill();
  g.beginPath();
  g.moveTo(Math.max(xOf(c.data[0].t), px0), yOf(c.data[0].v));
  c.data.forEach(p => g.lineTo(Math.max(xOf(p.t), px0), yOf(p.v)));
  g.strokeStyle = '#00ff00';
  g.lineWidth = 2;
  g.stroke();
}
function adcCharts(payload, now) {
  payload.split(' ').filter(x => x).forEach(pair => {
    const idx = pair.indexOf(':');
    if (idx < 0) return;
    const v = parseFloat(pair.slice(idx + 1));
    if (!isNaN(v)) chartPush(pair.slice(0, idx), v, now);
  });
}
function connect() {
  const url = `ws://${location.host}/ws`;
  ws = new WebSocket(url);

  ws.onopen = () => {
    setConnected(true);
    markAllUnknown();
    // Daemon pushes STATUS on its poll loop too; ask explicitly for sync.
    ws.send(JSON.stringify({type: 'CMD', payload: 'STATUS\n'}));
    ws.send(JSON.stringify({type: 'CMD', payload: 'VERSION\n'}));
    ws.send(JSON.stringify({type: 'CMD', payload: 'STATE\n'}));
    ws.send(JSON.stringify({type: 'CMD', payload: 'INTERLOCK\n'}));
  };

  ws.onmessage = (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg.type === 'STATUS') {
      parseStatus(msg.payload || '');
    } else if (msg.type === 'STATE') {
      setConnected(msg.payload === 'CONNECTED');
      if (msg.payload === 'CONNECTED') {
        document.getElementById('fwver').textContent = '';
        ws.send(JSON.stringify({type: 'CMD', payload: 'VERSION\n'}));
        ws.send(JSON.stringify({type: 'CMD', payload: 'STATE\n'}));
        ws.send(JSON.stringify({type: 'CMD', payload: 'INTERLOCK\n'}));
      }
    } else if (msg.type === 'RESP') {
      handleFwResp(msg.payload || '');
    } else if (msg.type === 'ADC') {
      if (!msg.payload) return;
      renderAdc(msg.payload);
      adcCharts(msg.payload, Date.now());
    }
    // Other RESP (OK/ERR echoes) intentionally ignored: next STATUS syncs UI.
  };
  const schedule = () => {
    setConnected(false);
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 3000);
    }
  };
  ws.onclose = schedule;
  ws.onerror = schedule;
}

// Checkbox toggles -> SET command (spec §5.5).
document.querySelectorAll('input[data-dir="OUT"]').forEach(box => {
  box.addEventListener('change', () => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    const cmd = `SET ${box.dataset.pin} ${box.checked ? 'ON' : 'OFF'}\n`;
    ws.send(JSON.stringify({type: 'CMD', payload: cmd}));
  });
});

// Interlock mode toggle -> INTERLOCK command (v1.2.0).
document.getElementById('interlock').addEventListener('change', (ev) => {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  ws.send(JSON.stringify({type: 'CMD', payload: `INTERLOCK ${ev.target.checked ? 'ON' : 'OFF'}\n`}));
});

chartInit();
connect();
window.addEventListener('resize', () => chartDrawAll(Date.now()));

// Edit mode (?edit): drag controls/indicators with the mouse, then copy
// the layout HTML back into index.html. Positions persist in localStorage
// while arranging. Normal view is untouched.
(function editMode() {
  if (!new URLSearchParams(location.search).has('edit')) return;
  document.body.classList.add('editing');
  const bar = document.getElementById('editbar');
  const read = document.getElementById('editread');
  const out = document.getElementById('editout');
  bar.hidden = false;
  const board = document.querySelector('.board');
  const store = 'teststand-layout';
  const saved = JSON.parse(localStorage.getItem(store) || '{}');

  const items = [...document.querySelectorAll('.control, .indicator, .voltread, .chart')];
  const key = (el) => el.dataset.pin || (el.querySelector('input') || {}).dataset.pin;
  // Apply in-progress arrangement (arrays = pre-chart drafts).
  items.forEach(el => {
    const p = saved[key(el)];
    if (!p) return;
    if (Array.isArray(p)) { el.style.left = p[0]; el.style.top = p[1]; return; }
    if (p.left) el.style.left = p.left;
    if (p.top) el.style.top = p.top;
    if (p.width && el.classList.contains('chart')) el.style.width = p.width;
    if (p.height && el.classList.contains('chart')) el.style.height = p.height;
  });

  const pos = (el) => [el.style.left, el.style.top];
  const entry = (el) => {
    const p = pos(el);
    const e = {left: p[0], top: p[1]};
    if (el.classList.contains('chart')) { e.width = el.style.width; e.height = el.style.height; }
    return e;
  };
  const save = () => {
    const o = {};
    items.forEach(el => { o[key(el)] = entry(el); });
    localStorage.setItem(store, JSON.stringify(o));
  };
  const serialize = () => {
    out.value = items.map(el => '  ' + el.outerHTML).join('\n');
  };

  let drag = null;
  items.forEach(el => {
    el.addEventListener('pointerdown', (ev) => {
      ev.preventDefault();
      el.setPointerCapture(ev.pointerId);
      drag = {el, x0: ev.clientX, y0: ev.clientY, moved: false};
      el.classList.add('dragging');
    });
    el.addEventListener('pointermove', (ev) => {
      if (!drag || drag.el !== el) return;
      if (Math.hypot(ev.clientX - drag.x0, ev.clientY - drag.y0) < 3) return;
      drag.moved = true;
      const r = board.getBoundingClientRect();
      const left = (ev.clientX - r.left) / r.width * 100;
      const top = (ev.clientY - r.top) / r.height * 100;
      el.style.left = left.toFixed(1) + '%';
      el.style.top = top.toFixed(1) + '%';
      read.textContent = `${key(el)}  left ${el.style.left}  top ${el.style.top}`;
    });
    const drop = () => {
      if (!drag || drag.el !== el) return;
      el.classList.remove('dragging');
      drag = null;
      save();
      serialize();
    };
    el.addEventListener('pointerup', drop);
    el.addEventListener('pointercancel', drop);
  });

  // Corner handles resize charts (px); coordinates still drag as usual.
  document.querySelectorAll('.chart .rhandle').forEach(h => {
    const box = h.closest('.chart');
    h.addEventListener('pointerdown', (ev) => {
      ev.stopPropagation();
      ev.preventDefault();
      const sx = ev.clientX, sy = ev.clientY;
      const w0 = box.offsetWidth, h0 = box.offsetHeight;
      const mv = (e2) => {
        box.style.width = Math.max(140, w0 + e2.clientX - sx) + 'px';
        box.style.height = Math.max(100, h0 + e2.clientY - sy) + 'px';
        chartDrawAll(Date.now());
      };
      const up = () => {
        window.removeEventListener('pointermove', mv);
        window.removeEventListener('pointerup', up);
        read.textContent = `${key(box)}  ${box.style.width} x ${box.style.height}`;
        save();
        serialize();
      };
      window.addEventListener('pointermove', mv);
      window.addEventListener('pointerup', up);
    });
  });

  document.getElementById('editcopy').addEventListener('click', async () => {
    serialize();
    try { await navigator.clipboard.writeText(out.value); read.textContent = 'copied'; }
    catch { out.select(); read.textContent = 'clipboard blocked — copy manually'; }
  });
  document.getElementById('editsave').addEventListener('click', async () => {
    const layout = {};
    items.forEach(el => {
      layout[key(el)] = entry(el);
    });
    read.textContent = 'saving...';
    try {
      const r = await fetch('/api/layout', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({layout}),
      });
      const d = await r.json();
      read.textContent = r.ok ? `saved ${d.saved} to index.html`
                              : `save failed: ${d.detail || r.status}`;
    } catch (e) {
      read.textContent = `save failed: ${e}`;
    }
  });
  document.getElementById('editreset').addEventListener('click', () => {
    localStorage.removeItem(store);
    location.reload();
  });
  serialize();
})();
