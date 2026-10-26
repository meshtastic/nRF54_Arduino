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

#include "Arduino.h"
#include "nrf.h"
#include "nrf_cache.h"
#include "nrf_nvic.h"
#include "sd_isr.h"

nrf_nvic_state_t nrf_nvic_state;

#define DFU_MAGIC_SERIAL_ONLY_RESET   0x4e
#define DFU_MAGIC_OTA_RESET           0xA8

// Must match temp register in bootloader
#define BOOTLOADER_VERSION_REGISTER     NRF_TIMER22->CC[0]
uint32_t bootloaderVersion = 0;
static uint32_t _reset_reason = 0;

// Called from Reset_Handler (startup_nrf54l15.S), before the global constructors
void nrf54_cache_enable( void )
{
  nrf_cache_enable(NRF_ICACHE);
}

void init( void )
{
  _reset_reason = NRF_RESET->RESETREAS;

  // clear reset reason (write-1-to-clear on nRF54L)
  NRF_RESET->RESETREAS = _reset_reason;

  // Retrieve bootloader version
  bootloaderVersion = BOOTLOADER_VERSION_REGISTER;

  // The bootloader may have left the LF clock running on another source
  NRF_CLOCK->TASKS_LFCLKSTOP = 1;
  while (NRF_CLOCK->LFCLK.STAT & CLOCK_LFCLK_STAT_STATE_Msk) {}

  // Select Clock Source : XTAL or RC (nRF54L uses LFCLK.SRC register)
#if defined( USE_LFXO )
  NRF_CLOCK->LFCLK.SRC = (CLOCK_LFCLK_SRC_SRC_LFXO << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk;
#elif defined(USE_LFRC)
  NRF_CLOCK->LFCLK.SRC = (CLOCK_LFCLK_SRC_SRC_LFRC << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk;
#else
  #error Clock Source is not configured, define USE_LFXO or USE_LFRC according to your board
#endif

  NRF_CLOCK->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;

  sd_isr_boot_init();

  // No RTC1 on nRF54L (uses GRTC instead). Bootloader timer already stopped.

  // Make sure all pin is set HIGH when pinmode() is called
  NRF_P0->OUTSET = UINT32_MAX;
#ifdef NRF_P1
  NRF_P1->OUTSET = UINT32_MAX;
#endif
#ifdef NRF_P2
  NRF_P2->OUTSET = UINT32_MAX;
#endif
}

uint32_t readResetReason(void)
{
  return _reset_reason;
}

static void reset_mcu(uint32_t gpregret)
{
  // disable SD
  sd_softdevice_disable();

  // Disable all interrupts (nRF54L has 8 NVIC registers)
  for (int i = 0; i < 8; i++)
  {
    NVIC->ICER[i]=0xFFFFFFFF;
    NVIC->ICPR[i]=0xFFFFFFFF;
  }

  NRF_POWER->GPREGRET[0] = gpregret;

  NVIC_SystemReset();

  while(1) {}
}

void enterSerialDfu(void)
{
  // shut down everything
  reset_mcu(DFU_MAGIC_SERIAL_ONLY_RESET);
}

void enterOTADfu(void)
{
  reset_mcu(DFU_MAGIC_OTA_RESET);
}

void waitForEvent(void)
{
#if 0
  // Set bit 7 and bits 4..0 in the mask to one (0x ...00 1001 1111)
  enum { FPU_EXCEPTION_MASK = 0x0000009F };

  /* Clear exceptions and PendingIRQ from the FPU unit */
  __set_FPSCR(__get_FPSCR()  & ~(FPU_EXCEPTION_MASK));
  (void) __get_FPSCR();
  NVIC_ClearPendingIRQ(FPU_IRQn);
#endif

  /* S145 does not provide sd_app_evt_wait(); use WFE directly. */
  __WFE();
  __SEV(); // Clear Event Register.
  __WFE();
}


void systemOff(uint32_t pin, uint8_t wake_logic)
{
//  for(int i=0; i<8; i++)
//  {
//    NRF_POWER->RAM[i].POWERCLR = 0x03UL;
//  }

  pin = g_ADigitalPinMap[pin];

  if ( wake_logic )
  {
    nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
  }else
  {
    nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  }

  // s145 has no sd_power_system_off(); REGULATORS is not SoftDevice-restricted
  NRF_REGULATORS->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Enter;
}


float readCPUTemperature( void )
{
  uint8_t en;
  int32_t temp;
  (void) sd_softdevice_is_enabled(&en);
  if (en) 
  {
    sd_temp_get(&temp);
  }
  else
  {
    NRF_TEMP->EVENTS_DATARDY = 0x00; // Only needed in case another function is also looking at this event flag
    NRF_TEMP->TASKS_START = 0x01; 
  
    while (!NRF_TEMP->EVENTS_DATARDY);
    temp = NRF_TEMP->TEMP;                      // Per anomaly 29 (unclear whether still applicable), TASKS_STOP will clear the TEMP register.

    NRF_TEMP->TASKS_STOP = 0x01;           // Per anomaly 30 (unclear whether still applicable), the temp peripheral needs to be shut down
    NRF_TEMP->EVENTS_DATARDY = 0x00;
  }
  return temp / 4.0F;
}
