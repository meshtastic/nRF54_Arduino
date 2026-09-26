/*
 * FreeRTOS Kernel V10.0.0
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software. If you wish to use our Amazon
 * FreeRTOS name, please do so in a fair use way that does not cause confusion.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/* Scheduler includes. */
#include "nrfy_grtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "nrf_nvic.h"

#define ROUNDED_DIV(A, B) (((A) + ((B) / 2)) / (B))

#ifdef SOFTDEVICE_PRESENT
#include "nrf_soc.h"
#endif

/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the ARM CM33 port.
 * CMSIS compatible layer to manage tick source using GRTC.
 *
 * GRTC SYSCOUNTER runs at 1 MHz (configSYSTICK_CLOCK_HZ = 1000000).
 * We use a compare channel to generate tick interrupts.
 *----------------------------------------------------------*/
#if configUSE_16_BIT_TICKS == 1
#error This port does not support 16 bit ticks.
#endif

/*-----------------------------------------------------------*/

/* Read the low 32 bits of the GRTC SYSCOUNTER.
 * SYSCOUNTER is 52-bit but we only need 32-bit for tick counting. */
static inline uint32_t grtc_counter_get(void)
{
    /* The SYSCOUNTER is only guaranteed readable while it is active: after an idle sleep a read
     * before it settles returns junk, which the tick catch-up below turns into thousands of ticks.
     * Reading SYSCOUNTERL latches SYSCOUNTERH (whose reset value already has BUSY set), so the
     * pair must be re-read until BUSY clears, as nrfx_grtc does; we only need the low 32 bits. */
    uint32_t lo, hi;
    do {
        lo = portNRF_GRTC_REG->SYSCOUNTER[portNRF_GRTC_DOMAIN].SYSCOUNTERL; /* latches SYSCOUNTERH */
        hi = portNRF_GRTC_REG->SYSCOUNTER[portNRF_GRTC_DOMAIN].SYSCOUNTERH;
    } while (hi & (1UL << 30)); /* BUSY: the latched pair is not valid yet */
    return lo;
}

/* Set compare channel value.
 * The compare is 52-bit and SYSCOUNTER keeps running across soft resets, so it passes
 * 2^32 about 71 minutes after power-on. A compare written with CCH = 0 is then already
 * in the past and never fires: the tick only survives while some other interrupt wakes
 * the CPU, and the first idle sleep after that never ends. val is a 32-bit target derived
 * from the low word; rebuild the high word from the live counter, carrying when val
 * wrapped past the end of the current 2^32 epoch. */
static inline void grtc_cc_set(uint32_t cc_channel, uint32_t val)
{
    uint32_t lo = portNRF_GRTC_REG->SYSCOUNTER[portNRF_GRTC_DOMAIN].SYSCOUNTERL; /* latches SYSCOUNTERH */
    uint32_t hi = portNRF_GRTC_REG->SYSCOUNTER[portNRF_GRTC_DOMAIN].SYSCOUNTERH & 0x000FFFFFUL;
    if ((val < lo) && ((int32_t)(val - lo) > 0)) {
        hi++; /* target lies in the next epoch */
    }
    portNRF_GRTC_REG->CC[cc_channel].CCL = val;
    portNRF_GRTC_REG->CC[cc_channel].CCH = hi; /* writing CCH enables the compare */
}

/* Clear compare event */
static inline void grtc_event_compare_clear(uint32_t cc_channel)
{
    portNRF_GRTC_REG->EVENTS_COMPARE[cc_channel] = 0;
    /* Ensure event is cleared before ISR returns (ARM Cortex-M write buffer) */
    volatile uint32_t dummy = portNRF_GRTC_REG->EVENTS_COMPARE[cc_channel];
    (void)dummy;
}

/* Enable compare interrupt for channel */
static inline void grtc_int_compare_enable(uint32_t cc_channel)
{
    portNRF_GRTC_REG->portNRF_GRTC_INTENSET = (1UL << cc_channel);
}

/* Disable compare interrupt for channel */
static inline void grtc_int_compare_disable(uint32_t cc_channel)
{
    portNRF_GRTC_REG->portNRF_GRTC_INTENCLR = (1UL << cc_channel);
}

/*-----------------------------------------------------------*/

// SYSCOUNTER value at scheduler start; the counter survives resets, so ticks count from here.
static uint32_t grtc_tick_base;

