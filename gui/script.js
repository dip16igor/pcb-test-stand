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
  };

  ws.onmessage = (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg.type === 'STATUS') {
      parseStatus(msg.payload || '');
    } else if (msg.type === 'STATE') {
      setConnected(msg.payload === 'CONNECTED');
    }
    // RESP (OK/ERR echoes) intentionally ignored: next STATUS syncs UI.
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
