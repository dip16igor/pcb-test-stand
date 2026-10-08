#include <Arduino.h>

// Fallback version/date if not passed via build_flags (see platformio.ini, spec §12.3).
#ifndef FW_VERSION
#define FW_VERSION "1.5.0"
#endif
#ifndef FW_DATE
#define FW_DATE "2026-10-08"
#endif

// UART3 115200 8N1 (spec §2.3, §3.5).
// Custom PCB: PC10 (TX) / PC11 (RX) — needs AFIO remap from default PB10/PB11,
// done via setTx/setRx before begin().
#define PC_UART_TX PC10
#define PC_UART_RX PC11
static constexpr uint32_t UART_BAUD = 115200;
static constexpr size_t LINE_BUF_SIZE = 512;  // full 29-pin STATUS is ~470 chars

struct PinEntry {
  const char *name;   // protocol name, e.g. "PIN_PA4"
  uint32_t arduinoPin;
  bool isOutput;
};

// Pin table per spec §3.3, with user clarification PB3 = OUT (CE2).
// Order kept stable: used for STATUS dump order.
static const PinEntry kPins[] = {
  {"PIN_PA0",  PA0,  false},  // KEY (active-HIGH: external 100k pull-down, button to VCC)
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
  {"PIN_PC2",  PC2,  false},  // PG
  {"PIN_PC3",  PC3,  false},  // spare input
  {"PIN_PC4",  PC4,  true},   // PWR1
  {"PIN_PC5",  PC5,  true},   // EN
  {"PIN_PC8",  PC8,  false},  // COMP1
  {"PIN_PC9",  PC9,  false},  // STAT2_2
  {"PIN_PC12", PC12, false},  // PG2
  {"PIN_PC13", PC13, true},   // POWER1
  {"PIN_PC14", PC14, true},   // POWER2
  {"PIN_PC15", PC15, true},   // EN_24V2
  {"PIN_PD0",  PD0,  true},   // LED0
  {"PIN_PD1",  PD1,  false},  // spare input (PD01 remap)
  {"PIN_PD2",  PD2,  false},  // STAT1_2
};
static constexpr size_t kNumPins = sizeof(kPins) / sizeof(kPins[0]);

// PWM outputs (v1.4.0): PC6 = TIM3_CH1, PC7 = TIM3_CH2 (full remap).
// 64 MHz TIM3CLK / prescaler 2 / ARR 4095 -> 7812.5 Hz, 12-bit duty.
// Duty stored as percent 0..100; ticks = pct * 4095 / 100.
static constexpr uint32_t PWM_ARR = 4095;
static constexpr uint32_t PWM_PRESCALER = 2;
struct PwmEntry {
  const char *name;   // protocol name, e.g. "PIN_PC6"
  uint32_t channel;   // timer channel 1..4
  uint8_t percent;    // duty 0..100
};
static PwmEntry kPwm[] = {
  {"PIN_PC6", 1, 0},  // TIM3_CH1
  {"PIN_PC7", 2, 0},  // TIM3_CH2
};
static constexpr size_t kNumPwm = sizeof(kPwm) / sizeof(kPwm[0]);
static HardwareTimer *pwmTimer = nullptr;

static PwmEntry *findPwm(const char *name) {
  for (size_t i = 0; i < kNumPwm; i++) {
    if (strcmp(kPwm[i].name, name) == 0) return &kPwm[i];
  }
  return nullptr;
}

static void pwmApply(PwmEntry *e) {
  uint32_t ticks = ((uint32_t)e->percent * PWM_ARR + 50) / 100;
  pwmTimer->setCaptureCompare(e->channel, ticks, TICK_COMPARE_FORMAT);
}

static void pwmInit() {
  pwmTimer = new HardwareTimer(TIM3);
  pwmTimer->setPrescaleFactor(PWM_PRESCALER);
  pwmTimer->setOverflow(PWM_ARR, TICK_FORMAT);
  pwmTimer->setMode(1, TIMER_OUTPUT_COMPARE_PWM1, PC6);
  pwmTimer->setMode(2, TIMER_OUTPUT_COMPARE_PWM1, PC7);
  for (size_t i = 0; i < kNumPwm; i++) pwmApply(&kPwm[i]);
  pwmTimer->resume();
}

static void sendPwm() {
  Serial3.print(F("PWM"));
  for (size_t i = 0; i < kNumPwm; i++) {
    Serial3.print(F(" "));
    Serial3.print(kPwm[i].name);
    Serial3.print(F(":"));
    Serial3.print(kPwm[i].percent);
  }
  Serial3.print(F("\n"));
}

