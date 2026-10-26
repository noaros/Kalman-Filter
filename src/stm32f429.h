/**
 * @file    stm32f429.h
 * @brief   Minimal, hand-written register map for the STM32F429ZI.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * ST ships a full CMSIS device header (stm32f429xx.h, ~1 MB) and a HAL on
 * top of it. This project deliberately uses neither: the goal is to show
 * that *everything* the demo needs is a few dozen memory-mapped registers,
 * and that you can read the reference manual and talk to them directly.
 *
 * Every peripheral here is described as a C struct whose members are laid
 * out exactly like the hardware registers, then a pointer to that struct
 * is placed at the peripheral's base address. Accessing `ADC1->CR2` is
 * therefore just a 32-bit load/store to address 0x40012008.
 *
 * All registers are `volatile` so the compiler never caches, merges, or
 * reorders accesses to them -- each C access becomes one bus transaction.
 *
 * The `_Static_assert`s at the bottom check that each struct member sits at
 * the offset given in the reference manual. If you add a register and get
 * the padding wrong, the build fails instead of the hardware misbehaving.
 *
 * REFERENCES (all freely available from st.com)
 * ----------------------------------------------
 *   RM0090  - STM32F405/415/407/417/427/437/429/439 Reference Manual
 *             (register descriptions; section numbers below refer to it)
 *   DS9405  - STM32F427xx/STM32F429xx datasheet
 *             (memory map, electrical characteristics, calibration values)
 *   PM0214  - Cortex-M4 Programming Manual (SysTick, SCB, FPU)
 */
#ifndef STM32F429_H
#define STM32F429_H

#include <stddef.h>
#include <stdint.h>

/* "Read/write" and "read-only" markers, purely documentary. */
#define __IO volatile
#define __I  volatile const

/* ========================================================================
 *  Memory map (RM0090 section 2.3, table 1)
 * ======================================================================== */
#define PERIPH_BASE      0x40000000UL
#define APB1PERIPH_BASE  (PERIPH_BASE + 0x00000000UL)
#define APB2PERIPH_BASE  (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE  (PERIPH_BASE + 0x00020000UL)

/* ========================================================================
 *  RCC - Reset and Clock Control (RM0090 section 6.3)
 *
 *  Every peripheral on the STM32 is clock-gated after reset to save power.
 *  Before touching *any* peripheral register you have to set its enable
 *  bit in one of the RCC_xxxENR registers, otherwise writes are ignored and
 *  reads return zero.
 * ======================================================================== */
typedef struct {
    __IO uint32_t CR;          /* 0x00 clock control                       */
    __IO uint32_t PLLCFGR;     /* 0x04 PLL configuration                   */
    __IO uint32_t CFGR;        /* 0x08 clock configuration                 */
    __IO uint32_t CIR;         /* 0x0C clock interrupt                     */
    __IO uint32_t AHB1RSTR;    /* 0x10 AHB1 peripheral reset               */
    __IO uint32_t AHB2RSTR;    /* 0x14 AHB2 peripheral reset               */
    __IO uint32_t AHB3RSTR;    /* 0x18 AHB3 peripheral reset               */
    uint32_t      RESERVED0;   /* 0x1C                                     */
    __IO uint32_t APB1RSTR;    /* 0x20 APB1 peripheral reset               */
    __IO uint32_t APB2RSTR;    /* 0x24 APB2 peripheral reset               */
    uint32_t      RESERVED1[2];/* 0x28-0x2C                                */
    __IO uint32_t AHB1ENR;     /* 0x30 AHB1 peripheral clock enable        */
    __IO uint32_t AHB2ENR;     /* 0x34 AHB2 peripheral clock enable        */
    __IO uint32_t AHB3ENR;     /* 0x38 AHB3 peripheral clock enable        */
    uint32_t      RESERVED2;   /* 0x3C                                     */
    __IO uint32_t APB1ENR;     /* 0x40 APB1 peripheral clock enable        */
    __IO uint32_t APB2ENR;     /* 0x44 APB2 peripheral clock enable        */
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)(AHB1PERIPH_BASE + 0x3800UL))  /* 0x40023800 */

