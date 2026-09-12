#include <Arduino.h>

// Fallback version/date if not passed via build_flags (see platformio.ini, spec §12.3).
#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif
#ifndef FW_DATE
#define FW_DATE "2026-09-11"
#endif

// UART3 115200 8N1 (spec §2.3, §3.5).
// Custom PCB: PC10 (TX) / PC11 (RX) — needs AFIO remap from default PB10/PB11,
// done via setTx/setRx before begin().
// BluePill test build (-D BLUEPILL_TEST, F103C8 LQFP48): PC10/PC11 don't exist,
// use default PB10 (TX) / PB11 (RX).
#ifdef BLUEPILL_TEST
#define PC_UART_TX PB10
#define PC_UART_RX PB11
#else
#define PC_UART_TX PC10
#define PC_UART_RX PC11
#endif
static constexpr uint32_t UART_BAUD = 115200;
static constexpr size_t LINE_BUF_SIZE = 128;  // spec: min 64 bytes

struct PinEntry {
  const char *name;   // protocol name, e.g. "PIN_PA4"
  uint32_t arduinoPin;
  bool isOutput;
};

// Pin table per spec §3.3, with user clarification PB3 = OUT (CE2).
// Order kept stable: used for STATUS dump order.
static const PinEntry kPins[] = {
  {"PIN_PA4",  PA4,  true},   // PWR2
  {"PIN_PA5",  PA5,  true},   // EN_7V
  {"PIN_PA6",  PA6,  true},   // EN_12V
  {"PIN_PA7",  PA7,  true},   // EN_24V
  {"PIN_PA8",  PA8,  false},  // PG1
  {"PIN_PA11", PA11, true},   // CE1
  {"PIN_PA12", PA12, false},  // STAT2_1
  {"PIN_PA15", PA15, false},  // STAT1_1
  {"PIN_PB0",  PB0,  false},  // ALERT
  {"PIN_PB1",  PB1,  false},  // PGOOD
  {"PIN_PB2",  PB2,  false},  // PGOOD2
  {"PIN_PB3",  PB3,  true},   // CE2 (clarified OUT)
  {"PIN_PB8",  PB8,  true},   // LED1
  {"PIN_PB9",  PB9,  true},   // LED2
  {"PIN_PB15", PB15, false},  // COMP2
// BluePill (LQFP48) has no PC2-PC12: excluded from test build (ERR INVALID_PIN).
#ifndef BLUEPILL_TEST
  {"PIN_PC2",  PC2,  false},  // PG
  {"PIN_PC3",  PC3,  false},  // KEY
  {"PIN_PC4",  PC4,  true},   // PWR1
  {"PIN_PC5",  PC5,  true},   // EN
  {"PIN_PC8",  PC8,  false},  // COMP1
  {"PIN_PC9",  PC9,  false},  // STAT2_2
  {"PIN_PC12", PC12, false},  // PG2
#endif
  {"PIN_PC13", PC13, true},   // POWER1
  {"PIN_PC14", PC14, true},   // POWER2
  {"PIN_PC15", PC15, true},   // EN_24V2
// BluePill (LQFP48) has no PD0/PD2: excluded from test build.
#ifndef BLUEPILL_TEST
  {"PIN_PD0",  PD0,  true},   // LED0
  {"PIN_PD2",  PD2,  false},  // STAT1_2
#endif
};
static constexpr size_t kNumPins = sizeof(kPins) / sizeof(kPins[0]);

static char lineBuf[LINE_BUF_SIZE];
static size_t lineLen = 0;
static bool lineOverflow = false;

static const PinEntry *findPin(const char *name) {
  for (size_t i = 0; i < kNumPins; i++) {
    if (strcmp(kPins[i].name, name) == 0) return &kPins[i];
  }
  return nullptr;
}

static void sendStatus() {
  Serial3.print(F("STATUS"));
  for (size_t i = 0; i < kNumPins; i++) {
    const PinEntry &p = kPins[i];
    int v = digitalRead(p.arduinoPin);
    Serial3.print(F(" "));
    Serial3.print(p.name);
    Serial3.print(p.isOutput ? F(":OUT:") : F(":IN:"));
    Serial3.print(v == HIGH ? F("ON") : F("OFF"));
  }
  Serial3.print(F("\n"));
}

