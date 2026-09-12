# Technical Specification: Test Stand Software for PCB with STM32F103RE

## 1. General Information

### 1.1. System Purpose
Develop a software suite for functional testing of a custom-designed printed circuit board (PCB). The board contains:
- **STM32F103RE** microcontroller (ARM Cortex-M3, 512 KB Flash, 64 KB RAM).
- Multiple DC-DC converters and charging circuits.
- Peripherals controlled via STM32 GPIO (outputs and inputs).

The system shall enable the operator to:
- Visually view the functional diagram of the board.
- Enable/disable individual board nodes via GUI (control output pins).
- Monitor input pin states in real-time (indicators).
- Receive up-to-date status of all pins (input/output).

### 1.2. Scope of Application
Benchtop testing and debugging of PCB in laboratory conditions. Not intended for end users.

### 1.3. Terms and Definitions
- **PC** — Personal computer (Windows 10/11, Linux Ubuntu 20.04+).
- **GUI** — Graphical user interface (web browser).
- **UART** — Serial interface (USB-to-UART adapter, COM port).
- **Control** — GUI element (checkbox, toggle) for controlling an output pin.
- **Indicator** — GUI element (LED) for displaying input pin state.
- **Underlay** — Background image with functional board diagram.

***

## 2. System Architecture

### 2.1. Components
The system consists of three independent components:

| Component | Description | Technology (Recommended) |
|-----------|-------------|---------------------------|
| **STM32 Firmware** | Embedded software for GPIO control, input polling, and UART communication | **Arduino Framework**, **PlatformIO** (VS Code) |
| **UART Daemon (PC)** | Background process: bridge between UART and web interface, **COM port auto-detection with connection verification** | Python 3.10+, FastAPI + pyserial + websockets |
| **Web Interface** | Browser-based GUI: board diagram + controls + indicators | HTML5, CSS3, JavaScript (Vanilla or Vue 3) |

### 2.2. Interaction Diagram
```
[Web Browser] ←HTTP/WebSocket→ [UART Daemon on PC] ←UART (COM port)→ [STM32F103RE]
```

### 2.3. Communication Requirements
- **UART**: 115200 baud, 8 data bits, 1 stop bit, no parity (8N1), **UART3 on PC10 (TX) and PC11 (RX)**.
- **HTTP/WebSocket**: localhost, port 8080 (configurable).
- **COM Port**: **Auto-detection** with mandatory **connection verification** (`PING` command → `PONG` response).

***

## 3. STM32F103RE Firmware Requirements

### 3.1. Development Environment

| Requirement | Value |
|-------------|-------|
| Framework | **Arduino Framework** (stm32duino) |
| Tools | **PlatformIO** (VS Code extension) |
| Language | C++ (Arduino API) |
| Board in PlatformIO | `nucleo_f103re` or `bluepill_f103c8` (depending on actual board) |
| Project Files | `platformio.ini`, `src/main.cpp` (standard PlatformIO structure) |

### 3.2. STM32 Hardware Configuration

| Parameter | Value |
|-----------|-------|
| Clocking | **Internal RC oscillator (HSI 8 MHz)**, PLL 64 MHz (no external crystal, F1 max from HSI: 4 MHz × 16) |
| UART for PC Communication | **UART3**: **PC10 (TX)**, **PC11 (RX)** |
| UART Settings | 115200 baud, 8N1 |

### 3.3. Managed Pins List

