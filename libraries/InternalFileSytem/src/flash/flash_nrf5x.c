/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "flash_nrf5x.h"
#include "flash_cache.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "delay.h"
#include "rtos.h"
#include "assert.h"
#include "nrfx_rramc.h"


// InternalFS region from the linker script; all flash writes must stay inside it.
extern uint32_t __flash_arduino_start[];
extern uint32_t __flash_arduino_end[];
#define BOOTLOADER_ADDR        ((uint32_t)__flash_arduino_end)

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+
static SemaphoreHandle_t _sem = NULL;
static uint32_t _flash_op_result = NRF_EVT_FLASH_OPERATION_SUCCESS;

flash_nrf5x_stats_t flash_nrf5x_stats;

// The completion semaphore used to be created by fal_erase() only, which always ran before
// fal_program(); with in-place writes a program can be the first operation, so create it here.
static bool ensure_sem (void)
{
  if ( _sem == NULL ) {
    _sem = xSemaphoreCreateBinary();
  }
  return _sem != NULL;
}

void flash_nrf5x_event_cb (uint32_t event)
{
  if ( _sem ) {
    // Record the result, for consumption by fal_erase or fal_program
    // Used to reattempt failed operations
    _flash_op_result = event;

    // Signal to fal_erase or fal_program that our async flash op is now complete
    xSemaphoreGive(_sem);
  }
}

// How many times an operation the SoftDevice reports BUSY is re-issued (5 ms apart)
#define MAX_RETRY 20
// How many times an operation whose completion never arrived is re-issued
#define MAX_TIMEOUT_RETRY 3

// Upper bound for one async flash op: slices x slice length (2 s)
#define FLASH_NRF5X_WAIT_SLICES   100
#define FLASH_NRF5X_WAIT_SLICE_MS 20

// Application hook for SoC events drained here that are not flash completions
// (power-failure warning, RNG seed request, ...). Weak: absent, they are dropped.
void flash_nrf5x_soc_event_hook(uint32_t event) __attribute__((weak));

// When soft device is enabled, flash ops are async
// Eventual success is reported via callback, which we await
static uint32_t wait_for_async_flash_op_completion(uint32_t initial_result)
{
  // If initial result not NRF_SUCCESS, no need to await callback
  // We will pass the initial result (failure) straight through
  int32_t result = initial_result;

  // Operation was queued successfully
  if (initial_result == NRF_SUCCESS) {

    // Wait for the result via callback, but never without a bound: the SoC event normally arrives
    // through the SoftDevice event task, yet a lost event would otherwise park the calling task
    // forever (seen on nRF54L15 during a BLE connection: the whole firmware froze in this take).
    // Drain the SoC event queue ourselves while waiting so the completion cannot get stuck behind
    // a task that is not running, and hand any other SoC event to the application hook.
    bool completed = false;
    flash_nrf5x_stats.in_flight = 1;
    for (uint32_t slice = 0; slice < FLASH_NRF5X_WAIT_SLICES && !completed; slice++) {
      if (xSemaphoreTake(_sem, pdMS_TO_TICKS(FLASH_NRF5X_WAIT_SLICE_MS)) == pdTRUE) {
        completed = true;
        flash_nrf5x_stats.completed++;
        break;
      }
      uint32_t evt;
      while (sd_evt_get(&evt) == NRF_SUCCESS) {
        if (evt == NRF_EVT_FLASH_OPERATION_SUCCESS || evt == NRF_EVT_FLASH_OPERATION_ERROR) {
          _flash_op_result = evt;
          completed = true;
          flash_nrf5x_stats.drained++;
        } else if (flash_nrf5x_soc_event_hook) {
          flash_nrf5x_soc_event_hook(evt);
        }
      }
    }
    flash_nrf5x_stats.in_flight = 0;
    if (!completed) {
      flash_nrf5x_stats.timeouts++;
      return NRF_ERROR_TIMEOUT;
    }
    flash_nrf5x_stats.last_ticks = xTaskGetTickCount() - flash_nrf5x_stats.last_start;
    if (flash_nrf5x_stats.last_ticks > flash_nrf5x_stats.max_ticks) {
      flash_nrf5x_stats.max_ticks = flash_nrf5x_stats.last_ticks;
    }

    // If completed successfully
    if (_flash_op_result == NRF_EVT_FLASH_OPERATION_SUCCESS) {
      result = NRF_SUCCESS;
    }

    // If general failure.
    else if (_flash_op_result == NRF_EVT_FLASH_OPERATION_ERROR) {
      result = NRF_ERROR_TIMEOUT;
    }

    // If this assert triggers, we need to implement a new NRF_SOC_EVTS value
    else {
      assert(false);
    }
  }

  return result;
}

// sd_flash_write() is a SoftDevice SVC; without the SoftDevice the RRAM controller is driven directly.
static uint32_t rram_write(uint32_t dst, uint32_t const * src, uint32_t n_words)
{
  static bool inited = false;
  if ( !inited ) {
    nrfx_rramc_config_t cfg = NRFX_RRAMC_DEFAULT_CONFIG(32);
    cfg.mode_write = true;
    nrfx_rramc_init(&cfg, NULL);
    inited = true;
  }
  nrfx_rramc_write_enable_set(true, 32);
  nrfx_rramc_words_write(dst, src, n_words);
  nrfx_rramc_write_buffer_commit();
  return NRF_SUCCESS;
}