// Analog rails (v1.1.0): resistor dividers from the schematic.
// 47k/4k7 -> ratio 11.0 (36 V full scale); 100k/100k -> ratio 2.0.
struct AdcChannel {
  const char *name;
  uint32_t arduinoPin;
  float ratio;
};
static const AdcChannel kAdc[] = {
  {"VSYS",    PA1, 11.00f}, // re-trimmed 2026-09-28, zeners removed: 28.74 read at 24.40 meter
  {"24V_IN1", PA2, 10.93f}, // re-trimmed 2026-09-28, zeners removed: 28.70 read at 24.40 meter
  {"24V_IN2", PA3, 10.84f}, // re-trimmed 2026-09-28, zeners removed: 27.72 read at 23.96 meter
  {"5V5_IN",  PC0,  2.0f},   // voltage divider 100k/100k (re-trim after S/H fix)
};
static constexpr size_t kNumAdc = sizeof(kAdc) / sizeof(kAdc[0]);
static constexpr uint8_t ADC_SAMPLES = 16;  // mean over 16 conversions
static constexpr float ADC_VREF_NOM = 3.3f; // fallback if VREFINT unreadable
static constexpr float ADC_FULL = 4095.0f;  // 12-bit
// F103 VREFINT: 1.20 V typ (datasheet, no factory cal cell on F1).
static constexpr float VREFINT_VOLTS = 1.20f;

// Actual VDDA from the internal bandgap: Vdda = 1.20 * 4095 / raw(VREFINT).
// Measured once per ADC frame; all rail voltages use it instead of a fixed
// 3.3 V, so readings stay correct as the MCU supply drifts. Residual absolute
// error is the VREFINT part-to-part spread (typ ±3%); divider ratios in kAdc
// absorb it at calibration time.
static float readVdda() {
  uint32_t sum = 0;
#ifdef AVREF
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) sum += analogRead(AVREF);
  float raw = (float)sum / ADC_SAMPLES;
  if (raw > 1.0f) return VREFINT_VOLTS * ADC_FULL / raw;
#endif
  return ADC_VREF_NOM;
}

static float readAdcVolts(const AdcChannel &ch, float vdda) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) sum += analogRead(ch.arduinoPin);
  return (float)sum / ADC_SAMPLES / ADC_FULL * vdda * ch.ratio;
}

static void sendAdc() {
  float vdda = readVdda();
  Serial3.print(F("ADC"));
  for (size_t i = 0; i < kNumAdc; i++) {
    Serial3.print(F(" "));
    Serial3.print(kAdc[i].name);
    Serial3.print(F(":"));
    Serial3.print(readAdcVolts(kAdc[i], vdda), 2);
  }
  Serial3.print(F(" VCC:"));
  Serial3.print(vdda, 2);
  Serial3.print(F("\n"));
}

static char lineBuf[LINE_BUF_SIZE];
static size_t lineLen = 0;
static bool lineOverflow = false;

// POWER2 (PC14) is open-drain, active-LOW: logical ON = sinking LOW
// (drain closed), logical OFF = released HIGH-Z.
static bool isPower2(const PinEntry *p) {
  return strcmp(p->name, "PIN_PC14") == 0;
}

// PWR2 (PA4 = DAC_OUT1) ramp: SET ON starts a 0 -> max ramp over
// kDacRampMs, SET OFF kills it to 0 immediately. Non-blocking: dacTask()
// advances one step per loop iteration. analogWrite() handles DAC init
// (12-bit DACC_RESOLUTION); 0..4095 maps to 0..VDDA (~3.3 V).
static constexpr uint32_t kDacRampMs = 2000;  // hardcoded ramp time
static constexpr uint32_t kDacMax = 4095;
static bool dacRunning = false;
static uint32_t dacStartMs = 0;

static void outputOff(const PinEntry *p) {
  if (strcmp(p->name, "PIN_PA4") == 0) {
    dacRunning = false;
    analogWrite(PA4, 0);  // immediate 0, no ramp-down
    return;
  }
  if (isPower2(p)) digitalWrite(p->arduinoPin, HIGH);  // release
  else digitalWrite(p->arduinoPin, LOW);
}

// Logical level: POWER2 reads inverted (sinking LOW = ON); PWR2 reports
// the DAC ramp state (a DAC pin has no meaningful digital readback).
static bool logicalRead(const PinEntry *p) {
  if (strcmp(p->name, "PIN_PA4") == 0) return dacRunning;
  int v = digitalRead(p->arduinoPin);
  if (isPower2(p)) return v == LOW;
  return v == HIGH;
}

