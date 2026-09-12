# Web Interface

Browser GUI for the test stand (spec §5). Served by the daemon:
`GET /` → `index.html`, `GET /static/*` → this directory.

## Files

- `index.html` — board + 14 output checkboxes + 13 input LEDs.
- `style.css` — layout, LED colors (green = 1, red = 0, gray = unknown).
- `script.js` — WebSocket client (spec §5.5).
- `scheme.png` — underlay diagram (copy of `doc/Backgroгnd.png`).

## Replacing the underlay

1. Overwrite `scheme.png` (PNG, ≥1920px wide recommended).
2. Move elements: every control/indicator carries inline
   `style="top: Y%; left: X%"` — percentages of the board box,
   so they track image scaling. Drag values until each sits on
   its node; no other file changes needed.
3. Hover an LED for its GUI name (`title` attribute).

## Behavior

- On connect: badge → Connected, LEDs → gray, explicit `STATUS`
  request (daemon also pushes polled `STATUS` at 10 Hz).
- Checkbox toggle → `{"type":"CMD","payload":"SET PIN_xxx ON\n"}`;
  next `STATUS` syncs the UI (no optimistic update).
- Disconnect → badge red, LEDs gray, reconnect every 3 s.