/* RCC_AHB1ENR bits (RM0090 6.3.10) */
#define RCC_AHB1ENR_GPIOAEN  (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN  (1UL << 1)
#define RCC_AHB1ENR_GPIOCEN  (1UL << 2)
#define RCC_AHB1ENR_GPIODEN  (1UL << 3)
/* RCC_APB1ENR bits (RM0090 6.3.13) */
#define RCC_APB1ENR_USART3EN (1UL << 18)
/* RCC_APB2ENR bits (RM0090 6.3.14) */
#define RCC_APB2ENR_ADC1EN   (1UL << 8)

/* ========================================================================
 *  FLASH interface (RM0090 section 3.9)
 *
 *  Only ACR (access control) is used: it holds the wait-state count and
 *  the enable bits for the ART accelerator (prefetch + instruction/data
 *  caches). At 16 MHz zero wait states are allowed (RM0090 table 11).
 * ======================================================================== */
typedef struct {
    __IO uint32_t ACR;         /* 0x00 access control                      */
    __IO uint32_t KEYR;        /* 0x04 key                                 */
    __IO uint32_t OPTKEYR;     /* 0x08 option key                          */
    __IO uint32_t SR;          /* 0x0C status                              */
    __IO uint32_t CR;          /* 0x10 control                             */
    __IO uint32_t OPTCR;       /* 0x14 option control                      */
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *)(AHB1PERIPH_BASE + 0x3C00UL)) /* 0x40023C00 */

#define FLASH_ACR_LATENCY_Msk (0xFUL << 0)
#define FLASH_ACR_PRFTEN      (1UL << 8)   /* prefetch enable              */
#define FLASH_ACR_ICEN        (1UL << 9)   /* instruction cache enable     */
#define FLASH_ACR_DCEN        (1UL << 10)  /* data cache enable            */

/* ========================================================================
 *  GPIO (RM0090 section 8.4)
 *
 *  Each pin uses 2 bits in MODER/OSPEEDR/PUPDR, 1 bit in OTYPER/IDR/ODR,
 *  and 4 bits in AFR[0] (pins 0-7) or AFR[1] (pins 8-15).
 * ======================================================================== */
typedef struct {
    __IO uint32_t MODER;       /* 0x00 mode: 00 in, 01 out, 10 AF, 11 analog */
    __IO uint32_t OTYPER;      /* 0x04 output type: 0 push-pull, 1 open-drain */
    __IO uint32_t OSPEEDR;     /* 0x08 output slew rate                       */
    __IO uint32_t PUPDR;       /* 0x0C pull: 00 none, 01 up, 10 down          */
    __I  uint32_t IDR;         /* 0x10 input data                             */
    __IO uint32_t ODR;         /* 0x14 output data                            */
    __IO uint32_t BSRR;        /* 0x18 bit set (low 16) / reset (high 16)     */
    __IO uint32_t LCKR;        /* 0x1C configuration lock                     */
    __IO uint32_t AFR[2];      /* 0x20 AFRL, 0x24 AFRH alternate function     */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0000UL)) /* 0x40020000 */
#define GPIOB ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0400UL)) /* 0x40020400 */
#define GPIOC ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0800UL)) /* 0x40020800 */
#define GPIOD ((GPIO_TypeDef *)(AHB1PERIPH_BASE + 0x0C00UL)) /* 0x40020C00 */

#define GPIO_MODE_INPUT   0x0UL
#define GPIO_MODE_OUTPUT  0x1UL
#define GPIO_MODE_AF      0x2UL
#define GPIO_MODE_ANALOG  0x3UL

#define GPIO_PULL_NONE    0x0UL
#define GPIO_PULL_UP      0x1UL
#define GPIO_PULL_DOWN    0x2UL

/* ========================================================================
 *  USART (RM0090 section 30.6)
 * ======================================================================== */