static void dacTask() {
  if (!dacRunning) return;
  uint32_t now = millis();
  uint32_t elapsed = now - dacStartMs;
  uint32_t ticks;
  if (elapsed >= kDacRampMs) {
    ticks = kDacMax;
    dacRunning = false;  // ramp complete, hold max
  } else {
    ticks = (elapsed * (kDacMax + 1)) / kDacRampMs;
  }
  analogWrite(PA4, ticks);
}

static const PinEntry *findPin(const char *name) {
  for (size_t i = 0; i < kNumPins; i++) {
    const PinEntry &p = kPins[i];
    if (strcmp(p.name, name) == 0) return &p;
  }
  return nullptr;
}
static void sendStatus() {
  Serial3.print(F("STATUS"));
  for (size_t i = 0; i < kNumPins; i++) {
    const PinEntry &p = kPins[i];
    Serial3.print(F(" "));
    Serial3.print(p.name);
    Serial3.print(p.isOutput ? F(":OUT:") : F(":IN:"));
    Serial3.print(logicalRead(&p) ? F("ON") : F("OFF"));
  }
  Serial3.print(F("\n"));
}

// Output interlock (v1.2.0, FW-9): these pairs must never drive HIGH
// together. Enabled by default; toggled over UART, not persisted.
static bool interlockEnabled = true;
struct InterlockPair {
  const char *a;
  const char *b;
};
static const InterlockPair kInterlock[] = {
  {"PIN_PC13", "PIN_PC14"},  // POWER1 / POWER2
  {"PIN_PC4", "PIN_PA4"},    // PWR1 / PWR2
};
static constexpr size_t kNumInterlock =
    sizeof(kInterlock) / sizeof(kInterlock[0]);

// Power sequencer (v1.3.0): KEY hold drives staged power on/off.
// KEY (PA0) is active-HIGH: external 100k pull-down, button drives VCC.
// Non-blocking: steps run off millis() so UART stays responsive.
static const char *kPowerSeq[] = {
  "PIN_PC13",  // POWER1
  "PIN_PC15",  // EN_24V2
  "PIN_PC5",   // EN
  "PIN_PC4",   // PWR1
  "PIN_PA7",   // EN_24V
  "PIN_PA6",   // EN_12V
  "PIN_PA5",   // EN_7V
};
static constexpr size_t kPowerSeqLen = sizeof(kPowerSeq) / sizeof(kPowerSeq[0]);
static constexpr uint32_t kPowerStepMs = 500;   // rail-to-rail delay
static constexpr uint32_t kKeyOnMs = 500;       // POWER_OFF -> press must exceed this
static constexpr uint32_t kKeyOffMs = 4000;     // POWER_ON -> STATE off, pins 2 s later
static constexpr uint32_t kPinsOffDelayMs = 2000;

enum PowerState : uint8_t { POWER_OFF, POWERING_ON, POWER_ON, POWERING_OFF };
static PowerState powerState = POWER_OFF;
static size_t powerStep = 0;
static uint32_t powerStepDue = 0;
static uint32_t keyPressStart = 0;  // 0 = key idle
static bool keyArmed = true;        // false until release after an action
static uint32_t pinsOffDue = 0;     // POWERING_OFF -> pins LOW deadline

// Drive an output ON through the interlock (partner forced OFF first).
// Shared by SET and the sequencer. POWER2 logic lives in the early helpers
// above (isPower2/outputOff/logicalRead); outputOn inverts the drive here.
static void outputOn(const PinEntry *p) {
  if (strcmp(p->name, "PIN_PA4") == 0) {
    // PWR2 is the DAC ramp: kill any stale level, restart 0 -> max.
    dacRunning = false;
    analogWrite(PA4, 0);
    dacStartMs = millis();
    dacRunning = true;
  }
  if (interlockEnabled) {
    for (size_t i = 0; i < kNumInterlock; i++) {
      const char *partner = nullptr;
      if (strcmp(p->name, kInterlock[i].a) == 0) partner = kInterlock[i].b;
      else if (strcmp(p->name, kInterlock[i].b) == 0) partner = kInterlock[i].a;
      if (partner != nullptr) {
        const PinEntry *q = findPin(partner);
        if (q != nullptr) outputOff(q);
      }
    }
  }
  if (isPower2(p)) digitalWrite(p->arduinoPin, LOW);  // sink
  else if (strcmp(p->name, "PIN_PA4") != 0) digitalWrite(p->arduinoPin, HIGH);
}

