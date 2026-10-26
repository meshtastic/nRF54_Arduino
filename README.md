# nRF54L Arduino Framework (s145 SoftDevice)

Arduino BSP for Nordic Semiconductor nRF54L05 / nRF54L10 / nRF54L15 with the
**s145 SoftDevice 10.0.1**. Derived from
[Adafruit_nRF52_Arduino](https://github.com/adafruit/Adafruit_nRF52_Arduino)
and reworked for nRF54L peripherals.

**PlatformIO only.** This framework is consumed exclusively through the
companion PlatformIO platform
[meshtastic/platform-nordicnrf54](https://github.com/meshtastic/platform-nordicnrf54).
There is no `platform.txt`, `boards.txt`, or Arduino IDE / Board Manager
support, and there are no plans to add any. Use PlatformIO.

## Supported boards

| Variant | MCU | Source |
|---|---|---|
| `xiao_nrf54l15` | nRF54L15 | Seeed Studio XIAO nRF54L15 |
| `xiao_nrf54l15_sense` | nRF54L15 | Seeed Studio XIAO nRF54L15 Sense |
| `nrf54l15dk` | nRF54L15 | Nordic nRF54L15-DK |
| `nrf54l10dk` | nRF54L10 | Nordic nRF54L15-DK with L10 silicon |
| `nrf54l05dk` | nRF54L05 | Nordic nRF54L15-DK with L05 silicon |

Board JSONs and upload tooling live in the
[PlatformIO platform repo](https://github.com/meshtastic/platform-nordicnrf54).

## Installation

```ini
; platformio.ini
[env:xiao_nrf54l15]
platform = https://github.com/meshtastic/platform-nordicnrf54.git
framework = arduino
board = xiao_nrf54l15
```

The platform pulls this framework, the
[nRF54_Bootloader](https://github.com/meshtastic/nRF54_Bootloader) hex
package, and toolchains automatically on first build.

## What's in here

- **s145 SoftDevice 10.0.1** API headers and merge-time hex, split by SoC
  family as upstream `sdk-nrf-bm` does
  (`cores/nRF5/nordic/softdevice/s145_nrf54l_10.0.1_API/`,
  `bootloader/s145/10.0.1/nrf54l/*_softdevice.hex`)
- **nRF54L MDK headers** for L05/L10/L15 application + flpr targets
  (`cores/nRF5/nordic/nrfx/mdk/`)
- **FreeRTOS port** running off the GRTC peripheral (nRF54L has no
  SysTick); see `cores/nRF5/freertos/portable/{GCC,CMSIS}/nrf54l/`
- **SoftDevice interrupt forwarding** (`cores/nRF5/nordic/sd_isr.S`):
  s145 has no MBR, the application owns the vector table and hands the
  SoftDevice its interrupts and SVCs
- **Linker scripts** for all three chips that reserve 28 KB at the top
  of the application FLASH region for InternalFS
  (`cores/nRF5/linker/`)
- **`nrf54l_compat.h`** force-included by the platform's build script.
  Aliases nRF52 peripheral names to their nRF54L equivalents
  (`NRF_PWM0..2` → `NRF_PWM20..22`, `NRF_TWIM0/1` → `NRF_TWIM20/22`,
  `NRF_SPIM0/2` → `NRF_SPIM00/21`, `NRF_UARTE0/1` → `NRF_UARTE00/20`,
  `NRF_GPIOTE` → `NRF_GPIOTE20`, etc.) so most of the Adafruit core code
  compiles unmodified
- **`wiring_analog_nRF54L.c`** — dedicated SAADC implementation for
  nRF54L (different reference voltage, multiplier gains, microsecond
  TACQ, byte-count MAXCNT, structured PSELP)
- **InternalFileSystem (LittleFS)** wired against
  `__flash_arduino_start` / `__flash_arduino_end` linker symbols, so no
  per-chip ifdefs in C++ code
- **Bluefruit54Lib** with HID detached from TinyUSB, plus
  BANDWIDTH-aware data-length-update reply
- **Library wrappers** for SPI, Wire (with per-variant TWIM instance
  override), Servo, SoftwareSerial, RotaryEncoder, PDM, nRF54Crypto
  (CRACEN RNG plus tinycrypt ECDH, replacing nRF52840's CryptoCell)

## Memory map

s145 ships without an MBR. The DFU bootloader owns RRAM `0x0` and the reset
vector, the application starts at `0x8000`, and the SoftDevice sits at the top
of RRAM.

| Chip | Bootloader | App FLASH | InternalFS (LittleFS) | DFU settings | SoftDevice |
|---|---|---|---|---|---|
| nRF54L05 | `0x0 – 0x8000` | `0x8000 – 0x47000` | `0x47000 – 0x4E000` | `0x4F000` | `0x5A800` |
| nRF54L10 | `0x0 – 0x8000` | `0x8000 – 0xC7000` | `0xC7000 – 0xCE000` | `0xCF000` | `0xDA800` |
| nRF54L15 | `0x0 – 0x8000` | `0x8000 – 0x147000` | `0x147000 – 0x14E000` | `0x14F000` | `0x15A800` |

RAM: `0x20000000 – 0x20004800` belongs to the SoftDevice, the application
runs from `0x20004800` to `0x2003FF80`; the last 128 bytes hold the
bootloader's BLE peer data and double-reset marker. The layout matches the
[nRF54_Bootloader](https://github.com/meshtastic/nRF54_Bootloader) linker
scripts.

## Limitations

- **No USB.** nRF54L has no USB peripheral. The Adafruit_TinyUSB_Arduino
  submodule has been removed. Sketches that need a host serial channel
  on the XIAO variants get it through the on-board SAMD11 USB bridge
  over UART, not over native CDC.
- **No `Adafruit_nRFCrypto`.** nRF52840's CryptoCell CC310 is replaced
  by CRACEN on nRF54L. Use `nRF54Crypto/` instead.
- **HardwarePWM and Wire share peripheral fabric on XIAO.** On the XIAO
  variants, `Wire` is routed to TWIM22 (a dedicated TWI controller)
  via a per-variant override so it doesn't compete with SPI/UARTE on
  the SERIAL00/SERIAL20 fabric. See
  [variants/xiao_nrf54l15/variant.h](variants/xiao_nrf54l15/variant.h).
- **PIN pairing uses LE Secure Connections.** s145 rates legacy passkey
  pairing as unauthenticated (security level 2); characteristics with MITM
  permissions need an LESC bond. The key agreement runs on CRACEN/tinycrypt
  via `nRF54Crypto`.

## Credits

- [Adafruit_nRF52_Arduino](https://github.com/adafruit/Adafruit_nRF52_Arduino)
  — the codebase this fork descends from. Core class layout,
  Bluefruit54Lib, Wire, SPI, FreeRTOS integration, InternalFS.
- [NRF54L15-Clean-Arduino-core](https://github.com/lolren/nrf54-arduino-core)
  — sibling project by lolren. Bare-metal (no SoftDevice); we share API
  surface and have ported a handful of fixes (Print compat, BANDWIDTH
  cap behavior, XIAO TWIM22 routing, SAADC MAXCNT-in-bytes).
- [Arduino-nRF5](https://github.com/sandeepmistry/arduino-nRF5) by
  Sandeep Mistry — the original Arduino-style nRF52 BSP that Adafruit's
  was forked from.
- [FreeRTOS](https://www.freertos.org/),
  [LittleFS](https://github.com/littlefs-project/littlefs),
  [nrfx](https://github.com/NordicSemiconductor/nrfx).

## License

Mixed BSD-3-Clause / MIT / LGPL-2.1 depending on file provenance. See
individual headers and `LICENSE`.
