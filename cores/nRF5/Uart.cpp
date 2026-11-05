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

#include "Uart.h"
#include "Arduino.h"
#include "wiring_private.h"


void serialEventRun(void)
{
  if (serialEvent1 && Serial1.available() ) serialEvent1();

#if defined(PIN_SERIAL2_RX) && defined(PIN_SERIAL2_TX)
  if (serialEvent2 && Serial2.available() ) serialEvent2();
#endif
}

Uart::Uart(NRF_UARTE_Type *_nrfUart, IRQn_Type _IRQn, uint8_t _pinRX, uint8_t _pinTX)
{
  nrfUart = _nrfUart;
  IRQn = _IRQn;
  uc_pinRX = g_ADigitalPinMap[_pinRX];
  uc_pinTX = g_ADigitalPinMap[_pinTX];
  uc_hwFlow = 0;

  _end_tx_sem = NULL;
  _begun = false;
}

Uart::Uart(NRF_UARTE_Type *_nrfUart, IRQn_Type _IRQn, uint8_t _pinRX, uint8_t _pinTX, uint8_t _pinCTS, uint8_t _pinRTS)
{
  nrfUart = _nrfUart;
  IRQn = _IRQn;
  uc_pinRX = g_ADigitalPinMap[_pinRX];
  uc_pinTX = g_ADigitalPinMap[_pinTX];
  uc_pinCTS = g_ADigitalPinMap[_pinCTS];
  uc_pinRTS = g_ADigitalPinMap[_pinRTS];
  uc_hwFlow = 1;

  _end_tx_sem = NULL;
  _begun = false;
}

void Uart::setPins(uint8_t pin_rx, uint8_t pin_tx)
{
  uc_pinRX = g_ADigitalPinMap[pin_rx];
  uc_pinTX = g_ADigitalPinMap[pin_tx];
}

void Uart::begin(unsigned long baudrate)
{
  begin(baudrate, (uint16_t)SERIAL_8N1);
}

void Uart::begin(unsigned long baudrate, uint16_t config)
{
  // skip if already begun
  if ( _begun ) return;

  nrfUart->PSEL.TXD = uc_pinTX;
  nrfUart->PSEL.RXD = uc_pinRX;

  if (uc_hwFlow == 1) {
    nrfUart->PSEL.CTS = uc_pinCTS;
    nrfUart->PSEL.RTS = uc_pinRTS;
    nrfUart->CONFIG = config | (UARTE_CONFIG_HWFC_Enabled << UARTE_CONFIG_HWFC_Pos);
  } else {
    nrfUart->CONFIG = config | (UARTE_CONFIG_HWFC_Disabled << UARTE_CONFIG_HWFC_Pos);
  }

  uint32_t nrfBaudRate;

  if (baudrate <= 1200) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud1200;
  } else if (baudrate <= 2400) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud2400;
  } else if (baudrate <= 4800) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud4800;
  } else if (baudrate <= 9600) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud9600;
  } else if (baudrate <= 14400) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud14400;
  } else if (baudrate <= 19200) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud19200;
  } else if (baudrate <= 28800) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud28800;
  } else if (baudrate <= 31250) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud31250;
  } else if (baudrate <= 38400) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud38400;
  } else if (baudrate <= 56000) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud56000;
  } else if (baudrate <= 57600) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud57600;
  } else if (baudrate <= 76800) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud76800;
  } else if (baudrate <= 115200) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud115200;
  } else if (baudrate <= 230400) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud230400;
  } else if (baudrate <= 250000) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud250000;
  } else if (baudrate <= 460800) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud460800;
  } else if (baudrate <= 921600) {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud921600;
  } else {
    nrfBaudRate = UARTE_BAUDRATE_BAUDRATE_Baud1M;
  }

  nrfUart->BAUDRATE = nrfBaudRate;

  nrfUart->ENABLE = UARTE_ENABLE_ENABLE_Enabled;

  nrfUart->DMA.TX.PTR = (uint32_t)txBuffer;
  nrfUart->EVENTS_DMA.TX.END = 0x0UL;

  nrfUart->DMA.RX.PTR = (uint32_t)&rxRcv;
  nrfUart->DMA.RX.MAXCNT = 1;
  nrfUart->TASKS_DMA.RX.START = 0x1UL;

  nrfUart->INTENSET = UARTE_INTENSET_DMARXEND_Msk | UARTE_INTENSET_DMATXEND_Msk;

  NVIC_ClearPendingIRQ(IRQn);
  NVIC_SetPriority(IRQn, 3);
  NVIC_EnableIRQ(IRQn);

  _end_tx_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(_end_tx_sem);
  _begun = true;
}

