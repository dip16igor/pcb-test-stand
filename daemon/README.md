# UART Daemon

Bridge between the STM32 test-stand firmware and the browser GUI
(spec §4). Polls `STATUS` over UART, forwards it over WebSocket,
serves the GUI over HTTP.

## Install

```bat
pip install -r requirements.txt
```

## Run methods

Run from the `daemon/` directory. The log tells you what happened —
look for `auto-detected and verified port: COMx`.

**1. Normal: auto-detect the board (recommended)**

```bat
python daemon.py --auto-port --http-port 8080
```

Scans COM ports, sends `PING` to each USB-like one, uses the first that
answers `PONG`. Then open `http://localhost:8080`.

**2. Manual port (known adapter, or several plugged in)**

```bat
python daemon.py --port COM6
python daemon.py --port /dev/ttyUSB0 --http-port 8080
```

Skips detection. A missing `PONG` only logs a warning — your explicit
choice wins. Use this when auto-detect grabs the wrong adapter.

**3. No hardware (loopback smoke test)**

```bat
python daemon.py --port loop:// --http-port 8081
```

UART echoes everything back: GUI connects, `STATUS`/`ADC` flow (empty),
commands echo as `RESP`. Proves the whole HTTP+WS+serial chain.

**4. Fast volts (~50 Hz) for the chart**

```bat
python daemon.py --auto-port --adc-interval 0.02 --poll-interval 0.5
```

## Parameters

| Flag | Default | Meaning, with example |
|---|---|---|
| `--port` | — | manual port, skips detection. `--port COM6`, `--port /dev/ttyUSB0`, `--port loop://` |
| `--auto-port` / `--no-auto-port` | on | COM auto-detect with PING→PONG. `--no-auto-port` without `--port` exits with an error (nothing to open) |
| `--http-port` | 8080 | HTTP/WebSocket listen port on 127.0.0.1. `--http-port 8081` when 8080 is busy |
| `--baudrate` | 115200 | UART baud, 8N1 fixed per spec. Change only if the firmware was rebuilt with another rate |
| `--poll-interval` | 0.1 | STATUS poll period, seconds. GUI LEDs/outputs refresh at this rate. Raise to 0.5 when ADC runs fast (wire budget) |
| `--adc-interval` | 0.5 | ADC poll period, seconds; broadcast as `ADC` message. `--adc-interval 0.02` ≈ 50 Hz volts; above ~75 Hz the daemon loop itself is the limit |
| `--link-timeout` | 2.0 | drop link after this many silent seconds. Catches board power-off while the adapter stays plugged in; GUI goes gray, rescan follows |
| `--rescan-interval` | 3.0 | COM rescan period when no board answers. Never exits — power the board late, it joins up |
| `--gui-dir` | `../gui` | directory served as GUI. `--gui-dir ./alt-gui` to try another layout |

Wire budget at 115200 baud: a full 27-pin STATUS needs ~40 ms, an ADC
frame ~5 ms. Defaults (≈50% wire) leave room for SET commands; pushing
both intervals to the floor saturates the link and the GUI lags.

## Troubleshooting

- `no board, rescanning...` forever → adapter unplugged, wrong `--port`,
  or another program (terminal!) holds the COM port. Close Bray first.
- Port changed after replug (COM6 → COM7 on Windows) → auto-detect
  follows it; the log names the new port. Manual `--port` does not.
- GUI shows stale data but board is off → upgrade: `--link-timeout`
  drops silent links (default 2 s).

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