typedef struct {
    __IO uint32_t SR;          /* 0x00 status                              */
    __IO uint32_t DR;          /* 0x04 data (TX on write, RX on read)      */
    __IO uint32_t BRR;         /* 0x08 baud rate                           */
    __IO uint32_t CR1;         /* 0x0C control 1                           */
    __IO uint32_t CR2;         /* 0x10 control 2                           */
    __IO uint32_t CR3;         /* 0x14 control 3                           */
    __IO uint32_t GTPR;        /* 0x18 guard time / prescaler              */
} USART_TypeDef;

#define USART3 ((USART_TypeDef *)(APB1PERIPH_BASE + 0x4800UL)) /* 0x40004800 */

#define USART_SR_ORE   (1UL << 3)   /* overrun error                        */
#define USART_SR_RXNE  (1UL << 5)   /* receive register not empty           */
#define USART_SR_TC    (1UL << 6)   /* transmission complete                */
#define USART_SR_TXE   (1UL << 7)   /* transmit data register empty         */
#define USART_CR1_RE   (1UL << 2)   /* receiver enable                      */
#define USART_CR1_TE   (1UL << 3)   /* transmitter enable                   */
#define USART_CR1_UE   (1UL << 13)  /* USART enable                         */

/* ========================================================================
 *  ADC (RM0090 section 13.13)
 * ======================================================================== */
typedef struct {
    __IO uint32_t SR;          /* 0x00 status                              */
    __IO uint32_t CR1;         /* 0x04 control 1                           */
    __IO uint32_t CR2;         /* 0x08 control 2                           */
    __IO uint32_t SMPR1;       /* 0x0C sample time, channels 10..18        */
    __IO uint32_t SMPR2;       /* 0x10 sample time, channels 0..9          */
    __IO uint32_t JOFR[4];     /* 0x14-0x20 injected channel offsets       */
    __IO uint32_t HTR;         /* 0x24 watchdog high threshold             */
    __IO uint32_t LTR;         /* 0x28 watchdog low threshold              */
    __IO uint32_t SQR1;        /* 0x2C regular sequence 1 (+ length L)     */
    __IO uint32_t SQR2;        /* 0x30 regular sequence 2                  */
    __IO uint32_t SQR3;        /* 0x34 regular sequence 3 (SQ1..SQ6)       */
    __IO uint32_t JSQR;        /* 0x38 injected sequence                   */
    __I  uint32_t JDR[4];      /* 0x3C-0x48 injected data                  */
    __I  uint32_t DR;          /* 0x4C regular data                        */
} ADC_TypeDef;

/* Registers shared by ADC1/2/3 (RM0090 section 13.13.16-18) */
typedef struct {
    __I  uint32_t CSR;         /* 0x00 common status                       */
    __IO uint32_t CCR;         /* 0x04 common control                      */
    __I  uint32_t CDR;         /* 0x08 common data (dual/triple mode)      */
} ADC_Common_TypeDef;

#define ADC1       ((ADC_TypeDef *)(APB2PERIPH_BASE + 0x2000UL))        /* 0x40012000 */
#define ADC_COMMON ((ADC_Common_TypeDef *)(APB2PERIPH_BASE + 0x2300UL)) /* 0x40012300 */

#define ADC_SR_EOC          (1UL << 1)   /* end of conversion              */
#define ADC_CR1_RES_Msk     (3UL << 24)  /* 00 = 12-bit resolution         */
#define ADC_CR2_ADON        (1UL << 0)   /* ADC on                         */
#define ADC_CR2_CONT        (1UL << 1)   /* continuous conversion          */
#define ADC_CR2_ALIGN       (1UL << 11)  /* 0 = right aligned              */
#define ADC_CR2_SWSTART     (1UL << 30)  /* start regular conversion       */
#define ADC_SQR1_L_Msk      (0xFUL << 20)/* sequence length - 1            */
#define ADC_CCR_ADCPRE_Pos  16           /* ADCCLK = PCLK2 / (2,4,6,8)     */
#define ADC_CCR_ADCPRE_Msk  (3UL << ADC_CCR_ADCPRE_Pos)
#define ADC_CCR_VBATE       (1UL << 22)  /* VBAT channel enable            */
#define ADC_CCR_TSVREFE     (1UL << 23)  /* temp sensor + VREFINT enable   */

