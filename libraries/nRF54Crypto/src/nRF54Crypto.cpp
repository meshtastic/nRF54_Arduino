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

#include <tinycrypt/ecc.h>
#include <tinycrypt/ecc_dh.h>
#include <tinycrypt/ecc_platform_specific.h>
#include <tinycrypt/constants.h>

// Timeout for RNG FIFO fill (in loop iterations)
#define RNG_TIMEOUT 100000

nRF54CryptoClass nRF54Crypto;

//--------------------------------------------------------------------+
// CRACEN RNG
//--------------------------------------------------------------------+

static bool _rng_started = false;

// Enable with 1x 128-bit block for AES conditioning
static void cracen_rng_control(bool soft_reset)
{
    nrf_cracen_rng_control_t cfg = { 0 };
    cfg.enable = true;
    cfg.number_128_blocks = 1;
    cfg.soft_reset = soft_reset;
    nrf_cracen_rng_control_set(NRF_CRACENCORE, &cfg);
}

// A halted generator needs a soft reset before it produces again. Done in place rather than through
// stop/start, so a reader on another task never finds the RNG disabled under it.
static void cracen_rng_reset_if_halted(void)
{
    if (nrf_cracen_rng_fsm_state_get(NRF_CRACENCORE) != NRF_CRACEN_RNG_FSM_STATE_ERROR) return;
    cracen_rng_control(true);
    cracen_rng_control(false);
}

static bool cracen_rng_start(void)
{
    if (_rng_started) return true;

    // Enable CRACEN RNG module
    nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_RNG_MASK);
    cracen_rng_control(false);

    // Wait for the FSM to leave RESET/STARTUP; a full FIFO parks it in IDLE_STANDBY (rings off)
    uint32_t timeout = RNG_TIMEOUT;
    while (timeout--) {
        nrf_cracen_rng_fsm_state_t state = nrf_cracen_rng_fsm_state_get(NRF_CRACENCORE);
        if (state == NRF_CRACEN_RNG_FSM_STATE_IDLE_READY ||
            state == NRF_CRACEN_RNG_FSM_STATE_IDLE_STANDBY ||
            state == NRF_CRACEN_RNG_FSM_STATE_FILL_FIFO) {
            _rng_started = true;
            return true;
        }
        cracen_rng_reset_if_halted();
    }
    return false;
}

static void cracen_rng_stop(void)
{
    if (!_rng_started) return;

    // Disable RNG
    nrf_cracen_rng_control_t cfg = { 0 };
    cfg.enable = false;
    nrf_cracen_rng_control_set(NRF_CRACENCORE, &cfg);

    nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_RNG_MASK);
    _rng_started = false;
}

// Readers run on different tasks (SoftDevice seed requests, LESC key generation). Another reader, or
// a reset clearing the FIFO, between the level check and the read would hand out a word that is not
// random, so the two happen in one critical section. Waiting for data stays outside it.
static bool cracen_rng_word(uint32_t *word)
{
    taskENTER_CRITICAL();
    bool got = nrf_cracen_rng_fifo_level_get(NRF_CRACENCORE) > 0;
    if (got) {
        *word = nrf_cracen_rng_fifo_get(NRF_CRACENCORE);
    } else {
        cracen_rng_reset_if_halted();
    }
    taskEXIT_CRITICAL();
    return got;
}

static bool cracen_rng_fill(uint8_t *dest, size_t len)
{
    if (!_rng_started) return false;

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

extern "C" int default_CSPRNG(uint8_t *dest, unsigned int size)
{
    // uECC also draws randomness during ECDH, which may run long after end() stopped the RNG
    if (!_rng_started && !cracen_rng_start()) return 0;
    return cracen_rng_fill(dest, size) ? 1 : 0;
}

//--------------------------------------------------------------------+
// nRF54CryptoClass
//--------------------------------------------------------------------+

bool nRF54CryptoClass::begin(void)
{
    if (!cracen_rng_start()) return false;

    // Register our RNG with uECC
    uECC_set_rng(default_CSPRNG);

    return true;
}

void nRF54CryptoClass::end(void)
{
    cracen_rng_stop();
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
    return cracen_rng_fill(dest, len);
}

#endif // NRF_CRACEN