static uint32_t flash_words_write(bool sd_en, uint32_t dst, uint32_t const * src, uint32_t n_words)
{
  flash_nrf5x_stats.ops++;
  flash_nrf5x_stats.last_addr  = dst;
  flash_nrf5x_stats.last_words = n_words;
  flash_nrf5x_stats.last_start = xTaskGetTickCount();

  uint32_t result;
  if ( !sd_en ) {
    result = rram_write(dst, src, n_words);
  } else {
    result = wait_for_async_flash_op_completion(sd_flash_write((uint32_t*) dst, src, n_words));
  }

  flash_nrf5x_stats.last_result = result;
  if ( result != NRF_SUCCESS && result != NRF_ERROR_TIMEOUT ) {
    flash_nrf5x_stats.errors++;
  }
  return result;
}

// One flash operation with the retry policy: BUSY is re-issued after a short pause (a previous
// operation may still be running inside the SoftDevice), a lost completion is re-issued a few
// times, any other error is final because it will not fix itself (bad address, forbidden area).
static uint32_t flash_words_write_retry(bool sd_en, uint32_t dst, uint32_t const * src, uint32_t n_words)
{
  uint32_t err;
  uint8_t busy = 0, timeouts = 0;

  for (;;) {
    err = flash_words_write(sd_en, dst, src, n_words);
    if ( err == NRF_SUCCESS ) return err;
    if ( err == NRF_ERROR_BUSY && ++busy < MAX_RETRY ) {
      delay(5);
      continue;
    }
    if ( err == NRF_ERROR_TIMEOUT && ++timeouts < MAX_TIMEOUT_RETRY ) continue;
    return err;
  }
}

// Flash Abstraction Layer
static bool fal_erase (uint32_t addr);
static uint32_t fal_program (uint32_t dst, void const * src, uint32_t len);
static uint32_t fal_read (void* dst, uint32_t src, uint32_t len);
static bool fal_verify (uint32_t addr, void const * buf, uint32_t len);

static uint8_t _cache_buffer[FLASH_CACHE_SIZE] __attribute__((aligned(4)));

static flash_cache_t _cache =
{
  // RRAM is written in place: no erase, the cache flushes only the chunks that changed.
  // fal_erase stays available through flash_nrf5x_erase() for the filesystem format path.
  .erase      = NULL,
  .program    = fal_program,
  .read       = fal_read,
  .verify     = fal_verify,

  .cache_addr = FLASH_CACHE_INVALID_ADDR,
  .cache_buf  = _cache_buffer
};

//--------------------------------------------------------------------+
// Application API
//--------------------------------------------------------------------+
bool flash_nrf5x_flush (void)
{
  bool ok = flash_cache_flush(&_cache);
  if ( !ok ) flash_nrf5x_stats.flush_failed++;
  return ok;
}

int flash_nrf5x_write (uint32_t dst, void const * src, uint32_t len)
{
  // Softdevice region
  VERIFY(dst >= ((uint32_t) __flash_arduino_start), -1);

  // Bootloader region
  VERIFY(dst < BOOTLOADER_ADDR, -1);

  return flash_cache_write(&_cache, dst, src, len);
}

int flash_nrf5x_read (void* dst, uint32_t src, uint32_t len)
{
  return flash_cache_read(&_cache, dst, src, len);
}

bool flash_nrf5x_erase(uint32_t addr)
{
  return fal_erase(addr);
}

//--------------------------------------------------------------------+
// HAL for caching
//--------------------------------------------------------------------+

// nRF54L uses RRAM which does not require erasing before writing.
// However, LittleFS expects erased pages to be 0xFF for its
// garbage collection to work correctly. We simulate an erase by
// writing 0xFF to the entire page via sd_flash_write.
static bool fal_erase (uint32_t addr)
{
  VERIFY(ensure_sem());

  uint8_t sd_en = 0;
  (void) sd_softdevice_is_enabled(&sd_en);

  // Fill page with 0xFF in chunks (sd_flash_write takes word count)
  // Use a small stack buffer of 0xFF words
  static const uint32_t ff_buf[32] = {
    [0 ... 31] = 0xFFFFFFFF
  };
  const uint32_t chunk_bytes = sizeof(ff_buf);
  uint32_t remaining = FLASH_NRF52_PAGE_SIZE;
  uint32_t dst = addr;

  while (remaining > 0)
  {
    uint32_t wr_bytes = (remaining < chunk_bytes) ? remaining : chunk_bytes;

    VERIFY_STATUS(flash_words_write_retry(sd_en, dst, ff_buf, wr_bytes / 4), false);

    dst += wr_bytes;
    remaining -= wr_bytes;
  }

  return true;
}

static uint32_t fal_program (uint32_t dst, void const * src, uint32_t len)
{
  VERIFY(ensure_sem(), 0);

  // wait for async event if SD is enabled
  uint8_t sd_en = 0;
  (void) sd_softdevice_is_enabled(&sd_en);

  // Written in slices no larger than the RRAMC write buffer; the cache normally hands over one
  // FLASH_CACHE_WRITE_CHUNK at a time, the format path a whole page.
  uint8_t const * src8 = (uint8_t const *) src;
  uint32_t const chunk_bytes = FLASH_CACHE_WRITE_CHUNK;
  uint32_t written = 0;

  while (written < len)
  {
    uint32_t wr_bytes = (len - written < chunk_bytes) ? (len - written) : chunk_bytes;

    VERIFY_STATUS(flash_words_write_retry(sd_en, dst + written, (uint32_t const *) (src8 + written), wr_bytes / 4), written);

    written += wr_bytes;
  }

  return written;
}

static uint32_t fal_read (void* dst, uint32_t src, uint32_t len)
{
  memcpy(dst, (void*) src, len);
  return len;
}

static bool fal_verify (uint32_t addr, void const * buf, uint32_t len)
{
  return 0 == memcmp((void*) addr, buf, len);
}
