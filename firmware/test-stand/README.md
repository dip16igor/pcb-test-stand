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

Environments (`firmware/test-stand/platformio.ini`):

| env | target | use |
|---|---|---|
| `genericSTM32F103RE` | custom PCB | production, UART3 on PC10/PC11, full 27-pin table |
| `bluepill_f103c8` | BluePill | logic/protocol testing, UART3 on PB10/PB11, 18-pin subset (PC2–PC12, PD0/PD2 don't exist on LQFP48 → `ERR INVALID_PIN`); releases JTAG so PA15/PB3 work as GPIO, SWD kept |

Upload: `pio run -e <env> -t upload` (ST-Link).

## Pin table (spec §3.3, PB3 = OUT per clarification)

Outputs (GUI checkboxes, reset to LOW): PA4 PWR2, PA5 EN_7V, PA6 EN_12V,
PA7 EN_24V, PA11 CE1, PB3 CE2, PB8 LED1, PB9 LED2, PC4 PWR1, PC5 EN,
PC13 POWER1, PC14 POWER2, PC15 EN_24V2, PD0 LED0.

Inputs (GUI LEDs, pull-up): PA8 PG1, PA12 STAT2_1, PA15 STAT1_1, PB0 ALERT,
PB1 PGOOD, PB2 PGOOD2, PB15 COMP2, PC2 PG, PC3 KEY, PC8 COMP1, PC9 STAT2_2,
PC12 PG2, PD2 STAT1_2.

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
PING              ->  PONG
VERSION           ->  FW v1.0.0 2026-09-11
```

Unknown commands return `ERR UNKNOWN_COMMAND`. Inputs are read live on
every `GET`/`STATUS` (no stale cache); the daemon polls at 10 Hz.

## Version

`FW_VERSION` / `FW_DATE` come from `build_flags` (spec §12.3).
Tag releases: `git tag v1.x.y`.