void xPortSysTickHandler( void )
{
    traceISR_ENTER();

    /* Clear compare event */
    grtc_event_compare_clear(portNRF_GRTC_CC_CH);

    BaseType_t switch_req = pdFALSE;
    uint32_t isrstate = portSET_INTERRUPT_MASK_FROM_ISR();

    uint32_t systick_counter = grtc_counter_get();

    if (configUSE_DISABLE_TICK_AUTO_CORRECTION_DEBUG == 0)
    {
        /* Auto-correct missed ticks.
         * GRTC runs at configSYSTICK_CLOCK_HZ (1 MHz).
         * Each OS tick = portNRF_GRTC_TICKS_PER_SYSTICK GRTC ticks. */
        TickType_t diff;
        uint32_t expected_counter = grtc_tick_base + xTaskGetTickCount() * portNRF_GRTC_TICKS_PER_SYSTICK;
        diff = (systick_counter - expected_counter) / portNRF_GRTC_TICKS_PER_SYSTICK;

        /* At most 1 step if scheduler is suspended */
        if ((diff > 1) && (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
        {
            diff = 1;
        }
        /* A bogus counter read must not stall the CPU in this loop nor jump millis() forward by
         * hours: anything beyond a few seconds of catch-up is treated as one tick and the tick base is
         * re-anchored to the counter. Genuine long sleeps are accounted in vPortSuppressTicksAndSleep. */
        if (diff > (TickType_t)(4 * configTICK_RATE_HZ))
        {
            grtc_tick_base = systick_counter - (xTaskGetTickCount() + 1) * portNRF_GRTC_TICKS_PER_SYSTICK;
            diff = 1;
        }
        while ((diff--) > 0)
        {
            switch_req |= xTaskIncrementTick();
        }
    }
    else
    {
        switch_req = xTaskIncrementTick();
    }

    /* Schedule next compare */
    {
        uint32_t next_cc = grtc_counter_get() + portNRF_GRTC_TICKS_PER_SYSTICK;
        grtc_cc_set(portNRF_GRTC_CC_CH, next_cc);
    }

    /* Increment the RTOS tick as usual which checks if there is a need for rescheduling */
    if ( switch_req != pdFALSE )
    {
        traceISR_EXIT_TO_SCHEDULER();
        /* A context switch is required.  Context switching is performed in
        the PendSV interrupt.  Pend the PendSV interrupt. */
        SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
        __SEV();
    }
    else
    {
        traceISR_EXIT();
    }

    portCLEAR_INTERRUPT_MASK_FROM_ISR( isrstate );
}

/*
 * Setup the GRTC compare channel to generate tick interrupts at the required
 * frequency.
 */
void vPortSetupTimerInterrupt( void )
{
    /* Nothing before us starts the GRTC (no MBR/bootloader does), so bring the
     * 1 MHz SYSCOUNTER up here; the SoftDevice uses the same counter later. */
    if (!nrf_grtc_sys_counter_check(NRF_GRTC))
    {
#ifdef USE_LFXO
        nrf_grtc_clksel_set(NRF_GRTC, NRF_GRTC_CLKSEL_LFXO);
#endif
        nrfy_grtc_prepare(NRF_GRTC, true);
        nrfy_grtc_sys_counter_start(NRF_GRTC, true);
    }
    /* The GRTC survives soft resets, so set this outside the start path: sd_softdevice_enable() requires AUTOEN. */
    nrf_grtc_sys_counter_auto_mode_set(NRF_GRTC, true);
    /* Nothing else writes the sleep timing either (nrfx_grtc_init() would, but nothing calls it). At the
     * reset values, TIMEOUT 0 and WAKETIME 1, the SYSCOUNTER stops as soon as the CPU sleeps and gets a
     * single 32 kHz cycle to wake up before a compare. Every idle hang caught on the DK had a SoftDevice
     * compare 1.5-2 of those cycles past the stop that never fired, and no later one fired either, so the
     * CPU slept until the watchdog. Use the values nrfx_grtc_init() applies (NRFX_GRTC_SLEEP_DEFAULT_CONFIG). */
    nrf_grtc_timeout_set(NRF_GRTC, 5);
    nrf_grtc_waketime_set(NRF_GRTC, 4);

    /* Clear any pending event */
    grtc_event_compare_clear(portNRF_GRTC_CC_CH);

    /* Set first compare value */
    uint32_t now = grtc_counter_get();
    grtc_tick_base = now;
    grtc_cc_set(portNRF_GRTC_CC_CH, now + portNRF_GRTC_TICKS_PER_SYSTICK);

    /* Enable compare interrupt */
    grtc_int_compare_enable(portNRF_GRTC_CC_CH);

    NVIC_SetPriority(portNRF_GRTC_IRQn, configKERNEL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(portNRF_GRTC_IRQn);
}

#if configUSE_TICKLESS_IDLE == 1

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
    TickType_t enterTime;

    /* Make sure the expected idle time does not overflow the counter. */
    if ( xExpectedIdleTime > portNRF_GRTC_MAXTICKS - configEXPECTED_IDLE_TIME_BEFORE_SLEEP )
    {
        xExpectedIdleTime = portNRF_GRTC_MAXTICKS - configEXPECTED_IDLE_TIME_BEFORE_SLEEP;
    }

    /* Block all the interrupts globally */
#ifdef SOFTDEVICE_PRESENT
    do{
        uint8_t dummy = 0;
        uint32_t err_code = sd_nvic_critical_region_enter(&dummy);
        (void) err_code;
    }while (0);
#else
    __disable_irq();
#endif

    enterTime = grtc_counter_get();

    if ( eTaskConfirmSleepModeStatus() != eAbortSleep )
    {
        TickType_t xModifiableIdleTime;
        /* Convert OS ticks to GRTC ticks for wakeup time */
        /* xExpectedIdleTime * TICKS_PER_SYSTICK must fit 32 bits: with every task blocked forever
         * FreeRTOS asks for a huge idle and the product wrapped to an arbitrary wake-up time. */
        if (xExpectedIdleTime > (portNRF_GRTC_MAXTICKS / portNRF_GRTC_TICKS_PER_SYSTICK) - 1)
        {
            xExpectedIdleTime = (portNRF_GRTC_MAXTICKS / portNRF_GRTC_TICKS_PER_SYSTICK) - 1;
        }
        uint32_t wakeupTime = (enterTime + xExpectedIdleTime * portNRF_GRTC_TICKS_PER_SYSTICK) & portNRF_GRTC_MAXTICKS;

        /* Disable periodic tick interrupt, use compare for wakeup */
        grtc_int_compare_disable(portNRF_GRTC_CC_CH);

        /* Configure compare for wakeup */
        grtc_cc_set(portNRF_GRTC_CC_CH, wakeupTime);
        grtc_event_compare_clear(portNRF_GRTC_CC_CH);
        grtc_int_compare_enable(portNRF_GRTC_CC_CH);

        __DSB();

        xModifiableIdleTime = xExpectedIdleTime;
        configPRE_SLEEP_PROCESSING( xModifiableIdleTime );
        if ( xModifiableIdleTime > 0 )
        {
#if (__FPU_USED == 1)
            /* Clear FPU flags to prevent it from keeping CPU awake */
            __set_FPSCR(__get_FPSCR() & ~(0x0000009F));
            (void) __get_FPSCR();
  #ifdef FPU_IRQn
            /* nRF52 routes FPU exceptions through a dedicated NVIC line;
             * clear any pending FPU interrupt before entering sleep so it
             * doesn't wake the CPU spuriously. Cortex-M33 (nRF54L) has no
             * FPU_IRQn - FPU exceptions go through UsageFault instead, so
             * this NVIC clear has nothing to do. */
            NVIC_ClearPendingIRQ(FPU_IRQn);
  #endif
#endif

            {
                /* S145 does not provide sd_app_evt_wait(), so the idle task sleeps on its own with
                 * PRIMASK set. Use WFI, not WFE: WFI completes as soon as an enabled interrupt is
                 * pending, PRIMASK or not, which is exactly the wake-up this sleep needs. WFE only
                 * wakes through SEVONPEND turning the pending transition into an event, and on the
                 * nRF54L15 the core has been found asleep for hours in that WFE with the GRTC tick and
                 * SoftDevice interrupts pending and SEVONPEND set. Zephyr sleeps the same way on this
                 * part (arch_cpu_idle: cpsid i / BASEPRI 0 / wfi / cpsie i). BASEPRI still cannot be
                 * used for the masking because it would keep WFI from waking. */
                __WFI();
            }
        }
        configPOST_SLEEP_PROCESSING( xExpectedIdleTime );

        grtc_int_compare_disable(portNRF_GRTC_CC_CH);
        grtc_event_compare_clear(portNRF_GRTC_CC_CH);

        /* Correct the system ticks */
        {
            TickType_t diff;
            TickType_t exitTime;

            exitTime = grtc_counter_get();
            /* Convert GRTC ticks back to OS ticks */
            diff = ((exitTime - enterTime) & portNRF_GRTC_MAXTICKS) / portNRF_GRTC_TICKS_PER_SYSTICK;

            /* Re-enable periodic tick via compare */
            uint32_t next_cc = grtc_counter_get() + portNRF_GRTC_TICKS_PER_SYSTICK;
            grtc_cc_set(portNRF_GRTC_CC_CH, next_cc);
            grtc_event_compare_clear(portNRF_GRTC_CC_CH);
            grtc_int_compare_enable(portNRF_GRTC_CC_CH);

            /* It is important that we clear pending here so that our corrections are latest and in sync with tick_interrupt handler */
            NVIC_ClearPendingIRQ(portNRF_GRTC_IRQn);

            if ((configUSE_TICKLESS_IDLE_SIMPLE_DEBUG) && (diff > xExpectedIdleTime))
            {
                diff = xExpectedIdleTime;
            }

            BaseType_t switch_req = pdFALSE;

            if (diff > 1)
            {
                vTaskStepTick(diff - 1);

                // If dwt cycle count is enabled, adjust it as well
                if ( (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) && (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) )
                {
                  DWT->CYCCNT += (((diff-1) * 1000000) / configTICK_RATE_HZ) * (SystemCoreClock / 1000000);
                }

                switch_req = xTaskIncrementTick();
            }
            else if (diff == 1)
            {
                switch_req = xTaskIncrementTick();
            }

            if ( switch_req != pdFALSE )
            {
                SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
                __SEV();
            }
        }
    }
#ifdef SOFTDEVICE_PRESENT
    uint32_t err_code = sd_nvic_critical_region_exit(0);
    (void) err_code;
#else
    __enable_irq();
#endif
}

#endif // configUSE_TICKLESS_IDLE
