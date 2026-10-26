/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Adafruit Industries
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

#include "nRF54Crypto.h"

#ifdef NRF_CRACEN

#include "nrf_cracen.h"
#include "nrf_cracen_rng.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <tinycrypt/ecc.h>
#include <tinycrypt/ecc_dh.h>
#include <tinycrypt/ecc_platform_specific.h>
#include <tinycrypt/constants.h>

// Timeout for RNG FIFO fill (in loop iterations)
#define RNG_TIMEOUT 100000

// Words taken from the FIFO for the AES conditioning key (4 words, 128 bits)
#define RNG_KEY_WORDS 4

nRF54CryptoClass nRF54Crypto;

//--------------------------------------------------------------------+
// CRACEN RNG
//--------------------------------------------------------------------+

static bool _rng_key_loaded;

// Soft reset, then enable with 1x 128-bit block for AES conditioning. As in
// nrfx_cracen_entropy_get(), the first words out of the FIFO after each reset become the new
// conditioning key (cracen_rng_word()).
static void cracen_rng_reset(void)
{
    nrf_cracen_rng_control_t cfg = { 0 };
    cfg.soft_reset = true;
    nrf_cracen_rng_control_set(NRF_CRACENCORE, &cfg);

    cfg.soft_reset = false;
    cfg.enable = true;
    cfg.number_128_blocks = 1;
    nrf_cracen_rng_control_set(NRF_CRACENCORE, &cfg);

    _rng_key_loaded = false;
}

// CRACEN->ENABLE also holds the CRYPTOMASTER and PKE/IKG bits, which an application may be using
// through nrfx; the read-modify-write must not interleave with one of its own.
static void cracen_rng_module_set(bool on)
{
    taskENTER_CRITICAL();
    if (on) {
        nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_RNG_MASK);
    } else {
        nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_RNG_MASK);
    }
    taskEXIT_CRITICAL();
}

// Turns the generator and its module off
static void cracen_rng_disable(void)
{
    nrf_cracen_rng_control_t cfg = { 0 };
    cfg.enable = false;
    nrf_cracen_rng_control_set(NRF_CRACENCORE, &cfg);

    cracen_rng_module_set(false);
}

// Every draw starts from a reset, so it gets its own conditioning key
static void cracen_rng_start(void)
{
    cracen_rng_module_set(true);
    cracen_rng_reset();
}

// One draw at a time, and the TRNG runs only for the draw. The mutex is taken only while the
// scheduler runs: before it starts no other task can get in, and blocking there, or while the
// scheduler is suspended, would trip FreeRTOS's assert. With the scheduler suspended the draw fails
// if another task holds the mutex: that task may be between the FIFO level check and the read, and
// turning the TRNG off under it would hand it a word that isn't random.
static StaticSemaphore_t _rng_lock_buf;
static SemaphoreHandle_t _rng_lock = xSemaphoreCreateMutexStatic(&_rng_lock_buf);

static bool rng_lock(bool *locked)
{
    *locked = false;
    switch (xTaskGetSchedulerState()) {
    case taskSCHEDULER_RUNNING:
        (void) xSemaphoreTake(_rng_lock, portMAX_DELAY);
        *locked = true;
        return true;
    case taskSCHEDULER_SUSPENDED:
        return uxSemaphoreGetCount(_rng_lock) != 0;
    default: // not started yet
        return true;
    }
}

static void rng_unlock(bool locked)
{
    if (locked) (void) xSemaphoreGive(_rng_lock);
}

// Takes one word from the FIFO, or returns false while there is none yet. After a reset the first
// RNG_KEY_WORDS words load the conditioning key instead. A generator halted on an error is reset and
// starts again with a new key.
static bool cracen_rng_word(uint32_t *word)
{
    if (nrf_cracen_rng_fsm_state_get(NRF_CRACENCORE) == NRF_CRACEN_RNG_FSM_STATE_ERROR) {
        cracen_rng_reset();
        return false;
    }

    uint32_t level = nrf_cracen_rng_fifo_level_get(NRF_CRACENCORE);
    if (!_rng_key_loaded) {
        if (level < RNG_KEY_WORDS) return false;
        for (uint8_t i = 0; i < RNG_KEY_WORDS; i++) {
            nrf_cracen_rng_key_set(NRF_CRACENCORE, i, nrf_cracen_rng_fifo_get(NRF_CRACENCORE));
        }
        _rng_key_loaded = true;
        level -= RNG_KEY_WORDS;
    }

    if (level == 0) return false;
    *word = nrf_cracen_rng_fifo_get(NRF_CRACENCORE);
    return true;
}

static bool cracen_rng_fill(uint8_t *dest, size_t len)
{
    size_t offset = 0;
    while (offset < len) {
        uint32_t word;
        uint32_t timeout = RNG_TIMEOUT;
        while (!cracen_rng_word(&word)) {
            if (--timeout == 0) return false;
        }

        // Copy bytes (handle partial word at end)
        size_t remaining = len - offset;
        size_t to_copy = (remaining < 4) ? remaining : 4;
        memcpy(dest + offset, &word, to_copy);
        offset += to_copy;
    }
    return true;
}

//--------------------------------------------------------------------+
// default_CSPRNG - called by TinyCrypt uECC
//--------------------------------------------------------------------+

static bool rng_draw(uint8_t *dest, size_t len)
{
    bool locked;
    if (!rng_lock(&locked)) return false;

    cracen_rng_start();
    bool ok = cracen_rng_fill(dest, len);
    cracen_rng_disable();

    rng_unlock(locked);
    return ok;
}

extern "C" int default_CSPRNG(uint8_t *dest, unsigned int size)
{
    return rng_draw(dest, size) ? 1 : 0;
}

//--------------------------------------------------------------------+
// nRF54CryptoClass
//--------------------------------------------------------------------+

// The TRNG starts and stops around each draw, and uECC already defaults to default_CSPRNG
// (ecc_platform_specific.h), so begin() and end() have nothing to do. Both stay for the callers that
// pair them.
bool nRF54CryptoClass::begin(void)
{
    return true;
}

void nRF54CryptoClass::end(void)
{
}

bool nRF54CryptoClass::genKeyPair(uint8_t privkey[32], uint8_t pubkey[64])
{
    return uECC_make_key(pubkey, privkey, uECC_secp256r1()) == TC_CRYPTO_SUCCESS;
}

bool nRF54CryptoClass::sharedSecret(const uint8_t peer_pubkey[64],
                                     const uint8_t privkey[32],
                                     uint8_t dhkey[32])
{
    return uECC_shared_secret(peer_pubkey, privkey, dhkey, uECC_secp256r1()) == TC_CRYPTO_SUCCESS;
}

bool nRF54CryptoClass::random(uint8_t* dest, size_t len)
{
    return rng_draw(dest, len);
}

#endif // NRF_CRACEN
