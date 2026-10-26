# Changelog

## 0.5.0 — 2026-10-26

Requires s145 10.0.1, bootloader 0.5.0 and a platform with the 10.0.1 boards.
Boards on s145 9.0.0 can't update by DFU: reflash the SoftDevice, the
bootloader and the application over SWD once. Without a chip erase, InternalFS
(config, BLE bonds) is kept.

- s145 10.0.1 on nRF54L05/L10/L15: SoftDevice base 0x5A800 / 0xDA800 /
  0x15A800, linker scripts `nrf54l{05,10,15}_s145_v10.ld` (the v9 scripts are
  gone), SoftDevice files split by SoC family
  (`s145_nrf54l_10.0.1_API/`, `bootloader/s145/10.0.1/nrf54l/`).
  `NRF_EVT_RADIO_CANCELED` is gone from the API and the SoC events after it
  are renumbered
- RRAM instruction cache on from `Reset_Handler`: code runs 3.4-3.9x faster
- `SystemCoreClock` reports the 128 MHz the PLL runs at; `delayMicroseconds()`
  is accurate and `micros()` on the DWT cycle counter scales by it. Known:
  `pulseIn()` reports about 0.4x the real width
- nRF54Crypto runs the TRNG only while a draw takes from it, one draw at a
  time, with a new conditioning key each; `begin()`/`end()` no longer start
  or stop it
- LESC pairing is refused without a key pair, and a failed ECDH is answered
  with a DHKey failure
- MDK 8.75.1, with the nRF54LM20A device headers

## 0.3.1 — 2026-10-05

- SoftDevice RNG seeded from the CRACEN TRNG on every request
- Tickless idle hang fixed (GRTC `TIMEOUT`/`WAKETIME` set), plus five more
  nRF54L15 bring-up fixes
- FreeRTOS tick on exact 1/1024 s boundaries
- `SoftwareSerial::stopListening()` no longer masks the GPIOTE channel
- InternalFS counts the cache pages it drops after repeated flush failures

## 0.3.0 — 2026-09-14

- Documentation describes the s145 layout (application at `0x8000`, SoftDevice
  interrupt forwarding, `__flash_arduino_start`/`__flash_arduino_end`) and
  LESC PIN pairing
- `wiring_analog_nRF52.c`, the nRF52 address gates in InternalFileSystem and
  eleven unreferenced `nrf54l_compat.h` aliases removed

## 0.2.1 — 2026-09-14

- BLE pairing with a PIN keeps LE Secure Connections: s145 rates legacy passkey
  pairing as unauthenticated (security level 2), which locked out every
  characteristic that requires MITM protection
- CRACEN RNG restarted on demand for the LESC key agreement and accepted in its
  standby state; a halted generator is soft-reset
- Heap size reported up to the stack limit, matching `_sbrk`

## 0.2.0 — 2026-09-13

Boots and runs on real nRF54L15 hardware (XIAO nRF54L15, nRF54L15-DK) with the
[caveman99/nRF54_Bootloader](https://github.com/caveman99/nRF54_Bootloader) 0.2.0
layout: bootloader at 0x0, application at 0x8000, s145 at the top of RRAM.

- s145 has no MBR: full nRF54L15 vector table in the startup file, SoftDevice
  interrupt and SVC forwarding (`cores/nRF5/nordic/sd_isr.S`), SoftDevice reset
  handler run once at init
- Linker scripts moved into `cores/nRF5/linker`; RAM ends below the bootloader
  NOINIT area and double-reset marker at the top of RAM; `__bootloader_addr`
  dropped, InternalFS bounded by `__flash_arduino_end`
- FreeRTOS tick on GRTC group 0 with the SYSCOUNTER started by the core and the
  tick base taken at scheduler start (the counter survives resets)
- Bluefruit: SoftDevice interrupt forwarding enabled before
  `sd_softdevice_enable`, valid `hfint_ctiv`, GRTC AUTOEN, advertising set count,
  CRACEN-seeded `sd_rand_seed_set`, failing VERIFY status recorded
- BLE DFU enters the bootloader through a reset with GPREGRET 0xA8
- RRAM writes through nrfx_rramc while the SoftDevice is disabled
- Wire: PSEL set before enable, NACK detection from ERRORSRC, address-only
  writes and STOP handling matching the nRF54L TWIM, bounded waits
- Uart: variants select the UARTE instance; nRF54L15-DK pin map corrected
- Heap bounded by the stack limit, GPIOTE interrupt group from the MDK,
  SoftwareSerial register access, default `File()` constructor,
  `NRF_REGULATORS->SYSTEMOFF` instead of the missing `sd_power_system_off`

## 0.1.0 — 2026-05-12

Initial fork from
[Adafruit_nRF52_Arduino](https://github.com/adafruit/Adafruit_nRF52_Arduino),
reworked for Nordic nRF54L05/L10/L15 with s145 SoftDevice 9.0.0.
PlatformIO-only consumption via
[caveman99/platform-nordicnrf54](https://github.com/caveman99/platform-nordicnrf54).

Highlights:

- nRF54L MDK headers, s145 9.0.0 SoftDevice API and SoftDevice hex
- Linker scripts for L05/L10/L15 reserving 28 KB for InternalFS
- FreeRTOS port running off the GRTC peripheral (nRF54L has no SysTick)
- `nrf54l_compat.h` aliasing nRF52 peripheral names to nRF54L
  equivalents so most Adafruit core code compiles unmodified
- `wiring_analog_nRF54L.c` — fresh SAADC implementation accounting for
  nRF54L's 0.9 V internal reference, multiplier gain enum, microsecond
  TACQ and byte-count MAXCNT
- InternalFileSystem and `flash_nrf5x.c` consume `__flash_arduino_start`
  / `__bootloader_addr` linker symbols (no per-chip ifdefs in code)
- Bluefruit54Lib: HID detached from TinyUSB; data-length update reply
  capped by configured BANDWIDTH
- Five board variants: `xiao_nrf54l15`, `xiao_nrf54l15_sense`,
  `nrf54l15dk`, `nrf54l10dk`, `nrf54l05dk`
- XIAO variants route `Wire` to TWIM22 (dedicated TWI controller),
  avoiding the SERIAL00/SERIAL20 fabric shared with SPI/UARTE
- nRF52-only submodules (`Adafruit_TinyUSB_Arduino`,
  `Adafruit_nRFCrypto`) removed — nRF54L has no USB and uses CRACEN
  instead of CC310
- Bootloader hex sourced from the separate
  [nRF54_Bootloader](https://github.com/caveman99/nRF54_Bootloader)
  PlatformIO package (this repo ships only the SoftDevice hex)