| GPIO | Type | Configuration | Post-Reset Value | GUI Name |
|------|------|---------------|------------------|----------|
| PA4 | OUT | Push-pull | 0 (LOW) | PWR2 |
| PA5 | OUT | Push-pull | 0 (LOW) | EN_7V |
| PA6 | OUT | Push-pull | 0 (LOW) | EN_12V |
| PA7 | OUT | Push-pull | 0 (LOW) | EN_24V |
| PA8 | IN | Pull-up | — | PG1 |
| PA11 | OUT | Push-pull | 0 (LOW) | CE1 |
| PA12 | IN | Pull-up | — | STAT2_1 |
| PA15 | IN | Pull-up | — | STAT1_1 |
| PB0 | IN | Pull-up | — | ALERT |
| PB1 | IN | Pull-up | — | PGOOD |
| PB2 | IN | Pull-up | — | PGOOD2 |
| PB3 | IN | Pull-up | — | CE2 |
| PB8 | OUT | Push-pull | 0 (LOW) | LED1 |
| PB9 | OUT | Push-pull | 0 (LOW) | LED2 |
| PB15 | IN | Pull-up | — | COMP2 |
| PC2 | IN | Pull-up | — | PG |
| PC3 | IN | Pull-up | — | KEY |
| PC4 | OUT | Push-pull | 0 (LOW) | PWR1 |
| PC5 | OUT | Push-pull | 0 (LOW) | EN |
| PC8 | IN | Pull-up | — | COMP1 |
| PC9 | IN | Pull-up | — | STAT2_2 |
| PC12 | IN | Pull-up | — | PG2 |
| PC13 | OUT | Push-pull | 0 (LOW) | POWER1 |
| PC14 | OUT | Push-pull | 0 (LOW) | POWER2 |
| PC15 | OUT | Push-pull | 0 (LOW) | EN_24V2 |
| PD0 | OUT | Push-pull | 0 (LOW) | LED0 |
| PD2 | IN | Pull-up | — | STAT1_2 |

**Notes:**
- **OUT**: `pinMode(pin, OUTPUT)`, initial state `LOW`.
- **IN**: `pinMode(pin, INPUT_PULLUP)`.
- **UART3**: PC10 (TX), PC11 (RX) — initialize via `Serial3` (Arduino API).

### 3.4. Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FW-1 | Initialize UART3 (PC10/TX, PC11/RX, 115200, 8N1) | Mandatory |
| FW-2 | Initialize all GPIO according to table above | Mandatory |
| FW-3 | Receive commands via UART, parse, execute | Mandatory |
| FW-4 | Send responses/status via UART | Mandatory |
| FW-5 | Cyclic input pin polling (minimum 100 Hz) | Mandatory |
| FW-6 | Automatic status transmission on input pin change | Optional |
| FW-7 | Handle invalid commands (ignore, log to debug) | Optional |
| FW-8 | Watchdog (IWDG) for hang protection | Optional |

### 3.5. UART Communication Protocol

**Command Format (ASCII, text, line terminated by `\n`, `\r`, or `\r\n`):**
```
<COMMAND> <ARG1> <ARG2> ... <ARGn> \n
```

**Commands:**

| Command | Description | Example | Response |
|---------|-------------|---------|----------|
| `SET <PIN> ON\|OFF` | Set **output** pin state | `SET PIN_PA4 ON` | `OK PIN_PA4 ON` or `ERR INVALID_PIN` or `ERR PIN_IS_INPUT` |
| `GET <PIN>` | Query pin state (input or output) | `GET PIN_PA8` | `PIN_PA8 ON` or `PIN_PA8 OFF` or `ERR INVALID_PIN` |
| `STATUS` | Dump state of **all** pins (inputs and outputs) | `STATUS` | `STATUS PIN_PA4:OUT:ON PIN_PA8:IN:OFF ...` |
| `PING` | Connection check | `PING` | `PONG` |
| `VERSION` | Firmware version | `VERSION` | `FW v1.0.0 2026-09-11` |

**Protocol Requirements:**
- Command case: **uppercase** (case-sensitive).
- Pin names: format `PIN_<PORT><NUMBER>` (e.g., `PIN_PA4`, `PIN_PA8`).
- Responses: always terminated by `\n`.
- `STATUS` response format: `<PIN>:<DIR>:<STATE>`, where:
  - `<DIR>`: `OUT` (output) or `IN` (input).
  - `<STATE>`: `ON` (1) or `OFF` (0).
- Command timeout: not specified (loop-based operation).
- Receive buffer: minimum 64 bytes.

### 3.6. Non-Functional Requirements

| Requirement | Value |
|-------------|-------|
| Language | C++ (Arduino API) |
| Framework | Arduino (stm32duino) |
| Tools | PlatformIO (VS Code) |
| Code Size | Maximum 64 KB Flash, 8 KB RAM |
| Clocking | HSI 8 MHz, PLL 64 MHz (internal RC, F1 max from HSI) |

***

## 4. UART Daemon (PC) Requirements

