# UART Daemon

Bridge between the STM32 test-stand firmware and the browser GUI
(spec §4). Polls `STATUS` over UART, forwards it over WebSocket,
serves the GUI over HTTP.

## Install

```bat
pip install -r requirements.txt
```

## Run

```bat
python daemon.py --auto-port --http-port 8080
```

Manual port (overrides auto-detection):

```bat
python daemon.py --port COM3
python daemon.py --port /dev/ttyUSB0 --http-port 8080
```

No hardware (loopback smoke test):

```bat
python daemon.py --port loop:// --http-port 8081
```

## CLI

| Flag | Default | Meaning |
|---|---|---|
| `--port` | — | manual port, skips detection |
| `--auto-port` / `--no-auto-port` | on | COM auto-detect with PING→PONG |
| `--http-port` | 8080 | HTTP/WebSocket listen port |
| `--baudrate` | 115200 | UART baud (8N1, fixed per spec) |
| `--poll-interval` | 0.1 | STATUS poll period, seconds |
| `--adc-interval` | 1.0 | ADC poll period, seconds (broadcast as `ADC` message) |
| `--rescan-interval` | 3.0 | COM rescan when no board, seconds |
| `--gui-dir` | `../gui` | directory served as GUI |

## Auto-detection (§4.2)

1. List ports via `pyserial.tools.list_ports`.
2. USB-like first (`USB`, `FTDI`, `CP210x`, `CH340`, … / known VID:PID).
3. Open 115200 8N1, send `PING\n`, accept first port answering `PONG`
   within 0.5 s.
4. Nothing found → log + rescan every `--rescan-interval` (never exits).
   Manual `--port` without `PONG` logs a warning but keeps going.

## Protocol (§4.3)

- `GET /` → `gui/index.html` (503 notice until `gui/` is built).
- `GET /static/*` → files from `gui/`.
- `ws://localhost:8080/ws`, JSON:
  - Client → server: `{"type": "CMD", "payload": "SET PIN_PA4 ON\n"}`
  - Server → client: `{"type": "RESP", "payload": "OK PIN_PA4 ON\n"}`
  - Server → client: `{"type": "STATUS", "payload": "PIN_PA4:OUT:ON ..."}`
  - Server → client: `{"type": "STATE", "payload": "CONNECTED"}`
    (extension for the GUI connection dot, §GUI-7).
- `POST /api/layout` with `{"layout": {"PIN_PA4": {"left": "9%", "top": "6%"}, ...}}`
  rewrites coordinates in `gui/index.html` (edit-mode Save button).
  Only `top`/`left` percentages on known `data-pin` lines change;
  chart lines additionally persist `width`/`height` px.

Every `SET` triggers an immediate `STATUS` re-read on top of polling.

## Log

Console + `daemon.log` next to the script.
