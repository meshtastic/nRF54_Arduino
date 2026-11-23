/*
 * TWI/I2C library for nRF5x
 * Copyright (c) 2015 Arduino LLC. All rights reserved.
 * Copyright (c) 2016 Sandeep Mistry All right reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

extern "C" {
#include <string.h>
}

#include <Arduino.h>
#include <wiring_private.h>

#include "Wire.h"

// TWIM events never arrive on a stuck or floating bus; bound every wait so scans return instead of hanging.
static bool twim_wait(NRF_TWIM_Type * twim, nrf_twim_event_t ev, bool abort_on_error = true)
{
  uint32_t start = micros();
  while (!nrf_twim_event_check(twim, ev) && !(abort_on_error && nrf_twim_event_check(twim, NRF_TWIM_EVENT_ERROR)))
  {
    if ((uint32_t)(micros() - start) > 25000)
    {
      nrf_twim_task_trigger(twim, NRF_TWIM_TASK_STOP);
      return false;
    }
  }
  return true;
}

// Open drain: setting a pin releases the line to the pull-up, clearing it drives the line low.
static void i2c_pins_cfg(uint32_t scl, uint32_t sda, nrf_gpio_pin_dir_t dir)
{
  nrf_gpio_cfg(scl, dir, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_cfg(sda, dir, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
}

// Waits out clock stretching; a line that stays low is a stuck bus.
static bool i2c_release_wait(uint32_t pin)
{
  nrf_gpio_pin_set(pin);
  for (uint32_t us = 0; us < 25000; us++)
  {
    if (nrf_gpio_pin_read(pin)) return true;
    delayMicroseconds(1);
  }
  return false;
}

// TWIM can't put an address on the bus without data (DMA.TX.MAXCNT is 1..0xFFFF), so the address-only
// write that I2C scanners send is clocked out on the GPIOs instead, at up to 100 kHz.
static uint8_t i2c_probe_address(uint32_t scl, uint32_t sda, uint8_t address)
{
  nrf_gpio_pin_set(scl);
  nrf_gpio_pin_set(sda);
  i2c_pins_cfg(scl, sda, NRF_GPIO_PIN_DIR_OUTPUT);

  uint8_t status = 4;
  if (i2c_release_wait(scl) && nrf_gpio_pin_read(sda))
  {
    delayMicroseconds(5);
    nrf_gpio_pin_clear(sda); // START
    delayMicroseconds(5);
    nrf_gpio_pin_clear(scl);

    // 7 address bits, the write bit, then SDA released for the target's ACK
    uint16_t const frame = ((uint16_t) address << 2) | 1;
    bool ack = false;
    bool clocked = true;
    for (uint16_t bit = 0x100; bit && clocked; bit >>= 1)
    {
      if (frame & bit) nrf_gpio_pin_set(sda); else nrf_gpio_pin_clear(sda);
      delayMicroseconds(5);
      clocked = i2c_release_wait(scl);
      if (bit == 1) ack = !nrf_gpio_pin_read(sda);
      delayMicroseconds(5);
      nrf_gpio_pin_clear(scl);
    }

    nrf_gpio_pin_clear(sda); // STOP
    delayMicroseconds(5);
    if (clocked && i2c_release_wait(scl))
    {
      delayMicroseconds(5);
      nrf_gpio_pin_set(sda);
      status = ack ? 0 : 2;
    }
  }

  nrf_gpio_pin_set(scl);
  nrf_gpio_pin_set(sda);
  i2c_pins_cfg(scl, sda, NRF_GPIO_PIN_DIR_INPUT);
  return status;
}

TwoWire::TwoWire(NRF_TWIM_Type * p_twim, NRF_TWIS_Type * p_twis, IRQn_Type IRQn, uint8_t pinSDA, uint8_t pinSCL)
{
  this->_p_twim = p_twim;
  this->_p_twis = p_twis;
  this->_IRQn = IRQn;
  this->_uc_pinSDA = g_ADigitalPinMap[pinSDA];
  this->_uc_pinSCL = g_ADigitalPinMap[pinSCL];
  transmissionBegun = false;
}

void TwoWire::begin(void) {
  //Main Mode
  master = true;

  i2c_pins_cfg(_uc_pinSCL, _uc_pinSDA, NRF_GPIO_PIN_DIR_INPUT);

  nrf_twim_frequency_set(_p_twim, NRF_TWIM_FREQ_100K);
  // nRF54L ignores PSEL writes while the peripheral is enabled
  nrf_twim_pins_set(_p_twim, _uc_pinSCL, _uc_pinSDA);
  nrf_twim_enable(_p_twim);

  NVIC_ClearPendingIRQ(_IRQn);
  NVIC_SetPriority(_IRQn, 3);
  NVIC_EnableIRQ(_IRQn);
}

void TwoWire::begin(uint8_t address) {
  //Secondary mode
  master = false;

  nrf_gpio_cfg(_uc_pinSCL,
               NRF_GPIO_PIN_DIR_INPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_S0S1,
               NRF_GPIO_PIN_NOSENSE);

  nrf_gpio_cfg(_uc_pinSDA,
               NRF_GPIO_PIN_DIR_INPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_S0S1,
               NRF_GPIO_PIN_NOSENSE);

  nrf_twis_address_set(_p_twis, 0, address);
  _p_twis->CONFIG = TWIS_CONFIG_ADDRESS0_Msk;
  nrf_twis_pins_set(_p_twis, _uc_pinSCL, _uc_pinSDA);
  nrf_twis_orc_set(_p_twis, 0xFF);

  nrf_twis_int_enable(_p_twis, TWIS_INTEN_STOPPED_Msk | TWIS_INTEN_ERROR_Msk |
                                TWIS_INTEN_WRITE_Msk | TWIS_INTEN_READ_Msk);

  NVIC_ClearPendingIRQ(_IRQn);
  NVIC_SetPriority(_IRQn, 3);
  NVIC_EnableIRQ(_IRQn);

  nrf_twis_enable(_p_twis);
}

void TwoWire::setClock(uint32_t baudrate) {
  if (master) {
    nrf_twim_disable(_p_twim);

    nrf_twim_frequency_t frequency;

    if (baudrate <= 100000)
    {
      frequency = NRF_TWIM_FREQ_100K;
    }
    else if (baudrate <= 250000)
    {
      frequency = NRF_TWIM_FREQ_250K;
    }
    else
    {
      frequency = NRF_TWIM_FREQ_400K;
    }

    nrf_twim_frequency_set(_p_twim, frequency);
    nrf_twim_enable(_p_twim);
  }
}

void TwoWire::setPins(uint8_t pinSDA, uint8_t pinSCL)
{
    this->_uc_pinSDA = g_ADigitalPinMap[pinSDA];
    this->_uc_pinSCL = g_ADigitalPinMap[pinSCL];
}

void TwoWire::end() {
  if (master)
  {
    nrf_twim_disable(_p_twim);
  }
  else
  {
    nrf_twis_disable(_p_twis);
  }
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool stopBit)
{
  if(quantity == 0)
  {
    return 0;
  }

  size_t byteRead = 0;
  rxBuffer.clear();

  nrf_twim_address_set(_p_twim, address);
  (void) nrf_twim_errorsrc_get_and_clear(_p_twim);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_STOPPED);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);

  nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_RESUME);
  nrf_twim_rx_buffer_set(_p_twim, rxBuffer._aucBuffer, quantity);
  nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_STARTRX);

  if (!twim_wait(_p_twim, NRF_TWIM_EVENT_RXSTARTED)) return 0;
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_RXSTARTED);

  if (!twim_wait(_p_twim, NRF_TWIM_EVENT_LASTRX)) return 0;
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_LASTRX);

  if (stopBit || nrf_twim_event_check(_p_twim, NRF_TWIM_EVENT_ERROR))
  {
    nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);
    nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_STOP);
    if (!twim_wait(_p_twim, NRF_TWIM_EVENT_STOPPED, false)) return 0;
    nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_STOPPED);
  }
  else
  {
    nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_SUSPEND);
    if (!twim_wait(_p_twim, NRF_TWIM_EVENT_SUSPENDED)) return 0;
    nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_SUSPENDED);
  }

  if (nrf_twim_event_check(_p_twim, NRF_TWIM_EVENT_ERROR))
  {
    nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);
  }

  byteRead = rxBuffer._iHead = nrf_twim_rxd_amount_get(_p_twim);

  return byteRead;
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity)
{
  return requestFrom(address, quantity, true);
}

void TwoWire::beginTransmission(uint8_t address) {
  // save address of target and clear buffer
  txAddress = address;
  txBuffer.clear();

  transmissionBegun = true;
}

// Errors:
//  0 : Success
//  1 : Data too long
//  2 : NACK on transmit of address
//  3 : NACK on transmit of data
//  4 : Other error
uint8_t TwoWire::endTransmission(bool stopBit)
{
  transmissionBegun = false ;

  size_t len = txBuffer.available();
  if (len == 0)
  {
    nrf_twim_disable(_p_twim);
    uint8_t status = i2c_probe_address(_uc_pinSCL, _uc_pinSDA, txAddress);
    nrf_twim_enable(_p_twim);
    return status;
  }

  nrf_twim_address_set(_p_twim, txAddress);
  (void) nrf_twim_errorsrc_get_and_clear(_p_twim);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_STOPPED);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_SUSPENDED);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_LASTTX);
  nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_RESUME);

  // Let the shortcut end the transfer at the last byte
  nrf_twim_shorts_set(_p_twim, stopBit ? NRF_TWIM_SHORT_LASTTX_STOP_MASK : NRF_TWIM_SHORT_LASTTX_SUSPEND_MASK);
  nrf_twim_tx_buffer_set(_p_twim, txBuffer._aucBuffer, len);
  nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_STARTTX);

  bool done = twim_wait(_p_twim, stopBit ? NRF_TWIM_EVENT_STOPPED : NRF_TWIM_EVENT_SUSPENDED);
  nrf_twim_shorts_set(_p_twim, 0);
  if (nrf_twim_event_check(_p_twim, NRF_TWIM_EVENT_ERROR) && !nrf_twim_event_check(_p_twim, NRF_TWIM_EVENT_STOPPED))
  {
    // NACK: the bus is still held, stop it (ERRORSRC keeps the cause)
    nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);
    nrf_twim_task_trigger(_p_twim, NRF_TWIM_TASK_STOP);
    twim_wait(_p_twim, NRF_TWIM_EVENT_STOPPED, false);
  }
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_STOPPED);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_SUSPENDED);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_ERROR);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_LASTTX);
  nrf_twim_event_clear(_p_twim, NRF_TWIM_EVENT_TXSTARTED);

  uint32_t error = nrf_twim_errorsrc_get_and_clear(_p_twim);
  if (error & NRF_TWIM_ERROR_ADDRESS_NACK) return 2;
  if (error & NRF_TWIM_ERROR_DATA_NACK) return 3;
  if (error || !done) return 4;
  return 0;
}

uint8_t TwoWire::endTransmission()
{
  return endTransmission(true);
}

size_t TwoWire::write(uint8_t ucData)
{
  // No writing, without begun transmission or a full buffer
  if ( !transmissionBegun || txBuffer.isFull() )
  {
    return 0 ;
  }

  txBuffer.store_char( ucData ) ;

  return 1 ;
}

size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
  //Try to store all data
  for(size_t i = 0; i < quantity; ++i)
  {
    //Return the number of data stored, when the buffer is full (if write return 0)
    if(!write(data[i]))
      return i;
  }

  //All data stored
  return quantity;
}

int TwoWire::available(void)
{
  return rxBuffer.available();
}

int TwoWire::read(void)
{
  return rxBuffer.read_char();
}

int TwoWire::peek(void)
{
  return rxBuffer.peek();
}

void TwoWire::flush(void)
{
  // Do nothing, use endTransmission(..) to force
  // data transfer.
}

void TwoWire::onReceive(void(*function)(int))
{
  onReceiveCallback = function;
}

void TwoWire::onRequest(void(*function)(void))
{
  onRequestCallback = function;
}

void TwoWire::onService(void)
{
  if (nrf_twis_event_check(_p_twis, NRF_TWIS_EVENT_WRITE))
  {
    nrf_twis_event_clear(_p_twis, NRF_TWIS_EVENT_WRITE);

    receiving = true;

    rxBuffer.clear();

    nrf_twis_rx_prepare(_p_twis, rxBuffer._aucBuffer, sizeof(rxBuffer._aucBuffer));
  }

  if (nrf_twis_event_check(_p_twis, NRF_TWIS_EVENT_READ))
  {
    nrf_twis_event_clear(_p_twis, NRF_TWIS_EVENT_READ);

    receiving = false;
    transmissionBegun = true;

    txBuffer.clear();

    if (onRequestCallback)
    {
      onRequestCallback();
    }

    transmissionBegun = false;

    nrf_twis_tx_prepare(_p_twis, txBuffer._aucBuffer, txBuffer.available());
  }

  if (nrf_twis_event_check(_p_twis, NRF_TWIS_EVENT_STOPPED))
  {
    nrf_twis_event_clear(_p_twis, NRF_TWIS_EVENT_STOPPED);

    if (receiving)
    {
      int rxAmount = nrf_twis_rx_amount_get(_p_twis);

      rxBuffer._iHead = rxAmount;

      if (onReceiveCallback)
      {
        onReceiveCallback(rxAmount);
      }
    }
  }

  if (nrf_twis_event_check(_p_twis, NRF_TWIS_EVENT_ERROR))
  {
    nrf_twis_event_clear(_p_twis, NRF_TWIS_EVENT_ERROR);

    (void)nrf_twis_error_source_get_and_clear(_p_twis);

    nrf_twis_task_trigger(_p_twis, NRF_TWIS_TASK_STOP);
  }
}

// Per-variant peripheral instance overrides for Wire/Wire1.
//
// Default mapping uses the legacy nRF52 names that nrf54l_compat.h aliases to
// nRF54L TWIM20/TWIM22 (SERIAL20/22 fabric). Boards that share their SERIALx
// fabric with SPI or UART on the same pins (e.g. XIAO nRF54L15, where SPI and
// Serial1 both want SERIAL00) should pick dedicated TWI controllers in their
// variant.h, e.g.:
//
//     #define WIRE_TWIM       NRF_TWIM22
//     #define WIRE_TWIS       NRF_TWIS22
//     #define WIRE_IRQN       SERIAL22_IRQn
//     #define WIRE_IRQ_HANDLER SERIAL22_IRQHandler
//
// to avoid clashing with the serial-fabric peripherals on the same pins.
#ifndef WIRE_TWIM
  #define WIRE_TWIM           NRF_TWIM0
  #define WIRE_TWIS           NRF_TWIS0
  #define WIRE_IRQN           SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn
  #define WIRE_IRQ_HANDLER    SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler
#endif

#ifndef WIRE1_TWIM
  #define WIRE1_TWIM          NRF_TWIM1
  #define WIRE1_TWIS          NRF_TWIS1
  #define WIRE1_IRQN          SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn
  #define WIRE1_IRQ_HANDLER   SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler
#endif

#if WIRE_INTERFACES_COUNT > 0
TwoWire Wire(WIRE_TWIM, WIRE_TWIS, WIRE_IRQN, PIN_WIRE_SDA, PIN_WIRE_SCL);

extern "C"
{
  void WIRE_IRQ_HANDLER(void)
  {
    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordEnterISR();
    #endif

    Wire.onService();

    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordExitISR();
    #endif
  }
}
#endif

#if WIRE_INTERFACES_COUNT > 1
TwoWire Wire1(WIRE1_TWIM, WIRE1_TWIS, WIRE1_IRQN, PIN_WIRE1_SDA, PIN_WIRE1_SCL);

extern "C"
{
  void WIRE1_IRQ_HANDLER(void)
  {
    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordEnterISR();
    #endif

    Wire1.onService();

    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordExitISR();
    #endif
  }
}
#endif
