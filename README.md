# pcb-test-stand

Bench tool for functional testing of a custom STM32F103RE board:
toggle output rails from a browser, watch input states live.

```
[Web Browser] ←HTTP/WebSocket→ [UART Daemon on PC] ←UART (COM port)→ [STM32F103RE]
```

## Layout

- `firmware/test-stand/` — Arduino/PlatformIO firmware (UART3 protocol, GPIO).
- `daemon/` — Python bridge: COM auto-detect (PING→PONG), HTTP + WebSocket.
- `gui/` — browser overlay: board diagram + 14 controls + 13 indicators.
- `doc/` — specification + board diagram source.

## Quickstart

1. Flash: `pio run -e genericSTM32F103RE -t upload` (ST-Link).
   No hardware yet? `-e bluepill_f103c8` validates protocol on a BluePill.
2. Bridge: `python daemon/daemon.py --auto-port --http-port 8080`
   (needs `pip install -r daemon/requirements.txt`).
3. Open `http://localhost:8080` — toggle a checkbox, watch the rail.

Details in each directory's README; protocol + acceptance criteria
in `doc/Specifications.md` (§3.5, §4.3, §8).

## Versions

SemVer + git tags (`v1.0.0` = first firmware cut; later commits add the
64 MHz clock correction, BluePill test env, daemon, GUI).
`VERSION` over UART reports the firmware's own version.