/* Sample-time encoding for SMPRx (3 bits per channel) */
#define ADC_SMP_480CYC      0x7UL

/* ========================================================================
 *  Factory calibration values in system memory (DS9405 section 6.3.22/23)
 *
 *  ST measures every chip during production test and burns the raw ADC
 *  readings into a read-only area of system memory. All were taken with
 *  VDDA = VREF+ = 3.3 V (+-10 mV).
 * ======================================================================== */
#define VREFINT_CAL_ADDR ((const volatile uint16_t *)0x1FFF7A2AUL) /* VREFINT @ 30 C  */
#define TS_CAL1_ADDR     ((const volatile uint16_t *)0x1FFF7A2CUL) /* temp sensor @ 30 C  */
#define TS_CAL2_ADDR     ((const volatile uint16_t *)0x1FFF7A2EUL) /* temp sensor @ 110 C */
#define TS_CAL1_TEMP_C   30.0f
#define TS_CAL2_TEMP_C   110.0f
#define CAL_VDDA_V       3.3f

/* ========================================================================
 *  Cortex-M4 core peripherals (PM0214 chapter 4)
 * ======================================================================== */
typedef struct {
    __IO uint32_t CTRL;        /* 0x00 control and status                  */
    __IO uint32_t LOAD;        /* 0x04 reload value (24 bit)               */
    __IO uint32_t VAL;         /* 0x08 current value                       */
    __I  uint32_t CALIB;       /* 0x0C calibration                         */
} SysTick_TypeDef;

#define SYSTICK ((SysTick_TypeDef *)0xE000E010UL)

#define SYSTICK_CTRL_ENABLE    (1UL << 0)
#define SYSTICK_CTRL_TICKINT   (1UL << 1)  /* raise exception on wrap      */
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)  /* 1 = processor clock          */

/* Coprocessor Access Control Register: enables the FPU (PM0214 4.6.1) */
#define SCB_CPACR (*(volatile uint32_t *)0xE000ED88UL)
#define SCB_CPACR_CP10_CP11_FULL (0xFUL << 20)

/* Memory barriers. Required after enabling the FPU (and in general after
 * changing system configuration) so that following instructions see the
 * new state. */
static inline void dsb(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void isb(void) { __asm volatile ("isb 0xF" ::: "memory"); }

/* ========================================================================
 *  Compile-time layout checks against RM0090 register maps
 * ======================================================================== */
_Static_assert(offsetof(RCC_TypeDef, AHB1ENR) == 0x30, "RCC_AHB1ENR offset");
_Static_assert(offsetof(RCC_TypeDef, APB1ENR) == 0x40, "RCC_APB1ENR offset");
_Static_assert(offsetof(RCC_TypeDef, APB2ENR) == 0x44, "RCC_APB2ENR offset");
_Static_assert(offsetof(GPIO_TypeDef, AFR) == 0x20, "GPIO_AFR offset");
_Static_assert(offsetof(USART_TypeDef, CR1) == 0x0C, "USART_CR1 offset");
_Static_assert(offsetof(ADC_TypeDef, SMPR1) == 0x0C, "ADC_SMPR1 offset");
_Static_assert(offsetof(ADC_TypeDef, SQR1) == 0x2C, "ADC_SQR1 offset");
_Static_assert(offsetof(ADC_TypeDef, SQR3) == 0x34, "ADC_SQR3 offset");
_Static_assert(offsetof(ADC_TypeDef, DR) == 0x4C, "ADC_DR offset");
_Static_assert(offsetof(ADC_Common_TypeDef, CCR) == 0x04, "ADC_CCR offset");

#endif /* STM32F429_H */
