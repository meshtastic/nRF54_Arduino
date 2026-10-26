/* Startup for the nRF54L15 application core (Arduino nRF54L core).
 * Vector table generated from nrf54l15_application.h; every IRQ has a weak <NAME>_IRQHandler.
 * The SoftDevice-owned vectors (SVC, SWI00, AAR00_CCM00, ECB00, TIMER10, RADIO_0, GRTC_3, CLOCK_POWER)
 * are overridden by nordic/sd_isr.S. */

    .syntax unified
    .arch armv8-m.main
    .thumb

    .global __isr_vector
    .extern __libc_init_array
    .extern __StackTop

    /* MSP stack (exceptions, SoftDevice); FreeRTOS task stacks come from the heap */
#ifndef __STACK_SIZE
#define __STACK_SIZE 8192
#endif
    .section .stack
    .align 3
    .space __STACK_SIZE

    .section .isr_vector, "a"
    .align 4
    .type g_pfnVectors, %object

__isr_vector:
g_pfnVectors:
    .word   __StackTop
    .word   Reset_Handler
    .word   NMI_Handler
    .word   HardFault_Handler
    .word   MemManage_Handler
    .word   BusFault_Handler
    .word   UsageFault_Handler
    .word   SecureFault_Handler
    .word   0
    .word   0
    .word   0
    .word   SVC_Handler
    .word   DebugMon_Handler
    .word   0
    .word   PendSV_Handler
    .word   SysTick_Handler
    .word   Default_Handler /* 0 */
    .word   Default_Handler /* 1 */
    .word   Default_Handler /* 2 */
    .word   Default_Handler /* 3 */
    .word   Default_Handler /* 4 */
    .word   Default_Handler /* 5 */
    .word   Default_Handler /* 6 */
    .word   Default_Handler /* 7 */
    .word   Default_Handler /* 8 */
    .word   Default_Handler /* 9 */
    .word   Default_Handler /* 10 */
    .word   Default_Handler /* 11 */
    .word   Default_Handler /* 12 */
    .word   Default_Handler /* 13 */
    .word   Default_Handler /* 14 */
    .word   Default_Handler /* 15 */
    .word   Default_Handler /* 16 */
    .word   Default_Handler /* 17 */
    .word   Default_Handler /* 18 */
    .word   Default_Handler /* 19 */
    .word   Default_Handler /* 20 */
    .word   Default_Handler /* 21 */
    .word   Default_Handler /* 22 */
    .word   Default_Handler /* 23 */
    .word   Default_Handler /* 24 */
    .word   Default_Handler /* 25 */
    .word   Default_Handler /* 26 */
    .word   Default_Handler /* 27 */
    .word   SWI00_IRQHandler /* 28 */
    .word   SWI01_IRQHandler /* 29 */
    .word   SWI02_IRQHandler /* 30 */
    .word   SWI03_IRQHandler /* 31 */
    .word   Default_Handler /* 32 */
    .word   Default_Handler /* 33 */
    .word   Default_Handler /* 34 */
    .word   Default_Handler /* 35 */
    .word   Default_Handler /* 36 */
    .word   Default_Handler /* 37 */
    .word   Default_Handler /* 38 */
    .word   Default_Handler /* 39 */
    .word   Default_Handler /* 40 */
    .word   Default_Handler /* 41 */
    .word   Default_Handler /* 42 */
    .word   Default_Handler /* 43 */
    .word   Default_Handler /* 44 */
    .word   Default_Handler /* 45 */
    .word   Default_Handler /* 46 */
    .word   Default_Handler /* 47 */
    .word   Default_Handler /* 48 */
    .word   Default_Handler /* 49 */
    .word   Default_Handler /* 50 */
    .word   Default_Handler /* 51 */
    .word   Default_Handler /* 52 */
    .word   Default_Handler /* 53 */
    .word   Default_Handler /* 54 */
    .word   Default_Handler /* 55 */
    .word   Default_Handler /* 56 */
    .word   Default_Handler /* 57 */
    .word   Default_Handler /* 58 */
    .word   Default_Handler /* 59 */
    .word   Default_Handler /* 60 */
    .word   Default_Handler /* 61 */
    .word   Default_Handler /* 62 */
    .word   Default_Handler /* 63 */
    .word   SPU00_IRQHandler /* 64 */
    .word   MPC00_IRQHandler /* 65 */
    .word   Default_Handler /* 66 */
    .word   Default_Handler /* 67 */
    .word   Default_Handler /* 68 */
    .word   Default_Handler /* 69 */
    .word   AAR00_CCM00_IRQHandler /* 70 */
    .word   ECB00_IRQHandler /* 71 */
    .word   CRACEN_IRQHandler /* 72 */
    .word   Default_Handler /* 73 */
    .word   SERIAL00_IRQHandler /* 74 */
    .word   RRAMC_IRQHandler /* 75 */
    .word   VPR00_IRQHandler /* 76 */
    .word   Default_Handler /* 77 */
    .word   Default_Handler /* 78 */
    .word   Default_Handler /* 79 */
    .word   Default_Handler /* 80 */
    .word   Default_Handler /* 81 */
    .word   CTRLAP_IRQHandler /* 82 */
    .word   Default_Handler /* 83 */
    .word   CM33SS_IRQHandler /* 84 */
    .word   TIMER00_IRQHandler /* 85 */
    .word   Default_Handler /* 86 */
    .word   Default_Handler /* 87 */
    .word   Default_Handler /* 88 */
    .word   Default_Handler /* 89 */
    .word   Default_Handler /* 90 */
    .word   Default_Handler /* 91 */
    .word   Default_Handler /* 92 */
    .word   Default_Handler /* 93 */
    .word   Default_Handler /* 94 */
    .word   Default_Handler /* 95 */
    .word   Default_Handler /* 96 */
    .word   Default_Handler /* 97 */
    .word   Default_Handler /* 98 */
    .word   Default_Handler /* 99 */
    .word   Default_Handler /* 100 */
    .word   Default_Handler /* 101 */
    .word   Default_Handler /* 102 */
    .word   Default_Handler /* 103 */
    .word   Default_Handler /* 104 */
    .word   Default_Handler /* 105 */
    .word   Default_Handler /* 106 */
    .word   Default_Handler /* 107 */
    .word   Default_Handler /* 108 */
    .word   Default_Handler /* 109 */
    .word   Default_Handler /* 110 */
    .word   Default_Handler /* 111 */
    .word   Default_Handler /* 112 */
    .word   Default_Handler /* 113 */
    .word   Default_Handler /* 114 */
    .word   Default_Handler /* 115 */
    .word   Default_Handler /* 116 */
    .word   Default_Handler /* 117 */
    .word   Default_Handler /* 118 */
    .word   Default_Handler /* 119 */
    .word   Default_Handler /* 120 */
    .word   Default_Handler /* 121 */
    .word   Default_Handler /* 122 */
    .word   Default_Handler /* 123 */
    .word   Default_Handler /* 124 */
    .word   Default_Handler /* 125 */
    .word   Default_Handler /* 126 */
    .word   Default_Handler /* 127 */
    .word   SPU10_IRQHandler /* 128 */
    .word   Default_Handler /* 129 */
    .word   Default_Handler /* 130 */
    .word   Default_Handler /* 131 */
    .word   Default_Handler /* 132 */
    .word   TIMER10_IRQHandler /* 133 */
    .word   Default_Handler /* 134 */
    .word   EGU10_IRQHandler /* 135 */
    .word   Default_Handler /* 136 */
    .word   Default_Handler /* 137 */
    .word   RADIO_0_IRQHandler /* 138 */
    .word   RADIO_1_IRQHandler /* 139 */
    .word   Default_Handler /* 140 */
    .word   Default_Handler /* 141 */
    .word   Default_Handler /* 142 */
    .word   Default_Handler /* 143 */
    .word   Default_Handler /* 144 */
    .word   Default_Handler /* 145 */
    .word   Default_Handler /* 146 */
    .word   Default_Handler /* 147 */
    .word   Default_Handler /* 148 */
    .word   Default_Handler /* 149 */
    .word   Default_Handler /* 150 */
    .word   Default_Handler /* 151 */
    .word   Default_Handler /* 152 */
    .word   Default_Handler /* 153 */
    .word   Default_Handler /* 154 */
    .word   Default_Handler /* 155 */
    .word   Default_Handler /* 156 */
    .word   Default_Handler /* 157 */
    .word   Default_Handler /* 158 */
    .word   Default_Handler /* 159 */
    .word   Default_Handler /* 160 */
    .word   Default_Handler /* 161 */
    .word   Default_Handler /* 162 */
    .word   Default_Handler /* 163 */
    .word   Default_Handler /* 164 */
    .word   Default_Handler /* 165 */
    .word   Default_Handler /* 166 */
    .word   Default_Handler /* 167 */
    .word   Default_Handler /* 168 */
    .word   Default_Handler /* 169 */
    .word   Default_Handler /* 170 */
    .word   Default_Handler /* 171 */
    .word   Default_Handler /* 172 */
    .word   Default_Handler /* 173 */
    .word   Default_Handler /* 174 */
    .word   Default_Handler /* 175 */
    .word   Default_Handler /* 176 */
    .word   Default_Handler /* 177 */
    .word   Default_Handler /* 178 */
    .word   Default_Handler /* 179 */
    .word   Default_Handler /* 180 */
    .word   Default_Handler /* 181 */
    .word   Default_Handler /* 182 */
    .word   Default_Handler /* 183 */
    .word   Default_Handler /* 184 */
    .word   Default_Handler /* 185 */
    .word   Default_Handler /* 186 */
    .word   Default_Handler /* 187 */
    .word   Default_Handler /* 188 */
    .word   Default_Handler /* 189 */
    .word   Default_Handler /* 190 */
    .word   Default_Handler /* 191 */
    .word   SPU20_IRQHandler /* 192 */
    .word   Default_Handler /* 193 */
    .word   Default_Handler /* 194 */
    .word   Default_Handler /* 195 */
    .word   Default_Handler /* 196 */
    .word   Default_Handler /* 197 */
    .word   SERIAL20_IRQHandler /* 198 */
    .word   SERIAL21_IRQHandler /* 199 */
    .word   SERIAL22_IRQHandler /* 200 */
    .word   EGU20_IRQHandler /* 201 */
    .word   TIMER20_IRQHandler /* 202 */
    .word   TIMER21_IRQHandler /* 203 */
    .word   TIMER22_IRQHandler /* 204 */
    .word   TIMER23_IRQHandler /* 205 */
    .word   TIMER24_IRQHandler /* 206 */
    .word   Default_Handler /* 207 */
    .word   PDM20_IRQHandler /* 208 */
    .word   PDM21_IRQHandler /* 209 */
    .word   PWM20_IRQHandler /* 210 */
    .word   PWM21_IRQHandler /* 211 */
    .word   PWM22_IRQHandler /* 212 */
    .word   SAADC_IRQHandler /* 213 */
    .word   NFCT_IRQHandler /* 214 */
    .word   TEMP_IRQHandler /* 215 */
    .word   Default_Handler /* 216 */
    .word   Default_Handler /* 217 */
    .word   GPIOTE20_0_IRQHandler /* 218 */
    .word   GPIOTE20_1_IRQHandler /* 219 */
    .word   TAMPC_IRQHandler /* 220 */
    .word   I2S20_IRQHandler /* 221 */
    .word   Default_Handler /* 222 */
    .word   Default_Handler /* 223 */
    .word   QDEC20_IRQHandler /* 224 */
    .word   QDEC21_IRQHandler /* 225 */
    .word   GRTC_0_IRQHandler /* 226 */
    .word   GRTC_1_IRQHandler /* 227 */
    .word   GRTC_2_IRQHandler /* 228 */
    .word   GRTC_3_IRQHandler /* 229 */
    .word   Default_Handler /* 230 */
    .word   Default_Handler /* 231 */
    .word   Default_Handler /* 232 */
    .word   Default_Handler /* 233 */
    .word   Default_Handler /* 234 */
    .word   Default_Handler /* 235 */
    .word   Default_Handler /* 236 */
    .word   Default_Handler /* 237 */
    .word   Default_Handler /* 238 */
    .word   Default_Handler /* 239 */
    .word   Default_Handler /* 240 */
    .word   Default_Handler /* 241 */
    .word   Default_Handler /* 242 */
    .word   Default_Handler /* 243 */
    .word   Default_Handler /* 244 */
    .word   Default_Handler /* 245 */
    .word   Default_Handler /* 246 */
    .word   Default_Handler /* 247 */
    .word   Default_Handler /* 248 */
    .word   Default_Handler /* 249 */
    .word   Default_Handler /* 250 */
    .word   Default_Handler /* 251 */
    .word   Default_Handler /* 252 */
    .word   Default_Handler /* 253 */
    .word   Default_Handler /* 254 */
    .word   Default_Handler /* 255 */
    .word   SPU30_IRQHandler /* 256 */
    .word   Default_Handler /* 257 */
    .word   Default_Handler /* 258 */
    .word   Default_Handler /* 259 */
    .word   SERIAL30_IRQHandler /* 260 */
    .word   CLOCK_POWER_IRQHandler /* 261 */
    .word   COMP_LPCOMP_IRQHandler /* 262 */
    .word   Default_Handler /* 263 */
    .word   WDT30_IRQHandler /* 264 */
    .word   WDT31_IRQHandler /* 265 */
    .word   Default_Handler /* 266 */
    .word   Default_Handler /* 267 */
    .word   GPIOTE30_0_IRQHandler /* 268 */
    .word   GPIOTE30_1_IRQHandler /* 269 */

    .size g_pfnVectors, .-g_pfnVectors

    .section .text.Reset_Handler
    .global Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    cpsid i

    /* The application owns the vector table (no MBR on nRF54L); point VTOR at it. */
    ldr r0, =__isr_vector
    ldr r1, =0xE000ED08
    str r0, [r1]

    ldr r0, =__etext
    ldr r1, =__data_start__
    ldr r2, =__data_end__
