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

function connect() {
  const url = `ws://${location.host}/ws`;
  ws = new WebSocket(url);

  ws.onopen = () => {
    setConnected(true);
    markAllUnknown();
    // Daemon pushes STATUS on its poll loop too; ask explicitly for sync.
    ws.send(JSON.stringify({type: 'CMD', payload: 'STATUS\n'}));
    ws.send(JSON.stringify({type: 'CMD', payload: 'VERSION\n'}));
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
      }
    } else if (msg.type === 'RESP' && (msg.payload || '').startsWith('FW ')) {
      document.getElementById('fwver').textContent = (msg.payload || '').trim();
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

connect();

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

  const items = [...document.querySelectorAll('.control, .indicator')];
  const key = (el) => el.dataset.pin || (el.querySelector('input') || {}).dataset.pin;
  // Apply in-progress arrangement.
  items.forEach(el => {
    const p = saved[key(el)];
    if (p) { el.style.left = p[0]; el.style.top = p[1]; }
  });

  const pos = (el) => [el.style.left, el.style.top];
  const save = () => {
    const o = {};
    items.forEach(el => { o[key(el)] = pos(el); });
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

  document.getElementById('editcopy').addEventListener('click', async () => {
    serialize();
    try { await navigator.clipboard.writeText(out.value); read.textContent = 'copied'; }
    catch { out.select(); read.textContent = 'clipboard blocked — copy manually'; }
  });
  document.getElementById('editreset').addEventListener('click', () => {
    localStorage.removeItem(store);
    location.reload();
  });
  serialize();
})();
