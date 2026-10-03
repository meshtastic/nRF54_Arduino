/*
  Copyright (c) 2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.

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

#include <nrf.h>

#include "Arduino.h"
#include "wiring_private.h"
#include "nrf_gpiote.h"

#include <string.h>

/* nRF54L routes each GPIO port to one GPIOTE instance: P0 -> GPIOTE30 (4 channels), P1 -> GPIOTE20
 * (8 channels). P2, the fast port, has no GPIOTE at all, so no pin on it can raise an interrupt.
 * The HAL enables interrupts on INTENSET<GPIOTE_IRQ_GROUP>, so each instance's IRQ line and handler
 * must belong to that group. */
#define GPIOTE20_IRQn_GRP        NRFX_CONCAT_3(GPIOTE20_, GPIOTE_IRQ_GROUP, _IRQn)
#define GPIOTE20_IRQHandler_GRP  NRFX_CONCAT_3(GPIOTE20_, GPIOTE_IRQ_GROUP, _IRQHandler)
#define GPIOTE30_IRQn_GRP        NRFX_CONCAT_3(GPIOTE30_, GPIOTE_IRQ_GROUP, _IRQn)
#define GPIOTE30_IRQHandler_GRP  NRFX_CONCAT_3(GPIOTE30_, GPIOTE_IRQ_GROUP, _IRQHandler)

#define NUMBER_OF_GPIO_TE 8

#ifdef GPIOTE_CONFIG_PORT_Msk
#define GPIOTE_CONFIG_PORT_PIN_Msk (GPIOTE_CONFIG_PORT_Msk | GPIOTE_CONFIG_PSEL_Msk)
#else
#define GPIOTE_CONFIG_PORT_PIN_Msk GPIOTE_CONFIG_PSEL_Msk
#endif

typedef struct {
  NRF_GPIOTE_Type *reg;
  IRQn_Type        irqn;
  uint8_t          nChannels;
  bool             enabled;
  voidFuncPtr      callbacksInt[NUMBER_OF_GPIO_TE];
  bool             callbackDeferred[NUMBER_OF_GPIO_TE];
  int8_t           channelMap[NUMBER_OF_GPIO_TE];
} gpiote_instance_t;

static gpiote_instance_t gpiote20 = { NRF_GPIOTE20, GPIOTE20_IRQn_GRP, 8, false, {0}, {0}, {0} };
static gpiote_instance_t gpiote30 = { NRF_GPIOTE30, GPIOTE30_IRQn_GRP, 4, false, {0}, {0}, {0} };

/* Pick the GPIOTE that can see this GPIO pin, or NULL when none can (P2). */
static gpiote_instance_t *instanceForPin(uint32_t pin)
{
  switch (pin >> 5) {
    case 0:  return &gpiote30;
    case 1:  return &gpiote20;
    default: return NULL;
  }
}

/* Configure I/O interrupt sources */
static void __initialize(gpiote_instance_t *inst)
{
  memset(inst->callbacksInt, 0, sizeof(inst->callbacksInt));
  memset(inst->channelMap, -1, sizeof(inst->channelMap));
  memset(inst->callbackDeferred, 0, sizeof(inst->callbackDeferred));

  NVIC_DisableIRQ(inst->irqn);
  NVIC_ClearPendingIRQ(inst->irqn);
  NVIC_SetPriority(inst->irqn, 3);
  NVIC_EnableIRQ(inst->irqn);
}

/*
 * \brief Specifies a named Interrupt Service Routine (ISR) to call when an interrupt occurs.
 *        Replaces any previous function that was attached to the interrupt.
 *
 * \return Interrupt Mask, 0 when the pin cannot raise interrupts or no channel is free
 */