### 4.1. Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| DM-1 | **COM port auto-detection** at startup with **connection verification** (PING → PONG) | Mandatory |
| DM-2 | Manual COM port specification via CLI argument (overrides auto-detection) | Optional |
| DM-3 | Open COM port (settings: 115200, 8N1) | Mandatory |
| DM-4 | Start HTTP server on localhost:8080 | Mandatory |
| DM-5 | Serve static web page (HTML/CSS/JS) | Mandatory |
| DM-6 | WebSocket channel for real-time browser communication | Mandatory |
| DM-7 | Bidirectional translation: WebSocket ↔ UART | Mandatory |
| DM-8 | Log commands and responses (console + file) | Optional |
| DM-9 | Error handling (UART disconnect, connection loss) | Optional |
| DM-10 | CLI arguments: `--port COM3`, `--port /dev/ttyUSB0`, `--http-port 8080`, `--auto-port` | Optional |

### 4.2. COM Port Auto-Detection (with Connection Verification)

**Algorithm:**
1. At daemon startup, scan available COM ports (`pyserial.tools.list_ports`).
2. Filter ports by USB signature (e.g., `VID:PID` or description containing "USB", "FTDI", "CP2102", "CH340").
3. For each found port (in sequence):
   - Open port (115200, 8N1, 1 sec timeout).
   - Send command `PING\n`.
   - Wait for response within 500 ms.
   - If response `PONG\n` received — port is working, stop search, use this port.
   - If no response or error — close port, proceed to next.
4. If all ports checked and none responded:
   - Log error.
   - Exit with error code **or** wait for connection (poll every 3 sec, configurable behavior).
5. If working port found — start HTTP server and WebSocket.

**Example (Python):**
```python
import serial
import serial.tools.list_ports
import time

def check_port(device, baudrate=115200, timeout=1):
    try:
        ser = serial.Serial(device, baudrate, timeout=timeout)
        time.sleep(0.1)  # wait for initialization
        ser.write(b"PING\n")
        time.sleep(0.1)
        if ser.in_waiting:
            response = ser.readline().decode('utf-8').strip()
            ser.close()
            return response == "PONG"
        ser.close()
        return False
    except Exception:
        return False

def auto_detect_port():
    ports = serial.tools.list_ports.comports()
    usb_ports = [p for p in ports if 'USB' in p.description or 'FTDI' in p.description or 'CP2102' in p.description or 'CH340' in p.description]
    
    # First check USB ports
    for port in usb_ports:
        if check_port(port.device):
            return port.device
    
    # If no USB found, check all ports
    for port in ports:
        if check_port(port.device):
            return port.device
    
    return None
```

### 4.3. Daemon API

**HTTP:**
- `GET /` — serves `index.html` (web interface).
- `GET /static/*` — static files (CSS, JS, images).

**WebSocket (`ws://localhost:8080/ws`):**
- **Client → Server**: JSON message:
  ```json
  {"type": "CMD", "payload": "SET PIN_PA4 ON\n"}
  ```
- **Server → Client**: JSON message:
  ```json
  {"type": "RESP", "payload": "OK PIN_PA4 ON\n"}
  ```
  or
  ```json
  {"type": "STATUS", "payload": "PIN_PA4:OUT:ON PIN_PA8:IN:OFF ..."}
  ```

### 4.4. Implementation Requirements

| Requirement | Value |
|-------------|-------|
| Language | Python 3.10+ |
| Libraries | `pyserial`, `FastAPI`, `websockets`, `uvicorn` |
| OS | Windows 10/11, Ubuntu 20.04+ |
| Startup | `python daemon.py --auto-port --http-port 8080` |
| Logging | Console + file `daemon.log` (optional) |

***

## 5. Web Interface (GUI) Requirements

### 5.1. Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| GUI-1 | Display background image (underlay) with functional board diagram | Mandatory |
| GUI-2 | Place **controls** (checkboxes) on underlay at specified coordinates (for **output** pins) | Mandatory |
| GUI-3 | Place **indicators** (LEDs) on underlay at specified coordinates (for **input** pins) | Mandatory |
| GUI-4 | Support all pins from table (13 OUT + 15 IN = 28 elements) | Mandatory |
| GUI-5 | On control toggle — send command to server (WebSocket) | Mandatory |
| GUI-6 | Display current control/indicator state (received from STM32) | Mandatory |
| GUI-7 | Show connection status (connected/disconnected) | Optional |
| GUI-8 | Auto-reconnect on WebSocket disconnect | Optional |
| GUI-9 | Responsive design for different screen resolutions (minimum 1280x720) | Optional |

