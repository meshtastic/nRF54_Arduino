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
#include "nrfx_grtc.h"
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

/* Read the 52-bit GRTC SYSCOUNTER of this core's domain (NRF_GRTC_DOMAIN_INDEX is GRTC_IRQ_GROUP,
 * which the #error in portmacro_cmsis.h ties to portNRF_GRTC_DOMAIN). nrfy re-reads until BUSY and
 * OVERFLOW clear: after an idle sleep the counter is not readable until it settles. Working in
 * 64 bits leaves no 2^32 epoch to reconstruct, although the counter passes 2^32 about 71 min after
 * power-on and keeps running across soft resets. */
static inline uint64_t grtc_counter_get(void)
{
    return nrfy_grtc_sys_counter_get(portNRF_GRTC_REG);
}

/* Arm a compare channel at an absolute SYSCOUNTER value. A target already in the past raises the
 * event at once, which is what a late tick needs. Writing CCL before CCH can also form a value in
 * the past for a moment; drop that spurious event while the target itself is still ahead, as
 * nrfx_grtc_syscounter_cc_abs_set(..., safe_setting = true) does. */
static inline void grtc_cc_set(uint32_t cc_channel, uint64_t val)
{
    nrfy_grtc_sys_counter_cc_set(portNRF_GRTC_REG, cc_channel, val);
    if (nrfy_grtc_sys_counter_compare_event_check(portNRF_GRTC_REG, cc_channel) && (val > grtc_counter_get()))
    {
        nrfy_grtc_sys_counter_compare_event_clear(portNRF_GRTC_REG, cc_channel);
    }
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

/* SYSCOUNTER value at which the next OS tick falls due. Ticks stay on this grid, which starts with
 * the scheduler, instead of being re-armed one period after whenever the interrupt happened to run. */
static uint64_t grtc_next_tick;

/* A backlog (core halted in a debugger, a long critical section) is caught up at most this many
 * ticks per interrupt, so the ISR stays short; the compare is then left behind and fires again. */
#define portNRF_GRTC_CATCHUP_MAX ((TickType_t)(4 * configTICK_RATE_HZ))
/* Interrupts that found more than portNRF_GRTC_CATCHUP_MAX ticks due; readable from a debugger. */
static volatile uint32_t grtc_tick_backlogs;

void xPortSysTickHandler( void )
{
    traceISR_ENTER();

    /* Clear compare event */
    grtc_event_compare_clear(portNRF_GRTC_CC_CH);

    BaseType_t switch_req = pdFALSE;
    uint32_t isrstate = portSET_INTERRUPT_MASK_FROM_ISR();

    uint64_t const now = grtc_counter_get();

    if (configUSE_DISABLE_TICK_AUTO_CORRECTION_DEBUG == 0)
    {
        /* Auto-correct missed ticks: every grid tick that has fallen due.
         * GRTC runs at configSYSTICK_CLOCK_HZ (1 MHz).
         * Each OS tick = portNRF_GRTC_TICKS_PER_SYSTICK GRTC ticks. */
        TickType_t diff = 0;
        if (now >= grtc_next_tick)
        {
            diff = (TickType_t)((now - grtc_next_tick) / portNRF_GRTC_TICKS_PER_SYSTICK) + 1;
        }

        /* At most 1 step if scheduler is suspended */
        if ((diff > 1) && (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
        {
            diff = 1;
        }
        else if (diff > portNRF_GRTC_CATCHUP_MAX)
        {
            grtc_tick_backlogs++;
            diff = portNRF_GRTC_CATCHUP_MAX;
        }

        grtc_next_tick += (uint64_t)diff * portNRF_GRTC_TICKS_PER_SYSTICK;
        while ((diff--) > 0)
        {
            switch_req |= xTaskIncrementTick();
        }
    }
    else
    {
        switch_req = xTaskIncrementTick();
        grtc_next_tick = now + portNRF_GRTC_TICKS_PER_SYSTICK;
    }

    /* Schedule next compare: the next tick on the grid. When ticks are still due it lies in the past
     * and fires at once, except while the scheduler is suspended, which would only spin through here:
     * then wait one tick period. */
    {
        uint64_t next_cc = grtc_next_tick;
        if ((next_cc <= now) && (xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED))
        {
            next_cc = now + portNRF_GRTC_TICKS_PER_SYSTICK;
        }
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
    /* The GRTC survives soft resets, so configure its sleep outside the start path. Nothing else does:
     * nrfx_grtc_init() would, but nothing calls it and the nrfx GRTC driver is not enabled here. At the
     * reset values, TIMEOUT 0 and WAKETIME 1, the SYSCOUNTER stops as soon as the CPU sleeps and gets a
     * single 32 kHz cycle to wake up before a compare. Every idle hang caught on the DK had a SoftDevice
     * compare 1.5-2 of those cycles past the stop that never fired, and no later one fired either, so the
     * CPU slept until the watchdog. Apply NRFX_GRTC_SLEEP_DEFAULT_CONFIG (TIMEOUT 5, WAKETIME 4, and
     * AUTOEN, which sd_softdevice_enable() requires) the way nrfx_grtc_sleep_configure() does, with the
     * SYSCOUNTER stopped for the write. The SoftDevice is not enabled yet when the scheduler starts. */
    {
        nrfx_grtc_sleep_config_t const sleep_cfg = NRFX_GRTC_SLEEP_DEFAULT_CONFIG;
        bool const active = nrfy_grtc_sys_counter_check(NRF_GRTC);
        if (active)
        {
            nrfy_grtc_sys_counter_set(NRF_GRTC, false);
        }
        nrfy_grtc_sys_counter_auto_mode_set(NRF_GRTC, sleep_cfg.auto_mode);
        nrfy_grtc_timeout_set(NRF_GRTC, sleep_cfg.timeout);
        nrfy_grtc_waketime_set(NRF_GRTC, sleep_cfg.waketime);
        if (active)
        {
            nrfy_grtc_sys_counter_set(NRF_GRTC, true);
        }
    }

    /* Clear any pending event */
    grtc_event_compare_clear(portNRF_GRTC_CC_CH);

    /* First tick one period from now: the grid starts here */
    grtc_next_tick = grtc_counter_get() + portNRF_GRTC_TICKS_PER_SYSTICK;
    grtc_cc_set(portNRF_GRTC_CC_CH, grtc_next_tick);

    /* Enable compare interrupt */
    grtc_int_compare_enable(portNRF_GRTC_CC_CH);

    NVIC_SetPriority(portNRF_GRTC_IRQn, configKERNEL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(portNRF_GRTC_IRQn);
}

#if configUSE_TICKLESS_IDLE == 1

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
    /* No cap on xExpectedIdleTime: in 64 bits even portMAX_DELAY ticks (~49 days) fit the 52-bit compare. */

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

    if ( eTaskConfirmSleepModeStatus() != eAbortSleep )
    {
        TickType_t xModifiableIdleTime;
        /* Wake on the grid, when the tick that unblocks a task falls due: the next tick is due at
         * grtc_next_tick and xExpectedIdleTime counts it. */
        uint64_t const wakeupTime = grtc_next_tick + (uint64_t)(xExpectedIdleTime - 1) * portNRF_GRTC_TICKS_PER_SYSTICK;

        /* Disable periodic tick interrupt, use compare for wakeup */
        grtc_int_compare_disable(portNRF_GRTC_CC_CH);

        /* Configure compare for wakeup. Clear first: grtc_cc_set() keeps the event of a target already passed. */
        grtc_event_compare_clear(portNRF_GRTC_CC_CH);
        grtc_cc_set(portNRF_GRTC_CC_CH, wakeupTime);
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
            /* Whole grid ticks that fell due while asleep */
            TickType_t diff = 0;
            uint64_t const exitTime = grtc_counter_get();
            if (exitTime >= grtc_next_tick)
            {
                diff = (TickType_t)((exitTime - grtc_next_tick) / portNRF_GRTC_TICKS_PER_SYSTICK) + 1;
            }

            /* vTaskStepTick() must not pass the unblock tick. Ticks beyond it stay due, and the tick
             * compare armed below in the past catches them up at once. */
            if ((configUSE_TICKLESS_IDLE_SIMPLE_DEBUG) && (diff > xExpectedIdleTime))
            {
                diff = xExpectedIdleTime;
            }
            grtc_next_tick += (uint64_t)diff * portNRF_GRTC_TICKS_PER_SYSTICK;

            /* It is important that we clear pending here so that our corrections are latest and in sync with
             * tick_interrupt handler. Done before re-arming: a compare armed in the past must still fire. */
            grtc_event_compare_clear(portNRF_GRTC_CC_CH);
            NVIC_ClearPendingIRQ(portNRF_GRTC_IRQn);

            /* Re-enable periodic tick via compare */
            grtc_cc_set(portNRF_GRTC_CC_CH, grtc_next_tick);
            grtc_int_compare_enable(portNRF_GRTC_CC_CH);

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