// Mutates `line` in place with strtok; must be NUL-terminated, newline stripped.
static void handleLine(char *line) {
  // Skip empty lines.
  while (*line == ' ' || *line == '\t') line++;
  if (*line == '\0') return;

  char *cmd = strtok(line, " \t");
  if (cmd == nullptr) return;

  if (strcmp(cmd, "PING") == 0) {
    Serial3.print(F("PONG\n"));
  } else if (strcmp(cmd, "VERSION") == 0) {
    Serial3.print(F("FW v"));
    Serial3.print(FW_VERSION);
    Serial3.print(F(" "));
    Serial3.println(FW_DATE);
  } else if (strcmp(cmd, "STATUS") == 0) {
    sendStatus();
  } else if (strcmp(cmd, "GET") == 0) {
    char *pinName = strtok(nullptr, " \t");
    if (pinName == nullptr) {
      Serial3.print(F("ERR INVALID_PIN\n"));
      return;
    }
    const PinEntry *p = findPin(pinName);
    if (p == nullptr) {
      Serial3.print(F("ERR INVALID_PIN\n"));
      return;
    }
    int v = digitalRead(p->arduinoPin);
    Serial3.print(p->name);
    Serial3.print(v == HIGH ? F(" ON\n") : F(" OFF\n"));
  } else if (strcmp(cmd, "SET") == 0) {
    char *pinName = strtok(nullptr, " \t");
    char *state = strtok(nullptr, " \t");
    if (pinName == nullptr || state == nullptr) {
      Serial3.print(F("ERR INVALID_PIN\n"));
      return;
    }
    const PinEntry *p = findPin(pinName);
    if (p == nullptr) {
      Serial3.print(F("ERR INVALID_PIN\n"));
      return;
    }
    if (!p->isOutput) {
      Serial3.print(F("ERR PIN_IS_INPUT\n"));
      return;
    }
    if (strcmp(state, "ON") == 0) {
      digitalWrite(p->arduinoPin, HIGH);
      Serial3.print(F("OK "));
      Serial3.print(p->name);
      Serial3.print(F(" ON\n"));
    } else if (strcmp(state, "OFF") == 0) {
      digitalWrite(p->arduinoPin, LOW);
      Serial3.print(F("OK "));
      Serial3.print(p->name);
      Serial3.print(F(" OFF\n"));
    } else {
      Serial3.print(F("ERR INVALID_PIN\n"));
    }
  } else {
    // FW-7: invalid commands ignored; NAK keeps UART/daemon debugging easy.
    Serial3.print(F("ERR UNKNOWN_COMMAND\n"));
  }
}

void setup() {
#ifdef BLUEPILL_TEST
  // Free PA15/PB3/PB4 (JTAG) as GPIO; SWD on PA13/PA14 keeps working.
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
#endif
  // GPIO per spec §3.3: OUT push-pull LOW, IN pull-up.
  for (size_t i = 0; i < kNumPins; i++) {
    if (kPins[i].isOutput) {
      pinMode(kPins[i].arduinoPin, OUTPUT);
      digitalWrite(kPins[i].arduinoPin, LOW);
    } else {
      pinMode(kPins[i].arduinoPin, INPUT_PULLUP);
    }
  }

  // FW-1: UART3 (custom PCB: PC10/PC11 remapped; BluePill test: PB10/PB11).
  Serial3.setTx(PC_UART_TX);
  Serial3.setRx(PC_UART_RX);
  Serial3.begin(UART_BAUD);
}

void loop() {
  // FW-3/FW-4: line-oriented ASCII protocol, \n or \r\n terminated.
  // FW-5: inputs are digitalRead on demand (always fresh, no stale cache);
  // effective poll rate is bound only by UART command rate (>> 100 Hz capable).
  while (Serial3.available()) {
    char c = static_cast<char>(Serial3.read());
    if (c == '\n') {
      if (!lineOverflow) {
        lineBuf[lineLen] = '\0';
        // Strip trailing \r for \r\n senders.
        if (lineLen > 0 && lineBuf[lineLen - 1] == '\r') {
          lineBuf[lineLen - 1] = '\0';
        }
        handleLine(lineBuf);
      }
      lineLen = 0;
      lineOverflow = false;
    } else if (!lineOverflow) {
      if (lineLen < LINE_BUF_SIZE - 1) {
        lineBuf[lineLen++] = c;
      } else {
        // Oversize line: discard until newline, keeps parser in sync.
        lineOverflow = true;
      }
    }
  }
}