static const char *powerStateName() {
  switch (powerState) {
    case POWER_ON: return "POWER_ON";
    case POWERING_ON: return "POWERING_ON";
    case POWERING_OFF: return "POWERING_OFF";
    default: return "POWER_OFF";
  }
}

// LED0 heartbeat: 500 ms period (250 ms ON / 250 ms OFF), starts ON.
// Free-runs off millis(); SET PIN_PD0 still works but is overwritten
// on the next half-period edge.
static constexpr uint32_t kLedHalfMs = 250;
static uint32_t ledDue = 0;
static bool ledOn = true;

static void ledTask() {
  uint32_t now = millis();
  if ((int32_t)(now - ledDue) < 0) return;
  ledDue = now + kLedHalfMs;
  ledOn = !ledOn;
  const PinEntry *p = findPin("PIN_PD0");
  if (p != nullptr) digitalWrite(p->arduinoPin, ledOn ? HIGH : LOW);
}

static void powerTask() {
  const PinEntry *key = findPin("PIN_PA0");  // KEY, active-HIGH
  bool pressed = (key != nullptr) && (digitalRead(key->arduinoPin) == HIGH);
  uint32_t now = millis();
  if (!pressed) {
    keyPressStart = 0;
    keyArmed = true;
  } else if (keyPressStart == 0) {
    keyPressStart = now;
  }
  uint32_t held = (pressed && keyPressStart != 0) ? (now - keyPressStart) : 0;

  if (powerState == POWER_OFF) {
    if (keyArmed && pressed && held >= kKeyOnMs) {
      keyArmed = false;  // require release before the next trigger
      powerState = POWERING_ON;
      powerStep = 0;
      powerStepDue = now;  // first rail now, then every 50 ms
    }
    return;
  }
  if (powerState == POWERING_ON) {
    if ((int32_t)(now - powerStepDue) >= 0) {
      const PinEntry *p = findPin(kPowerSeq[powerStep]);
      if (p != nullptr && p->isOutput) outputOn(p);
      powerStep++;
      powerStepDue = now + kPowerStepMs;
      if (powerStep >= kPowerSeqLen) {
        powerState = POWER_ON;
        Serial3.print(F("STATE POWER_ON\n"));
      }
    }
    return;
  }
  // POWER_ON: 4 s hold flips STATE immediately; pins follow 2 s later.
  if (keyArmed && pressed && held >= kKeyOffMs) {
    keyArmed = false;
    powerState = POWERING_OFF;
    pinsOffDue = now + kPinsOffDelayMs;
    Serial3.print(F("STATE POWER_OFF\n"));
  }
  if (powerState == POWERING_OFF && (int32_t)(now - pinsOffDue) >= 0) {
    for (size_t i = 0; i < kPowerSeqLen; i++) {
      const PinEntry *p = findPin(kPowerSeq[i]);
      if (p != nullptr && p->isOutput) outputOff(p);
    }
    powerState = POWER_OFF;
  }
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
  } else if (strcmp(cmd, "STATE") == 0) {
    Serial3.print(F("STATE "));
    Serial3.print(powerStateName());
    Serial3.print(F("\n"));
  } else if (strcmp(cmd, "ADC") == 0) {
    sendAdc();
  } else if (strcmp(cmd, "PWM") == 0) {
    char *pinName = strtok(nullptr, " \t");
    if (pinName == nullptr) {
      sendPwm();
    } else {
      char *arg = strtok(nullptr, " \t");
      if (arg == nullptr) {
        Serial3.print(F("ERR INVALID_ARG\n"));
        return;
      }
      PwmEntry *e = findPwm(pinName);
      if (e == nullptr) {
        Serial3.print(F("ERR INVALID_PIN\n"));
        return;
      }
      char *end = nullptr;
      long v = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || v < 0 || v > 100) {
        Serial3.print(F("ERR INVALID_ARG\n"));
        return;
      }
      e->percent = (uint8_t)v;
      pwmApply(e);
      Serial3.print(F("OK "));
      Serial3.print(e->name);
      Serial3.print(F(" "));
      Serial3.print(e->percent);
      Serial3.print(F("\n"));
    }
  } else if (strcmp(cmd, "INTERLOCK") == 0) {
    char *arg = strtok(nullptr, " \t");
    if (arg == nullptr) {
      Serial3.print(F("INTERLOCK "));
      Serial3.print(interlockEnabled ? F("ON\n") : F("OFF\n"));
    } else if (strcmp(arg, "ON") == 0) {
      interlockEnabled = true;
      Serial3.print(F("OK INTERLOCK ON\n"));
    } else if (strcmp(arg, "OFF") == 0) {
      interlockEnabled = false;
      Serial3.print(F("OK INTERLOCK OFF\n"));
    } else {
      Serial3.print(F("ERR INVALID_ARG\n"));
    }
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
    bool on = logicalRead(p);
    Serial3.print(p->name);
    Serial3.print(on ? F(" ON\n") : F(" OFF\n"));
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
      // Interlock (v1.2.0): partner goes LOW first, then this pin HIGH
      // (shared outputOn helper, also used by the sequencer).
      outputOn(p);
      Serial3.print(F("OK "));
      Serial3.print(p->name);
      Serial3.print(F(" ON\n"));
    } else if (strcmp(state, "OFF") == 0) {
      outputOff(p);
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
  // stm32duino defaults analogRead to 10-bit (AVR compat); the ADC math
  // below assumes 12-bit (ADC_FULL 4095). Without this every rail reads
  // ~1/4 of the true voltage.
  analogReadResolution(12);
  // PD0/PD1 live on OSC_IN/OSC_OUT: remap them to GPIO (needs HSI, no HSE).
  // Without this PD0 stays a dead oscillator pin (the LED0 fault).
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_PD01_ENABLE();
  // GPIO per spec §3.3: OUT push-pull LOW, IN pull-up — except KEY (PA0,
  // active-HIGH) which has an external 100k pull-down, so plain INPUT,
  // and POWER2 (PC14, open-drain active-LOW) which resets released (OFF).
  // POWER1 resets released (OFF). LED0 (PD0) resets ON: heartbeat starts lit.
  for (size_t i = 0; i < kNumPins; i++) {
    if (kPins[i].isOutput) {
      if (strcmp(kPins[i].name, "PIN_PC14") == 0) {
        pinMode(kPins[i].arduinoPin, OUTPUT_OPEN_DRAIN);
        digitalWrite(kPins[i].arduinoPin, HIGH);  // release = logical OFF
      } else if (strcmp(kPins[i].name, "PIN_PD0") == 0) {
        pinMode(kPins[i].arduinoPin, OUTPUT);
        digitalWrite(kPins[i].arduinoPin, HIGH);  // heartbeat starts ON
      } else if (strcmp(kPins[i].name, "PIN_PA4") == 0) {
        // PWR2 is DAC-driven at runtime; park as GPIO LOW until first SET ON.
        pinMode(kPins[i].arduinoPin, OUTPUT);
        digitalWrite(kPins[i].arduinoPin, LOW);
      } else {
        pinMode(kPins[i].arduinoPin, OUTPUT);
        digitalWrite(kPins[i].arduinoPin, LOW);
      }
    } else if (strcmp(kPins[i].name, "PIN_PA0") == 0) {
      pinMode(kPins[i].arduinoPin, INPUT);
    } else {
      pinMode(kPins[i].arduinoPin, INPUT_PULLUP);
    }
  }
  Serial3.setTx(PC_UART_TX);
  Serial3.setRx(PC_UART_RX);
  Serial3.begin(UART_BAUD);
  pwmInit();  // TIM3 PC6/PC7, 7812.5 Hz, 0% duty
  // Shows on every power-on/reset without any command.
  delay(100);  // let the USB-UART adapter enumerate
  Serial3.print(F("FW v"));
  Serial3.print(FW_VERSION);
  Serial3.print(F(" "));
  Serial3.print(FW_DATE);
  Serial3.println(F(" READY"));
}

void loop() {
  // FW-3/FW-4: line-oriented ASCII protocol, \n or \r\n terminated.
  // FW-5: inputs are digitalRead on demand (always fresh, no stale cache);
  // effective poll rate is bound only by UART command rate (>> 100 Hz capable).
  // FW-3/FW-4: line-oriented ASCII protocol, \n, \r or \r\n terminated
  // (CR-only senders like Bray's Terminal work too; empty halves of \r\n
  // are ignored by handleLine).
  while (Serial3.available()) {
    char c = static_cast<char>(Serial3.read());
    if (c == '\n' || c == '\r') {
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
  powerTask();  // KEY-hold power sequencer (v1.3.0), non-blocking
  ledTask();    // LED0 heartbeat: 500 ms period, starts ON
  dacTask();    // PWR2 DAC ramp: 0 -> max over kDacRampMs
}