LoopCopyData:
    cmp r1, r2
    bcs LoopCopyDataDone
    ldr r3, [r0], #4
    str r3, [r1], #4
    b LoopCopyData
LoopCopyDataDone:

    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    mov r2, #0
LoopClearBss:
    cmp r0, r1
    bcs RunMain
    str r2, [r0], #4
    b LoopClearBss

RunMain:
    bl SystemInit

    /* SystemCoreClock starts at its 64 MHz default, but SystemInit has set the
     * PLL to 128 MHz. Correct it before global constructors can time anything. */
    bl SystemCoreClockUpdate

    /* The RRAM cache is off after reset. On before the global constructors, so all
     * code runs cached, which is also what nrfx_coredep_delay_us() is costed for
     * (3 cycles per loop iteration; about 8 from RRAM). */
    bl nrf54_cache_enable

    bl __libc_init_array
    cpsie i
    bl main
InfiniteLoop:
    b InfiniteLoop
    .size Reset_Handler, .-Reset_Handler

    .section .text.Default_Handler
    .weak Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b .
    .size Default_Handler, .-Default_Handler

    .macro def_weak_handler name
    .weak \name
    .thumb_set \name, Default_Handler
    .endm

    def_weak_handler NMI_Handler
    def_weak_handler HardFault_Handler
    def_weak_handler MemManage_Handler
    def_weak_handler BusFault_Handler
    def_weak_handler UsageFault_Handler
    def_weak_handler SecureFault_Handler
    def_weak_handler SVC_Handler
    def_weak_handler DebugMon_Handler
    def_weak_handler PendSV_Handler

    /* FreeRTOS ticks from the GRTC, SysTick is unused. */
    .weak SysTick_Handler
    .type SysTick_Handler, %function
