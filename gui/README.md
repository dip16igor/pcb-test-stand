# Web Interface

Browser GUI for the test stand (spec §5). Served by the daemon:
`GET /` → `index.html`, `GET /static/*` → this directory.

## Files

- `index.html` — board + 14 output checkboxes + 13 input LEDs.
- `style.css` — layout, LED colors (green = 1, red = 0, gray = unknown).
- `script.js` — WebSocket client (spec §5.5).
- `scheme.png` — underlay diagram (copy of `doc/Backgroгnd.png`).

## Placing elements

1. Open `http://localhost:8080/?edit` and drag elements into place.
2. Press **Save to index.html** — the daemon writes the coordinates
   straight into this file (no copy-paste; commit the result with git).
   **Copy layout HTML** remains as a clipboard fallback.
3. Overwrite `scheme.png` (PNG, ≥1920px wide recommended) and re-check.
   Hover an LED for its GUI name (`title` attribute).

## Behavior

- On connect: badge → Connected, LEDs → gray, explicit `STATUS`
  request (daemon also pushes polled `STATUS` at 10 Hz).
- Checkbox toggle → `{"type":"CMD","payload":"SET PIN_xxx ON\n"}`;
  next `STATUS` syncs the UI (no optimistic update).
- Disconnect → badge red, LEDs gray, reconnect every 3 s.