### 5.2. GUI Element Types

| Element | Purpose | Appearance | States |
|---------|---------|------------|--------|
| **Control** (checkbox) | Control **output** pin | Checkbox + label | Checked / Unchecked |
| **Indicator** (LED) | Display **input** pin state | Circular LED (16-24px diameter) | **Green** (1), **Red** (0), **Gray** (unknown/no connection) |

### 5.3. Web Page Structure

**HTML:**
- Container `.board` with `position: relative`.
- Image `.underlay` (functional diagram).
- Controls `.control` with `position: absolute`, coordinates set in CSS (top, left).
- Indicators `.indicator` with `position: absolute`, coordinates set in CSS.

**Example Markup:**
```html
<div class="board">
  <img src="scheme.png" class="underlay" alt="PCB Scheme">
  
  <!-- Control (output pin) -->
  <label class="control" style="top: 120px; left: 340px;">
    <input type="checkbox" data-pin="PIN_PA4" data-dir="OUT">
    PWR2
  </label>
  
  <!-- Indicator (input pin) -->
  <div class="indicator" style="top: 180px; left: 450px;" data-pin="PIN_PA8" data-dir="IN" title="PG1">
  </div>
  
  <!-- other elements -->
</div>

<div id="status">Disconnected</div>
```

### 5.4. Styles (CSS)

**Requirements:**
- Controls and indicators must be clearly visible on diagram background.
- Control labels: readable font (minimum 12px).
- Connection status indicator: green (connected), red (disconnected).

**Example Indicator Styles:**
```css
.indicator {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  border: 2px solid #000;
  position: absolute;
  transform: translate(-50%, -50%);
  background-color: gray; /* default - unknown */
}

.indicator.on {
  background-color: green; /* 1 */
}

.indicator.off {
  background-color: red; /* 0 */
}

.indicator.unknown {
  background-color: gray; /* unknown */
}
```

### 5.5. Logic (JavaScript)

**Mandatory Functionality:**
1. Connect to WebSocket: `ws://localhost:8080/ws`.
2. Event handling:
   - `onopen` → status "Connected", reset all indicators to "unknown" → request `STATUS`.
   - `onclose` / `onerror` → status "Disconnected", set all indicators to "unknown", auto-reconnect (every 3 sec).
   - `onmessage` → parse JSON, update controls and indicators.
3. On checkbox change:
   - Send JSON: `{"type": "CMD", "payload": "SET PIN_PA4 ON\n"}`.
4. On status received (`type: "STATUS"`):
   - Parse string: `PIN_PA4:OUT:ON PIN_PA8:IN:OFF ...`
   - For **controls** (OUT): set checkbox state (checked/unchecked).
   - For **indicators** (IN): set class (`on` / `off` / `unknown`).

**STATUS Processing Example:**
```js
function parseStatus(payload) {
  // payload: "PIN_PA4:OUT:ON PIN_PA8:IN:OFF ..."
  const pins = payload.split(' ').filter(x => x);
  pins.forEach(pinStr => {
    const [pinName, dir, state] = pinStr.split(':');
    if (dir === 'OUT') {
      const checkbox = document.querySelector(`input[data-pin="${pinName}"][data-dir="OUT"]`);
      if (checkbox) checkbox.checked = (state === 'ON');
    } else if (dir === 'IN') {
      const indicator = document.querySelector(`.indicator[data-pin="${pinName}"][data-dir="IN"]`);
      if (indicator) {
        indicator.classList.remove('on', 'off', 'unknown');
        if (state === 'ON') indicator.classList.add('on');
        else if (state === 'OFF') indicator.classList.add('off');
        else indicator.classList.add('unknown');
      }
    }
  });
}
```

**Optional Functionality:**
- Log commands to browser console.
- Visual animation on toggle (transition 0.2s).
- Tooltip on indicator hover (pin name, type, state).

### 5.6. Implementation Requirements

| Requirement | Value |
|-------------|-------|
| Language | JavaScript (ES6+) |
| Framework | Vanilla JS or Vue 3 (optional) |
| Build | Not required (or Vite if Vue) |
| Browsers | Chrome 100+, Firefox 90+, Edge 100+ |
| Resolution | Minimum 1280x720 |