SysTick_Handler:
    bx lr
    .size SysTick_Handler, .-SysTick_Handler

    def_weak_handler SWI00_IRQHandler
    def_weak_handler SWI01_IRQHandler
    def_weak_handler SWI02_IRQHandler
    def_weak_handler SWI03_IRQHandler
    def_weak_handler SPU00_IRQHandler
    def_weak_handler MPC00_IRQHandler
    def_weak_handler AAR00_CCM00_IRQHandler
    def_weak_handler ECB00_IRQHandler
    def_weak_handler CRACEN_IRQHandler
    def_weak_handler SERIAL00_IRQHandler
    def_weak_handler RRAMC_IRQHandler
    def_weak_handler VPR00_IRQHandler
    def_weak_handler CTRLAP_IRQHandler
    def_weak_handler CM33SS_IRQHandler
    def_weak_handler TIMER00_IRQHandler
    def_weak_handler SPU10_IRQHandler
    def_weak_handler TIMER10_IRQHandler
    def_weak_handler EGU10_IRQHandler
    def_weak_handler RADIO_0_IRQHandler
    def_weak_handler RADIO_1_IRQHandler
    def_weak_handler SPU20_IRQHandler
    def_weak_handler SERIAL20_IRQHandler
    def_weak_handler SERIAL21_IRQHandler
    def_weak_handler SERIAL22_IRQHandler
    def_weak_handler EGU20_IRQHandler
    def_weak_handler TIMER20_IRQHandler
    def_weak_handler TIMER21_IRQHandler
    def_weak_handler TIMER22_IRQHandler
    def_weak_handler TIMER23_IRQHandler
    def_weak_handler TIMER24_IRQHandler
    def_weak_handler PDM20_IRQHandler
    def_weak_handler PDM21_IRQHandler
    def_weak_handler PWM20_IRQHandler
    def_weak_handler PWM21_IRQHandler
    def_weak_handler PWM22_IRQHandler
    def_weak_handler SAADC_IRQHandler
    def_weak_handler NFCT_IRQHandler
    def_weak_handler TEMP_IRQHandler
    def_weak_handler GPIOTE20_0_IRQHandler
    def_weak_handler GPIOTE20_1_IRQHandler
    def_weak_handler TAMPC_IRQHandler
    def_weak_handler I2S20_IRQHandler
    def_weak_handler QDEC20_IRQHandler
    def_weak_handler QDEC21_IRQHandler
    def_weak_handler GRTC_0_IRQHandler
    def_weak_handler GRTC_1_IRQHandler
    def_weak_handler GRTC_2_IRQHandler
    def_weak_handler GRTC_3_IRQHandler
    def_weak_handler SPU30_IRQHandler
    def_weak_handler SERIAL30_IRQHandler
    def_weak_handler CLOCK_POWER_IRQHandler
    def_weak_handler COMP_LPCOMP_IRQHandler
    def_weak_handler WDT30_IRQHandler
    def_weak_handler WDT31_IRQHandler
    def_weak_handler GPIOTE30_0_IRQHandler
    def_weak_handler GPIOTE30_1_IRQHandler

    .weak SystemInit
    .type SystemInit, %function
SystemInit:
    bx lr
    .size SystemInit, .-SystemInit

    .end