void Uart::end()
{
  NVIC_DisableIRQ(IRQn);

  nrfUart->INTENCLR = UARTE_INTENCLR_DMARXEND_Msk | UARTE_INTENCLR_DMATXEND_Msk;

  nrfUart->EVENTS_RXTO = 0;
  nrfUart->EVENTS_TXSTOPPED = 0;

  nrfUart->TASKS_DMA.RX.STOP = 0x1UL;
  nrfUart->TASKS_DMA.TX.STOP = 0x1UL;

  // Wait for TXSTOPPED event and for RXTO event
  // This is required before disabling UART to fully power down transceiver PHY.
  // Otherwise transceiver will continue to consume ~900uA
  while ( !(nrfUart->EVENTS_TXSTOPPED && nrfUart->EVENTS_RXTO) ) yield();

  nrfUart->ENABLE = UARTE_ENABLE_ENABLE_Disabled;

  nrfUart->PSEL.TXD = 0xFFFFFFFF;
  nrfUart->PSEL.RXD = 0xFFFFFFFF;

  nrfUart->PSEL.RTS = 0xFFFFFFFF;
  nrfUart->PSEL.CTS = 0xFFFFFFFF;

  rxBuffer.clear();

  vSemaphoreDelete(_end_tx_sem);
  _end_tx_sem = NULL;
  _begun = false;
}

void Uart::flush()
{
  if ( _begun ) {
    xSemaphoreTake(_end_tx_sem, portMAX_DELAY);
    xSemaphoreGive(_end_tx_sem);
  }
}

void Uart::IrqHandler()
{
  if (nrfUart->EVENTS_DMA.RX.END)
  {
    nrfUart->EVENTS_DMA.RX.END = 0x0UL;
    if (nrfUart->DMA.RX.AMOUNT)
    {
      rxBuffer.store_char(rxRcv);
    }
    nrfUart->DMA.RX.PTR = (uint32_t)&rxRcv;
    nrfUart->DMA.RX.MAXCNT = 1;
    nrfUart->TASKS_DMA.RX.START = 0x1UL;
  }

  if (nrfUart->EVENTS_DMA.TX.END)
  {
    nrfUart->EVENTS_DMA.TX.END = 0x0UL;
    xSemaphoreGiveFromISR(_end_tx_sem, NULL);
  }
}

int Uart::available()
{
  return rxBuffer.available();
}

int Uart::peek()
{
  return rxBuffer.peek();
}

int Uart::read()
{
  return rxBuffer.read_char();
}

size_t Uart::write(uint8_t data)
{
  return write(&data, 1);
}

size_t Uart::write(const uint8_t *buffer, size_t size)
{
  // end() deletes the semaphore, and there is none before begin()
  if(!_begun || size == 0) return 0;

  size_t sent = 0;

  do
  {
    size_t remaining = size - sent;
    size_t txSize = min(remaining, (size_t)SERIAL_BUFFER_SIZE);

    xSemaphoreTake(_end_tx_sem, portMAX_DELAY);

    memcpy(txBuffer, buffer + sent, txSize);

    nrfUart->DMA.TX.PTR = (uint32_t)txBuffer;
    nrfUart->DMA.TX.MAXCNT = txSize;
    nrfUart->TASKS_DMA.TX.START = 0x1UL;
    sent += txSize;

  } while (sent < size);

  return sent;
}

int Uart::availableForWrite(void) {
  if(!_begun) return 0;

  // UART does not use ring buffer for TX, therefore it is either busy or not
  UBaseType_t available = uxSemaphoreGetCount(_end_tx_sem);
  return available ? SERIAL_BUFFER_SIZE : 0;
}

//------------- Serial1 (UART, nRF54L has no USB CDC) -------------//
// UARTE pin domains are fixed on nRF54L: UARTE00 -> P2, UARTE2x -> P1, UARTE30 -> P0.
// Variants pick the instance matching their pins via SERIALn_UARTE / SERIALn_IRQN / SERIALn_IRQ_HANDLER.
#ifndef SERIAL1_UARTE
#define SERIAL1_UARTE        NRF_UARTE00
#define SERIAL1_IRQN         SERIAL00_IRQn
#define SERIAL1_IRQ_HANDLER  SERIAL00_IRQHandler
#endif

Uart Serial1( SERIAL1_UARTE, SERIAL1_IRQN, PIN_SERIAL1_RX, PIN_SERIAL1_TX );

extern "C"
{
  void SERIAL1_IRQ_HANDLER()
  {
    Serial1.IrqHandler();
  }
}

//------------- Serial2 -------------//
#if defined(PIN_SERIAL2_RX) && defined(PIN_SERIAL2_TX)
#ifndef SERIAL2_UARTE
#define SERIAL2_UARTE        NRF_UARTE20
#define SERIAL2_IRQN         SERIAL20_IRQn
#define SERIAL2_IRQ_HANDLER  SERIAL20_IRQHandler
#endif

Uart Serial2( SERIAL2_UARTE, SERIAL2_IRQN, PIN_SERIAL2_RX, PIN_SERIAL2_TX );

extern "C"
{
  void SERIAL2_IRQ_HANDLER()
  {
    Serial2.IrqHandler();
  }
}
#endif