int attachInterrupt(uint32_t pin, voidFuncPtr callback, uint32_t mode)
{
  if (pin >= PINS_COUNT) {
    return 0;
  }

  pin = g_ADigitalPinMap[pin];

  gpiote_instance_t *inst = instanceForPin(pin);
  if (inst == NULL) {
    return 0; // P2 has no GPIOTE
  }

  if (!inst->enabled) {
    __initialize(inst);
    inst->enabled = true;
  }

  bool deferred = (mode & ISR_DEFERRED) ? true : false;
  mode &= ~ISR_DEFERRED;

  uint32_t polarity;

  switch (mode) {
    case CHANGE:
      polarity = GPIOTE_CONFIG_POLARITY_Toggle;
      break;

    case FALLING:
      polarity = GPIOTE_CONFIG_POLARITY_HiToLo;
      break;

    case RISING:
      polarity = GPIOTE_CONFIG_POLARITY_LoToHi;
      break;

    default:
      return 0;
  }

  // All information for the configuration is known, except the prior values
  // of the config register. Pre-compute the mask and new bits for later use:
  //     CONFIG[n] = (CONFIG[n] & oldRegMask) | newRegBits;
  //
  // Three fields are configured here: PORT/PIN, POLARITY, MODE
  const uint32_t oldRegMask = ~(GPIOTE_CONFIG_PORT_PIN_Msk | GPIOTE_CONFIG_POLARITY_Msk | GPIOTE_CONFIG_MODE_Msk);
  const uint32_t newRegBits =
    ((pin                      << GPIOTE_CONFIG_PSEL_Pos    ) & GPIOTE_CONFIG_PORT_PIN_Msk) |
    ((polarity                 << GPIOTE_CONFIG_POLARITY_Pos) & GPIOTE_CONFIG_POLARITY_Msk) |
    ((GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos    ) & GPIOTE_CONFIG_MODE_Msk    ) ;

  int ch = -1;
  int newChannel = 0;

  // Find channel where pin is already assigned, if any
  for (int i = 0; i < inst->nChannels; i++) {
    if ((uint32_t)inst->channelMap[i] != pin) continue;
    ch = i;
    break;
  }
  // else, find one not already mapped and also not in use by others
  if (ch == -1) {
    for (int i = 0; i < inst->nChannels; i++) {
      if (inst->channelMap[i] != -1) continue;
      if (nrf_gpiote_te_is_enabled(inst->reg, i)) continue;

      ch = i;
      newChannel = 1;
      break;
    }
  }
  // if no channel found, exit
  if (ch == -1) {
    return 0; // no channel available
  }

  inst->channelMap[ch]       = pin;      // harmless for existing channel
  inst->callbacksInt[ch]     = callback; // caller might be updating this for existing channel
  inst->callbackDeferred[ch] = deferred; // caller might be updating this for existing channel

  uint32_t tmp = inst->reg->CONFIG[ch];
  tmp &= oldRegMask;
  tmp |= newRegBits;                 // for existing channel, effectively updates only the polarity
  inst->reg->CONFIG[ch] = tmp;

  // For a new channel, additionally ensure no old events existed, and enable the interrupt
  if (newChannel) {
    inst->reg->EVENTS_IN[ch] = 0;
    // nRF54L GPIOTE splits INTENSET/INTENCLR per IRQ group (INTENSET0,
    // INTENSET1, ...). The HAL routes to the right one based on
    // NRF_GPIOTE_IRQ_GROUP from the MDK interim header.
    nrf_gpiote_int_enable(inst->reg, (1 << ch));
  }

  // Finally, indicate to caller the allocated / updated channel
  return (1 << ch);
}

NRF_GPIOTE_Type *digitalPinToGpiote(uint32_t pin)
{
  if (pin >= PINS_COUNT) {
    return NULL;
  }
  gpiote_instance_t *inst = instanceForPin(g_ADigitalPinMap[pin]);
  return inst ? inst->reg : NULL;
}

/*
 * \brief Turns off the given interrupt.
 */
void detachInterrupt(uint32_t pin)
{
  if (pin >= PINS_COUNT) {
    return;
  }

  pin = g_ADigitalPinMap[pin];

  gpiote_instance_t *inst = instanceForPin(pin);
  if (inst == NULL || !inst->enabled) {
    return;
  }

  for (int ch = 0; ch < inst->nChannels; ch++) {
    if ((uint32_t)inst->channelMap[ch] == pin) {
      nrf_gpiote_int_disable(inst->reg, (1 << ch));
      inst->reg->CONFIG[ch] = 0;
      inst->reg->EVENTS_IN[ch] = 0; // clear any final events

      // now cleanup the rest of the use of the channel
      inst->channelMap[ch] = -1;
      inst->callbacksInt[ch] = NULL;
      inst->callbackDeferred[ch] = false;
      break;
    }
  }
}

static void gpiote_irq(gpiote_instance_t *inst)
{
#if CFG_SYSVIEW
  SEGGER_SYSVIEW_RecordEnterISR();
#endif

  // Read this once (not 8x), as it's a volatile read
  // across the AHB, which adds up to 3 cycles.
  uint32_t const enabledInterruptMask = nrf_gpiote_int_enable_check(inst->reg, ~0u);
  for (int ch = 0; ch < inst->nChannels; ch++) {
    // only process where the interrupt is enabled and the event register is set
    // check interrupt enabled mask first, as already read that IOM value, to
    // reduce delays from AHB (16MHz) reads.
    if ( 0 == (enabledInterruptMask & (1 << ch))) continue;
    if ( 0 == inst->reg->EVENTS_IN[ch]) continue;

    // If the event was set and interrupts are enabled,
    // call the callback function only if it exists,
    // but ALWAYS clear the event to prevent an interrupt storm.
    if (inst->channelMap[ch] != -1 && inst->callbacksInt[ch]) {
      if ( inst->callbackDeferred[ch] ) {
        // Adafruit defer callback to non-isr if configured so
        ada_callback(NULL, 0, inst->callbacksInt[ch]);
      } else {
        inst->callbacksInt[ch]();
      }
    }

    // clear the event
    inst->reg->EVENTS_IN[ch] = 0;
  }
  // Ensure event clear completes before ISR returns
  __DSB(); __NOP();__NOP();__NOP();__NOP();

#if CFG_SYSVIEW
  SEGGER_SYSVIEW_RecordExitISR();
#endif
}

void GPIOTE20_IRQHandler_GRP(void)
{
  gpiote_irq(&gpiote20);
}

void GPIOTE30_IRQHandler_GRP(void)
{
  gpiote_irq(&gpiote30);
}
