/*
 * S145 compatibility shim for nrf_nvic.h
 *
 * The S145 SoftDevice (nRF54L) does not provide sd_nvic_* wrappers.
 * On nRF54L the application can use CMSIS NVIC functions directly,
 * even when the SoftDevice is enabled, as the interrupt isolation
 * is handled by the hardware (SPU/DPPI routing).
 *
 * This header provides the types and inline functions that the
 * Adafruit core expects from the legacy nrf_nvic.h.
 */

#ifndef NRF_NVIC_H__
#define NRF_NVIC_H__

#include <stdint.h>
#include "nrf.h"
#include "nrf_svc.h"
#include "nrf_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type representing the state of the NVIC.
 *
 * On nRF52 with S140 this tracked which IRQs the app had enabled
 * so the SoftDevice could manage shared NVIC access.  On nRF54L
 * with S145 this is not needed — we keep the struct for source compat.
 */
typedef struct
{
    uint32_t __irq_masks[8];  /* 8 words for up to 256 IRQs */
    uint32_t volatile __cr_flag;
} nrf_nvic_state_t;

extern nrf_nvic_state_t nrf_nvic_state;

/**
 * @brief Enter a critical region (disable interrupts).
 *
 * On S145, the SoftDevice does not provide sd_nvic_critical_region_enter.
 * We use PRIMASK directly.
 */
static inline uint32_t sd_nvic_critical_region_enter(uint8_t * p_is_nested_critical_region)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *p_is_nested_critical_region = (primask != 0) ? 1 : 0;
    return NRF_SUCCESS;
}

/**
 * @brief Exit a critical region (restore interrupts).
 */
static inline uint32_t sd_nvic_critical_region_exit(uint8_t is_nested_critical_region)
{
    if (!is_nested_critical_region)
    {
        __enable_irq();
    }
    return NRF_SUCCESS;
}

/**
 * @brief Enable an IRQ.
 */
static inline uint32_t sd_nvic_EnableIRQ(IRQn_Type IRQn)
{
    NVIC_EnableIRQ(IRQn);
    return NRF_SUCCESS;
}

/**
 * @brief Disable an IRQ.
 */
static inline uint32_t sd_nvic_DisableIRQ(IRQn_Type IRQn)
{
    NVIC_DisableIRQ(IRQn);
    return NRF_SUCCESS;
}

/**
 * @brief Set IRQ priority.
 */
static inline uint32_t sd_nvic_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    NVIC_SetPriority(IRQn, priority);
    return NRF_SUCCESS;
}

/**
 * @brief Clear pending IRQ.
 */
static inline uint32_t sd_nvic_ClearPendingIRQ(IRQn_Type IRQn)
{
    NVIC_ClearPendingIRQ(IRQn);
    return NRF_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif /* NRF_NVIC_H__ */
