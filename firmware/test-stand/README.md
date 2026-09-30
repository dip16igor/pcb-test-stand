# STM32 Firmware

GPIO + UART control firmware for the custom STM32F103RE board
(spec §3). Listens on UART3, executes text commands, reports pin states.

## Hardware

- Custom PCB, STM32F103RE (LQFP, 512 KB Flash / 64 KB RAM).
- No external crystal: runs on internal RC (HSI 8 MHz → PLL ×16 = **64 MHz**).
  72 MHz is unreachable from HSI on F1 (needs HSE); the daemon/GUI don't care.
- UART3 on **PC10 (TX) / PC11 (RX)**, 115200 8N1, to a USB-to-UART adapter.
- Flash/debug via ST-Link (SWD).

## Build

VS Code + PlatformIO extension, then build. Or CLI:

```bat
%USERPROFILE%\.platformio\penv\Scripts\pio.exe run -e genericSTM32F103RE
```

Build: `pio run -e genericSTM32F103RE`. Upload: `pio run -e genericSTM32F103RE -t upload` (ST-Link).

## Pin table (spec §3.3, PB3 = OUT per clarification)

Outputs (GUI checkboxes, reset to LOW): PA4 PWR2, PA5 EN_7V, PA6 EN_12V,
PA7 EN_24V, PA11 CE1, PB3 CE2, PB8 LED1, PB9 LED2, PC4 PWR1, PC5 EN,
PC13 POWER1, PC14 POWER2 (open-drain active-LOW: ON = sinking LOW, OFF = released; resets ON), PC15 EN_24V2, PD0 LED0.

Inputs (GUI LEDs, pull-up; KEY on PA0 is active-HIGH with external 100k
pull-down, button to VCC): PA0 KEY, PA8 PG1, PA12 STAT2_1, PA15 STAT1_1,
PB0 ALERT, PB1 PGOOD, PB2 PGOOD2, PB15 COMP2, PC2 PG, PC3 spare, PC8 COMP1,
PC9 STAT2_2, PC12 PG2, PD1 spare (PD01 remap), PD2 STAT1_2.

## Protocol (spec §3.5)

Uppercase, `PIN_<PORT><NUMBER>`, lines end `\n`, `\r`, or `\r\n`.
Oversize lines (>127 chars) are discarded to the next newline.

> Bray's Terminal sends CR on Enter in some configs and nothing in others;
> if Enter gets no answer, append `$0D` (CR) or `$0A` (LF) to the command
> or use the Send button — both terminators are proven on hardware.

```
SET PIN_PA4 ON    ->  OK PIN_PA4 ON | ERR INVALID_PIN | ERR PIN_IS_INPUT
GET PIN_PA8       ->  PIN_PA8 OFF  | ERR INVALID_PIN
STATUS            ->  STATUS PIN_PA4:OUT:ON PIN_PA8:IN:OFF ...
STATE             ->  STATE POWER_OFF | STATE POWERING_ON | STATE POWER_ON | STATE POWERING_OFF
PWM PIN_PC6 50   ->  OK PIN_PC6 50 | ERR INVALID_PIN | ERR INVALID_ARG
PWM              ->  PWM PIN_PC6:0 PIN_PC7:0
INTERLOCK OFF     ->  OK INTERLOCK OFF | query INTERLOCK -> INTERLOCK ON
PING              ->  PONG
VERSION           ->  FW v1.4.0 2026-09-29
PWM outputs (v1.4.0): PC6 (TIM3_CH1) + PC7 (TIM3_CH2), 7812.5 Hz, 12-bit duty
(ARR 4095, prescaler 2 at 64 MHz). Duty in percent 0..100, 1% step.
GUI sliders send `PWM <PIN> <0..100>`; bare `PWM` dumps both duties.
Interlock (v1.2.0, FW-9, default ON): SETting POWER1/POWER2 or PWR1/PWR2 ON
forces the partner LOW first; OFF needs nothing. GUI header checkbox
toggles the mode; terminal users get the same protection automatically.

Power sequencer (v1.3.0): KEY (PA0, active-HIGH, external 100k pull-down)
hold drives staged power. `POWER_OFF` + 50 ms hold starts the chain with 500 ms
between rails: POWER1 → EN_24V2 → EN → PWR1 → EN_24V → EN_12V → EN_7V,
then `STATE POWER_ON` (announced as `STATE POWER_ON`). `POWER_ON` + 4 s hold
announces `STATE POWER_OFF` immediately; the seven rails drop 2 s later
(`POWERING_OFF` in between). Non-blocking (millis steps), works through the
interlock, manual SET still overrides any rail. GUI header shows the state.

`VSYS` (PA1, 47k/4k7), `24V_IN1` (PA2, 47k/4k7), `24V_IN2` (PA3, 47k/4k7),
`5V5_IN` (PC0, 100k/100k). Every `ADC` frame starts by sampling the internal
bandgap (`AVREF`, VREFINT 1.20 V typ): VDDA = 1.20 * 4095 / raw, and all rail
voltages scale from that VDDA instead of a fixed 3.3 V (trailing `VCC:` field
shows it). Residual absolute error is the VREFINT part spread (typ ±3%).
Verify against a meter on first run — divider tolerances shift readings.

Unknown commands return `ERR UNKNOWN_COMMAND`. Inputs are read live on
every `GET`/`STATUS` (no stale cache); the daemon polls at 10 Hz.

## Version

`FW_VERSION` / `FW_DATE` come from `build_flags` (spec §12.3).
Tag releases: `git tag v1.x.y`.
