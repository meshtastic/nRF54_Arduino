# Bluefruit54Lib

Arduino BLE API for Nordic nRF54L with the **s145 SoftDevice 10.0.1**.

Wraps the SoftDevice's GAP / GATT / SMP SVC calls behind the
peripheral / central / characteristic / service classes that originated
in the Adafruit Bluefruit nRF52 BSP, so existing Bluefruit sketches
mostly port over with no changes.

## Adaptations for s145

The SoftDevice API differs from the nRF52-era s132/s140 in a handful
of places — see `src/BLESecurity.cpp` for the LESC DHKey reply
sec_status arg, the trimmed `sd_ble_gap_sec_info_reply` signature, and
the absence of `p_sign_key` in the keyset struct. CRACEN (via the
sibling [nRF54Crypto](../nRF54Crypto/) library) replaces CryptoCell
CC310 for the LESC key agreement. `BLESecurity::setPIN` uses LESC; s145
rates legacy passkey pairing as unauthenticated (security level 2).

Bandwidth handling is also tightened: `BLEConnection` caps the
data-length update reply at the MTU implied by the configured
`BANDWIDTH_NORMAL` / `_LOW` setting instead of always accepting the
peer's maximum.

## Examples

`examples/` is unchanged from the upstream Adafruit Bluefruit54 layout.
Most peripheral / central sketches compile against nRF54L without
modification; examples that pulled in TinyUSB or
Adafruit_nRFCrypto for CC310-backed LESC are the exceptions and will
need editing.
