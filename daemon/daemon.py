#!/usr/bin/env python3
"""UART daemon: bridge between STM32 test-stand firmware and the web GUI.

Spec section 4. Three jobs:
  1. Find the board (COM auto-detect with PING -> PONG verification, §4.2).
  2. Serve the GUI over HTTP (port 8080) and talk to it over WebSocket (§4.3).
  3. Translate WebSocket JSON <-> UART text lines, plus poll STATUS (§3.5).

Decisions (from spec clarifications):
  - No firmware push (FW-6): the daemon polls STATUS at --poll-interval.
  - No port found: wait and rescan every --rescan-interval seconds, don't exit.
  - Manual --port overrides detection but is retried the same way if it drops.

Run:  python daemon.py --auto-port --http-port 8080
Test without hardware:  python daemon.py --port loop:// --http-port 8081
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import queue
import re
import threading
import time
from pathlib import Path

import serial
import serial.tools.list_ports
import uvicorn
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles

HERE = Path(__file__).resolve().parent
LOG_FILE = HERE / "daemon.log"

BAUDRATE = 115200
# USB UART chips we prefer during auto-detect (spec §4.2).
USB_DESCRIPTIONS = ("USB", "FTDI", "CP210", "CH340", "CH341", "PL2303")
USB_VID_PID = {(0x0403, None), (0x10C4, None), (0x1A86, None), (0x067B, None)}

log = logging.getLogger("daemon")


# --------------------------------------------------------------------------
# UART side (blocking thread: pyserial has no asyncio support)
# --------------------------------------------------------------------------

def probe_port(device: str, baudrate: int = BAUDRATE, timeout: float = 0.5) -> bool:
    """Open `device`, send PING, return True iff PONG arrives in time (§4.2)."""
    try:
        ser = serial.serial_for_url(device, baudrate, timeout=timeout)
    except Exception as exc:
        log.debug("probe %s: open failed: %s", device, exc)
        return False
    try:
        ser.reset_input_buffer()
        ser.write(b"PING\n")
        ser.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace").strip()
            if line == "PONG":
                return True
        return False
    except Exception as exc:
        log.debug("probe %s: io failed: %s", device, exc)
        return False
    finally:
        ser.close()


def detect_port(baudrate: int = BAUDRATE) -> str | None:
    """Scan ports, USB-like first, return first that answers PONG (§4.2)."""
    ports = list(serial.tools.list_ports.comports())

    def usbish(p) -> bool:
        desc = (p.description or "").upper()
        if any(s in desc for s in USB_DESCRIPTIONS):
            return True
        return (p.vid, None) in USB_VID_PID or (p.vid, p.pid) in {
            (v, p_) for v, p_ in USB_VID_PID if p_ is not None
        } or p.vid in {v for v, _ in USB_VID_PID}

    for port in sorted(ports, key=lambda p: (not usbish(p), p.device)):
        log.info("probing %s (%s)...", port.device, port.description)
        if probe_port(port.device, baudrate):
            log.info("auto-detected and verified port: %s", port.device)
            return port.device
    return None


class UartThread(threading.Thread):
    """Owns the serial port. Async side talks via `tx`/`rx` queues."""

    def __init__(self, manual_port: str | None, baudrate: int, rescan_interval: float):
        super().__init__(daemon=True, name="uart")
        self.manual_port = manual_port
        self.baudrate = baudrate
        self.rescan_interval = rescan_interval
        self.tx: queue.Queue[str] = queue.Queue()
        self.rx: queue.Queue[str] = queue.Queue()
        self.connected = threading.Event()
        self.port_name: str | None = None
        self._stop = threading.Event()
        self.drop_requested = threading.Event()  # link supervisor: force rescan

    def stop(self) -> None:
        self._stop.set()

    def _connect(self) -> serial.Serial | None:
        if self.manual_port:
            try:
                ser = serial.serial_for_url(
                    self.manual_port, self.baudrate, timeout=0.05
                )
            except Exception as exc:
                log.warning("port %s: open failed: %s", self.manual_port, exc)
                return None
            if probe_port(self.manual_port, self.baudrate):
                log.info("port %s verified (PONG)", self.manual_port)
            else:
                log.warning("port %s: no PONG, continuing anyway (manual override)",
                            self.manual_port)
            return ser
        device = detect_port(self.baudrate)
        if device is None:
            return None
        try:
            return serial.serial_for_url(device, self.baudrate, timeout=0.05)
        except Exception as exc:
            log.warning("port %s: open failed: %s", device, exc)
            return None

    @staticmethod
    def _drain(q: queue.Queue) -> None:
        try:
            while True:
                q.get_nowait()
        except queue.Empty:
            pass

    def run(self) -> None:
        buf = bytearray()
        while not self._stop.is_set():
            ser = self._connect()
            if ser is None:
                log.info("no board, rescanning in %.0fs...", self.rescan_interval)
                self._stop.wait(self.rescan_interval)
                continue
            self.port_name = ser.port
            # Fresh start: drop stale bytes and queued lines from the outage.
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            self._drain(self.tx)
            self._drain(self.rx)
            buf.clear()
            self.connected.set()
            log.info("UART connected: %s", self.port_name)
            try:
                # ANY serial failure (read or write, e.g. surprise USB
                # removal on Windows) lands here and triggers a rescan.
                # Only queue.Empty (no more to send) continues the loop.
                # readline() can return mid-line fragments when the poll loop
                # outruns a long STATUS frame (~470 chars): accumulate bytes
                # until \n so the bridge only ever sees whole lines.
                while not self._stop.is_set():
                    if self.drop_requested.is_set():
                        self.drop_requested.clear()
                        log.warning("link supervisor: dropping silent %s",
                                    self.port_name)
                        break
                    try:
                        while True:
                            line = self.tx.get_nowait()
                            log.debug("TX: %s", line.strip())
                            ser.write(line.encode("utf-8"))
                    except queue.Empty:
                        pass
                    ser.flush()
                    raw = ser.readline()
                    if raw:
                        buf.extend(raw)
                        if buf.endswith(b"\n"):
                            text = bytes(buf).decode("utf-8", "replace")
                            buf.clear()
                            log.debug("RX: %s", text.strip())
                            self.rx.put(text)
            except Exception as exc:
                log.warning("UART I/O failed on %s: %s", self.port_name, exc)
            finally:
                self.connected.clear()
                try:
                    ser.close()
                except Exception:
                    pass
                log.warning("UART disconnected, rescanning...")


# --------------------------------------------------------------------------
# HTTP + WebSocket side
# --------------------------------------------------------------------------

class Hub:
    """Connected browsers; single async bridge loop drives UART polling."""

    def __init__(self, uart: UartThread, poll_interval: float,
                 adc_interval: float = 0.5, link_timeout: float = 2.0):
        self.uart = uart
        self.poll_interval = poll_interval
        self.adc_interval = adc_interval
        # No reply this long (while polling!) means the board is gone even
        # though the adapter still accepts bytes (e.g. STM32 powered off).
        self.link_timeout = link_timeout
        self.last_rx = 0.0
        self.clients: set[WebSocket] = set()
        self.last_status: str = ""
        self.last_adc: str = ""
        self.last_pwm: str = ""
        self.refresh = asyncio.Event()  # immediate STATUS re-read (after SET)
        self._was_connected = False

    async def broadcast(self, msg: dict) -> None:
        dead = []
        for ws in self.clients:
            try:
                await ws.send_json(msg)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.clients.discard(ws)

    def _classify(self, line: str) -> dict | None:
        s = line.strip()
        if not s:
            return None
        if s.startswith("STATUS"):
            self.last_status = s[len("STATUS"):].strip()
            log.debug("STATUS: %d pins", len(self.last_status.split()))
            return {"type": "STATUS", "payload": self.last_status}
        if s == "ADC" or s.startswith("ADC "):
            self.last_adc = s[len("ADC"):].strip()
            log.debug("ADC: %s", self.last_adc)
            return {"type": "ADC", "payload": self.last_adc}
        if s == "PWM" or s.startswith("PWM "):
            self.last_pwm = s[len("PWM"):].strip()
            log.debug("PWM: %s", self.last_pwm)
            return {"type": "PWM", "payload": self.last_pwm}
        if s.startswith("ERR"):
            log.warning("FW: %s", s)
        else:
            log.info("FW: %s", s)
        return {"type": "RESP", "payload": s + "\n"}

    async def loop(self) -> None:
        next_poll = 0.0
        next_adc = 0.0
        while True:
            connected = self.uart.connected.is_set()
            now = asyncio.get_event_loop().time()
            if connected != self._was_connected:
                self._was_connected = connected
                self.last_rx = now if connected else 0.0
                await self.broadcast({
                    "type": "STATE",
                    "payload": "CONNECTED" if connected else "DISCONNECTED",
                })
            if connected:
                # Drain everything the firmware (or loopback) sent.
                try:
                    while True:
                        msg = self._classify(self.uart.rx.get_nowait())
                        self.last_rx = now
                        if msg is not None:
                            await self.broadcast(msg)
                except queue.Empty:
                    pass
                if (self.last_rx
                        and now - self.last_rx > self.link_timeout):
                    log.warning("link silent %.1fs, dropping %s",
                                now - self.last_rx, self.uart.port_name)
                    self.last_rx = 0.0
                    self.uart.drop_requested.set()
                now = asyncio.get_event_loop().time()
                if self.refresh.is_set() or now >= next_poll:
                    self.refresh.clear()
                    next_poll = now + self.poll_interval
                    self.uart.tx.put("STATUS\n")
                    self.uart.tx.put("PWM\n")
                if now >= next_adc:
                    next_adc = now + self.adc_interval
                    self.uart.tx.put("ADC\n")
            await asyncio.sleep(0.01)


PIN_RE = re.compile(r'data-pin="(PIN_[A-Z0-9_]+)"')
COORD_RE = re.compile(r"(top:\s*)([\d.]+)(%\s*;\s*left:\s*)([\d.]+)(%)")
WIDTH_RE = re.compile(r"(width:\s*)([\d.]+)(px)")
HEIGHT_RE = re.compile(r"(height:\s*)([\d.]+)(px)")
PCT_RE = re.compile(r"^\d+(\.\d+)?%$")
PX_RE = re.compile(r"^\d+(\.\d+)?px$")


def apply_layout(index_path: Path, layout: dict) -> tuple[int, list[str]]:
    """Rewrite coords (and chart px sizes) in index.html for given pins.

    Only touches `style="top: ..%; left: ..%"` (plus `width`/`height` px on
    lines that already carry them, e.g. charts) on lines with a known
    data-pin; everything else is byte-preserved. Returns (saved, skipped).
    """
    lines = index_path.read_text(encoding="utf-8").splitlines(keepends=True)
    saved = 0
    skipped: list[str] = []
    out = []
    for line in lines:
        m = PIN_RE.search(line)
        if not m or m.group(1) not in layout:
            out.append(line)
            continue
        entry = layout[m.group(1)]
        try:
            top, left = entry["top"], entry["left"]
        except (TypeError, KeyError):
            skipped.append(m.group(1))
            out.append(line)
            continue
        if not (isinstance(top, str) and isinstance(left, str)
                and PCT_RE.match(top) and PCT_RE.match(left)):
            skipped.append(m.group(1))
            out.append(line)
            continue
        new_line, n = COORD_RE.subn(
            lambda c: c.group(1) + top[:-1] + c.group(3) + left[:-1] + c.group(5),
            line, count=1)
        if n == 0:
            skipped.append(m.group(1))
            out.append(line)
            continue
        # Chart dimensions: applied only where the line already has them.
        for dim_re, dim_key in ((WIDTH_RE, "width"), (HEIGHT_RE, "height")):
            dim_val = entry.get(dim_key) if isinstance(entry, dict) else None
            if (isinstance(dim_val, str) and PX_RE.match(dim_val)
                    and dim_re.search(new_line)):
                new_line = dim_re.sub(
                    lambda c: c.group(1) + dim_val[:-2] + c.group(3),
                    new_line, count=1)
        out.append(new_line)
        saved += 1
    if saved:
        index_path.write_text("".join(out), encoding="utf-8")
    return saved, skipped


def build_app(hub: Hub, gui_dir: Path | None) -> FastAPI:
    app = FastAPI(title="pcb-test-stand daemon")

    @app.get("/")
    async def index():
        if gui_dir is not None and (gui_dir / "index.html").exists():
            return FileResponse(gui_dir / "index.html")
        return PlainTextResponse(
            "GUI not built yet (gui/index.html missing). "
            "WebSocket API is live at /ws.\n",
            status_code=503,
        )

    if gui_dir is not None and gui_dir.exists():
        app.mount("/static", StaticFiles(directory=gui_dir), name="static")

    @app.websocket("/ws")
    async def ws_endpoint(ws: WebSocket):
        await ws.accept()
        hub.clients.add(ws)
        try:
            await ws.send_json({
                "type": "STATE",
                "payload": "CONNECTED" if hub.uart.connected.is_set()
                           else "DISCONNECTED",
            })
            if hub.last_status:
                await ws.send_json({"type": "STATUS", "payload": hub.last_status})
            if hub.last_adc:
                await ws.send_json({"type": "ADC", "payload": hub.last_adc})
            if hub.last_pwm:
                await ws.send_json({"type": "PWM", "payload": hub.last_pwm})
            while True:
                msg = await ws.receive_json()
                if msg.get("type") != "CMD":
                    continue
                payload = str(msg.get("payload", ""))
                if not payload.endswith("\n"):
                    payload += "\n"
                if not hub.uart.connected.is_set():
                    log.warning("CMD dropped, UART disconnected: %s", payload.strip())
                    await ws.send_json(
                        {"type": "RESP", "payload": "ERR UART_DISCONNECTED\n"})
                    continue
                log.info("WS CMD: %s", payload.strip())
                hub.uart.tx.put(payload)
                # SET/PWM change outputs: re-read STATUS (and PWM) right away.
                if payload.startswith("SET") or payload.startswith("PWM"):
                    hub.refresh.set()
        except WebSocketDisconnect:
            pass
        finally:
            hub.clients.discard(ws)

    @app.post("/api/layout")
    async def save_layout(req: Request):
        """Edit-mode Save: persist element coordinates into gui/index.html."""
        if gui_dir is None or not (gui_dir / "index.html").exists():
            return JSONResponse({"detail": "no gui/index.html"}, status_code=404)
        try:
            body = await req.json()
            layout = body.get("layout", {})
        except Exception:
            return JSONResponse({"detail": "invalid JSON"}, status_code=400)
        if not isinstance(layout, dict) or not layout:
            return JSONResponse({"detail": "empty layout"}, status_code=400)
        try:
            saved, skipped = apply_layout(gui_dir / "index.html", layout)
        except Exception as exc:
            log.warning("layout save failed: %s", exc)
            return JSONResponse({"detail": "write failed"}, status_code=500)
        log.info("layout saved: %d pins (%s)", saved, gui_dir / "index.html")
        return {"saved": saved, "skipped": skipped}

    return app


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="pcb-test-stand UART daemon")
    p.add_argument("--port", default=None,
                   help="manual COM port (e.g. COM3, /dev/ttyUSB0, loop://); "
                        "overrides auto-detection")
    p.add_argument("--auto-port", dest="auto_port", action="store_true",
                   default=True)
    p.add_argument("--no-auto-port", dest="auto_port", action="store_false")
    p.add_argument("--http-port", type=int, default=8080)
    p.add_argument("--baudrate", type=int, default=BAUDRATE)
    p.add_argument("--poll-interval", type=float, default=0.1,
                   help="STATUS poll period in seconds (default 0.1)")
    p.add_argument("--adc-interval", type=float, default=0.5,
                   help="ADC poll period in seconds (default 0.5)")
    p.add_argument("--link-timeout", type=float, default=2.0,
                   help="drop link after this many silent seconds (default 2.0)")
    p.add_argument("--rescan-interval", type=float, default=3.0,
                   help="COM rescan period when no board found (default 3.0)")
    p.add_argument("--gui-dir", default=str(HERE.parent / "gui"),
                   help="directory served as GUI (default ../gui)")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="log every UART TX/RX line, including STATUS/ADC polls")
    return p.parse_args(argv)


def main(argv=None) -> None:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[logging.StreamHandler(),
                  logging.FileHandler(LOG_FILE, encoding="utf-8")],
    )
    manual = args.port if args.port else None
    if manual:
        log.info("manual port: %s", manual)
    elif not args.auto_port:
        log.error("no --port given and --no-auto-port set; nothing to open")
        raise SystemExit(2)

    uart = UartThread(manual, args.baudrate, args.rescan_interval)
    uart.start()
    hub = Hub(uart, args.poll_interval, args.adc_interval, args.link_timeout)

    gui_dir = Path(args.gui_dir)
    if not (gui_dir / "index.html").exists():
        log.warning("GUI missing at %s; / returns 503 until gui/ is built",
                    gui_dir)
        gui_for_app: Path | None = gui_dir if gui_dir.exists() else None
    else:
        gui_for_app = gui_dir
    app = build_app(hub, gui_for_app)

    async def lifespan_wrapper():
        bridge = asyncio.ensure_future(hub.loop())
        config = uvicorn.Config(app, host="127.0.0.1", port=args.http_port,
                                log_level="warning")
        server = uvicorn.Server(config)
        try:
            await server.serve()
        finally:
            bridge.cancel()
            uart.stop()

    log.info("serving HTTP on 127.0.0.1:%d", args.http_port)
    asyncio.run(lifespan_wrapper())


if __name__ == "__main__":
    main()