***

## 6. Underlay (Diagram Image) Requirements

### 6.1. Format
- **File**: PNG, minimal compression (for clarity).
- **Resolution**: at least 1920x1080 pixels (or proportionally smaller but readable).
- **Color Scheme**: High contrast for controls and indicators visibility.

### 6.2. Content
- Functional board diagram (blocks: DC-DC, chargers, STM32, connectors).
- Main node labels (optional).
- **No** fine details unrelated to control.

### 6.3. Element Coordinates
- For each **control** and **indicator**, coordinates (X, Y) are manually set by developer in HTML/CSS based on board diagram.

***

## 7. Documentation Requirements

### 7.1. For STM32 Firmware
- README with build instructions (VS Code + PlatformIO, extension installation).
- Pin table (section 3.3).
- Command examples (section 3.5).

### 7.2. For UART Daemon
- README with startup instructions (Python installation, dependencies, CLI arguments).
- COM port auto-detection algorithm description.

### 7.3. For Web Interface
- Instructions for placing controls and indicators (how to edit coordinates in HTML/CSS).
- Screenshot of working interface.

### 7.4. General
- Component interaction diagram (section 2.2).
- Communication protocol (section 3.5, 4.3).

***

## 8. Acceptance Criteria

### 8.1. Functional Tests

| Test | Expected Result |
|------|-----------------|
| Daemon startup, **COM port auto-detection with connection verification** | Daemon starts, log: "Auto-detected and verified port: COM3" |
| Open `http://localhost:8080` in browser | Board diagram displayed with all controls (13 OUT) and indicators (15 IN) |
| Toggle checkbox (e.g., PWR2) in GUI | Corresponding pin PA4 toggles (verified with oscilloscope/multimeter) |
| Change input pin state (e.g., PG1 on PA8) | Indicator in GUI changes color (green/red) in real-time |
| Send `STATUS` command via UART terminal | Returns dump of all 28 pins with direction (IN/OUT) and state |
| WebSocket disconnect | GUI shows "Disconnected", all indicators turn gray, auto-reconnect in 3 sec |
| Invalid command (`SET INVALID_PIN ON`) | Response: `ERR INVALID_PIN`, GUI unchanged |
| Attempt to set input pin (`SET PIN_PA8 ON`) | Response: `ERR PIN_IS_INPUT`, GUI unchanged |

### 8.2. Non-Functional Tests

| Test | Expected Result |
|------|-----------------|
| Output pin toggle delay | Maximum 10 ms from GUI click to state change |
| Input pin indicator update delay | Maximum 50 ms from board change to GUI update |
| Daemon memory consumption | Maximum 100 MB RAM |
| Firmware size | Maximum 64 KB Flash, 8 KB RAM |
| Operation on Windows 11 and Ubuntu 22.04 | All components work without errors |
| **COM port auto-detection with connection verification** | Daemon checks each port with `PING` command, selects first responding `PONG` |
| HSI clocking | Firmware runs without external crystal (internal RC 8 MHz) |

***

## 9. Limitations and Assumptions

- **PC OS**: Windows 10/11 or Ubuntu 20.04+.
- **Browser**: Chrome, Firefox, Edge (latest versions).
- **UART Adapter**: Any USB-to-UART (CP2102, FTDI, CH340).
- **Network**: Local (localhost), no external access required.
- **Security**: No authentication required (benchtop software in closed environment).

***

## 10. Appendices

### Appendix A: Command and Response Examples

```
Command: SET PIN_PA4 ON
Response:   OK PIN_PA4 ON

Command: GET PIN_PA8
Response:   PIN_PA8 OFF

Command: STATUS
Response:   STATUS PIN_PA4:OUT:ON PIN_PA8:IN:OFF PIN_PC4:OUT:ON ...

Command: PING
Response:   PONG

Command: VERSION
Response:   FW v1.0.0 2026-09-11

Command: SET PIN_PA8 ON  (input pin)
Response:   ERR PIN_IS_INPUT
```

***

## 11. Development Stages (Recommended)

