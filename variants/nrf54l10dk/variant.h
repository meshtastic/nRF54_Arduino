/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_NRF54L10DK_
#define _VARIANT_NRF54L10DK_

/** Master clock frequency */
#define VARIANT_MCK       (128000000ul)

#define USE_LFXO      // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

/*
 * nRF54L15 DK Pin Map
 *
 * Arduino pin 0..31 = P0.00..P0.31 (not all bonded out)
 * Arduino pin 32..63 = P1.00..P1.31
 * Arduino pin 64..95 = P2.00..P2.31
 *
 * The g_ADigitalPinMap[] in variant.cpp maps Arduino pin indices
 * to physical GPIO numbers (port * 32 + pin).
 */

// Number of pins defined in PinDescription array
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (8)
#define NUM_ANALOG_OUTPUTS   (0)

/*
 * LEDs — nRF54L15 DK
 * LED0: P1.10 (active low)
 * LED1: P1.14 (active low)
 * LED2: P1.08 (active low)
 * LED3: P1.13 (active low)
 */
#define PIN_LED1             (42)  // P1.10 = Arduino pin 32+10
#define PIN_LED2             (46)  // P1.14 = Arduino pin 32+14
#define PIN_LED3             (40)  // P1.08 = Arduino pin 32+8
#define PIN_LED4             (45)  // P1.13 = Arduino pin 32+13

#define LED_BUILTIN          PIN_LED1
#define LED_CONN             PIN_LED2

#define LED_RED              PIN_LED1
#define LED_BLUE             PIN_LED2

#define LED_STATE_ON         0         // State when LED is lit (active low)

/*
 * Buttons — nRF54L15 DK
 * Button 0: P1.13
 * Button 1: P1.09
 * Button 2: P1.10  (shared with LED0)
 * Button 3: P1.14  (shared with LED1)
 */
#define PIN_BUTTON1          (45)  // P1.13
#define PIN_BUTTON2          (41)  // P1.09
#define PIN_BUTTON3          (42)  // P1.10
#define PIN_BUTTON4          (46)  // P1.14

/*
 * Analog pins — SAADC inputs
 * nRF54L15 AIN0..AIN7 map to specific GPIO pins
 */
#define PIN_A0               (1)   // P0.01 / AIN0
#define PIN_A1               (2)   // P0.02 / AIN1
#define PIN_A2               (3)   // P0.03 / AIN2
#define PIN_A3               (4)   // P0.04 / AIN3
#define PIN_A4               (5)   // P0.05 / AIN4
#define PIN_A5               (6)   // P0.06 / AIN5
#define PIN_A6               (7)   // P0.07 / AIN6
#define PIN_A7               (8)   // P0.08 / AIN7

static const uint8_t A0  = PIN_A0;
static const uint8_t A1  = PIN_A1;
static const uint8_t A2  = PIN_A2;
static const uint8_t A3  = PIN_A3;
static const uint8_t A4  = PIN_A4;
static const uint8_t A5  = PIN_A5;
static const uint8_t A6  = PIN_A6;
static const uint8_t A7  = PIN_A7;
#define ADC_RESOLUTION    14

// Other pins
#define PIN_AREF           (0xff)  // No external AREF on nRF54L

/*
 * Serial interfaces
 */

// UART0 — connected to J-Link CDC via VCOM
#define PIN_SERIAL1_RX      (34)  // P1.02
#define PIN_SERIAL1_TX      (33)  // P1.01

// UART1 — Arduino header pins (optional)
#define PIN_SERIAL2_RX      (36)  // P1.04
#define PIN_SERIAL2_TX      (35)  // P1.05

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO         (10)  // P0.10
#define PIN_SPI_MOSI         (9)   // P0.09
#define PIN_SPI_SCK          (11)  // P0.11

static const uint8_t SS   = 12;    // P0.12
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

/*
 * Wire Interfaces
 *
 * TWIM22, as on the nRF54L15 boards. The default NRF_TWIM0 alias is TWIM20,
 * which shares SERIAL20 with UARTE20 (Serial2): both would define
 * SERIAL20_IRQHandler, and a sketch using Wire wouldn't link.
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (37)  // P1.05  (or adjust per DK routing)
#define PIN_WIRE_SCL         (38)  // P1.06

#define WIRE_TWIM            NRF_TWIM22
#define WIRE_TWIS            NRF_TWIS22
#define WIRE_IRQN            SERIAL22_IRQn
#define WIRE_IRQ_HANDLER     SERIAL22_IRQHandler

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