| Stage | Duration | Deliverable |
|-------|----------|-------------|
| 1. STM32 Firmware (Arduino + PlatformIO) | 1-2 days | UART3, GPIO (input/output), command parser, input polling |
| 2. UART Daemon (Python) | 1 day | HTTP + WebSocket + UART bridge, **COM port auto-detection with connection verification** |
| 3. Web Interface (HTML/CSS/JS) | 1-2 days | Diagram + 13 controls + 15 indicators, WebSocket connection |
| 4. Integration and Testing | 1 day | End-to-end tests, debugging |
| 5. Documentation | 0.5 day | README, instructions |

**Total**: 4-6 working days.

***

## 12. Version Control System Requirements (Git)

### 12.1. General Requirements

| Requirement | Value |
|-------------|-------|
| Version Control System | **Git** (mandatory) |
| Repository | Local (on developer PC) or remote (GitHub, GitLab, etc.) |
| Repository Structure | Separate directories for each component: `firmware/`, `daemon/`, `gui/` |

### 12.2. Commit Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| GIT-1 | Initialize Git repository at project start | Mandatory |
| GIT-2 | Commit after each significant code change (new feature, bug fix, protocol change) | Mandatory |
| GIT-3 | Meaningful commit messages (in English or Russian, briefly describing changes) | Mandatory |
| GIT-4 | Tags for firmware versions (e.g., `v1.0.0`, `v1.1.0`) | Mandatory |
| GIT-5 | `CHANGELOG.md` file with change history (optional) | Optional |

**Example Commit Messages:**
```
git commit -m "Initial commit: basic UART3 init and GPIO setup"
git commit -m "Add PING command handler"
git commit -m "Fix STATUS response format for input pins"
git commit -m "Add LED toggle on PA4 (PWR2) control"
git tag v1.0.0
```

### 12.3. Firmware Versioning

| Requirement | Value |
|-------------|-------|
| Version Format | **SemVer** (Semantic Versioning): `MAJOR.MINOR.PATCH` (e.g., `1.0.0`) |
| Version Storage | In firmware code (constant `FW_VERSION`) and in `platformio.ini` |
| `VERSION` Command | Must return string like `FW v1.0.0 2026-09-11` |
| Git Tags | Create tag on each `MAJOR` or `MINOR` version change |
| CHANGELOG | Maintain `CHANGELOG.md` file with version change descriptions (optional) |

**Example (C++):**
```cpp
#define FW_VERSION "1.0.0"
#define FW_DATE "2026-09-11"

// VERSION command handling
if (cmd == "VERSION") {
  Serial3.print("FW v");
  Serial3.print(FW_VERSION);
  Serial3.print(" ");
  Serial3.println(FW_DATE);
}
```

**Example (platformio.ini):**
```ini
[env:nucleo_f103re]
platform = ststm32
board = nucleo_f103re
framework = arduino

build_flags = 
  -D FW_VERSION="1.0.0"
  -D FW_DATE="2026-09-11"
```

### 12.4. Recommended Repository Structure

```
pcb-test-stand/
├── firmware/
│   ├── src/
│   │   └── main.cpp
│   ├── platformio.ini
│   └── README.md
├── daemon/
│   ├── daemon.py
│   ├── requirements.txt
│   └── README.md
├── gui/
│   ├── index.html
│   ├── style.css
│   ├── script.js
│   └── README.md
├── docs/
│   ├── scheme.png
│   └── pinout.md
├── CHANGELOG.md
└── README.md
```

### 12.5. Acceptance Criteria (Git)

| Test | Expected Result |
|------|-----------------|
| Git repository initialized | `git log` shows commit history |
| Commits after code changes | History contains commits with change descriptions |
| Firmware version tags | `git tag` shows `v1.0.0`, `v1.1.0`, etc. |
| `VERSION` command returns current version | Response: `FW v1.0.0 2026-09-11` |

***

**Note for AI Agents:**  
This TS is designed for delegation to separate agents (or a single agent with modular structure). Each section can be used as an independent task:
- Agent 1: STM32 firmware (**Arduino Framework + PlatformIO**, section 3).
- Agent 2: UART daemon (section 4).
- Agent 3: Web interface (section 5).
- Agent 4: Integration and testing (section 8).
- Agent 5: Git setup, commits, versioning (section 12).

If needed, this can be further broken down into more detailed subtasks for each agent.