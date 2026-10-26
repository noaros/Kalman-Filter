/**
 * @file    temp_sensor.c
 * @brief   STM32F429 internal temperature sensor via ADC1 channel 18.
 *
 * THE SENSOR
 * ----------
 * Inside the die there is a diode-based sensor whose output voltage rises
 * roughly linearly with junction temperature (DS9405 table "temperature
 * sensor characteristics"):
 *
 *     V_25      ~ 0.76 V       (typical output at 25 C)
 *     Avg_Slope ~ 2.5 mV/C
 *     t_S_temp  >= 10 us       minimum ADC sampling time
 *
 * RM0090 13.10 warns that the offset varies by up to 45 C from chip to
 * chip, so the typical values are only good for relative measurements.
 * Fortunately ST measures each chip at two temperatures during production
 * and stores the raw ADC codes (TS_CAL1 at 30 C and TS_CAL2 at 110 C) in
 * system memory. A straight line through those two points gives an
 * absolute reading accurate to a few C.
 *
 * IMPORTANT: this is the temperature of the *silicon die*, not of the room.
 * It reads several degrees above ambient because the chip heats itself,
 * and it responds to the CPU load, the regulator, the ST-LINK next to it...
 * For a Kalman filter demo that's perfect: a slowly varying true value
 * buried in a few tenths of a degree of ADC noise.
 *
 * WHY MEASURE VREFINT TOO
 * -----------------------
 * The ADC measures relative to VREF+, which on the Nucleo is tied to VDDA
 * (nominally 3.3 V from the board regulator). The factory codes were
 * recorded at exactly 3.3 V. If our VDDA is, say, 3.28 V, every code reads
 * 0.6 % high, i.e. ~2 C error. VREFINT is an internal ~1.21 V bandgap
 * reference whose code at 3.3 V is also factory-stored (VREFINT_CAL):
 *
 *     VDDA = 3.3 V * VREFINT_CAL / VREFINT_measured
 *
 * and a code measured at VDDA can be rescaled to what it would have been
 * at 3.3 V by multiplying by VDDA / 3.3 V = VREFINT_CAL / VREFINT_measured.
 * VDDA is very stable on this board, so we measure it once at start-up and
 * average many samples so the correction itself adds no noise.
 *
 * ADC CONFIGURATION
 * -----------------
 *   ADCCLK  = PCLK2 / 2 = 8 MHz        (max 36 MHz; DS9405)
 *   sample  = 480 cycles = 60 us       (>= 10 us required for the sensor)
 *   convert = 12 cycles (12-bit)       -> 61.5 us per conversion
 *   trigger = software (SWSTART), single conversion, polled EOC
 *
 * On the F42x/F43x, channel 18 is shared by the temperature sensor and
 * VBAT. If VBATE is set, VBAT wins (RM0090 13.10), so we make sure it is
 * cleared.
 */
#include "temp_sensor.h"
#include "stm32f429.h"
#include "systick.h"

#define ADC_CH_VREFINT  17u
#define ADC_CH_TEMP     18u
#define VREFINT_SAMPLES 64u

/* Datasheet typicals, used only if the calibration area looks blank. */
#define TYP_V25_V        0.76f
#define TYP_SLOPE_V_PER_C 0.0025f

static temp_cal_t s_cal;

/* Precomputed so each conversion is one multiply-add:
 *   T = s_gain * raw + s_offset                                        */
static float s_gain;
static float s_offset;

/* Set the 3-bit sample-time field for channels 10..18 in SMPR1. */
static void adc_set_sample_time(uint32_t channel, uint32_t smp)
{
    const uint32_t shift = (channel - 10u) * 3u;
    ADC1->SMPR1 = (ADC1->SMPR1 & ~(7UL << shift)) | (smp << shift);
}

static uint16_t adc_convert(uint32_t channel)
{
    ADC1->SQR3 = channel;            /* SQ1 = first (only) conversion   */
    ADC1->CR2 |= ADC_CR2_SWSTART;    /* kick off; bit self-clears       */
    while ((ADC1->SR & ADC_SR_EOC) == 0u) {
        /* ~61.5 us at 8 MHz ADCCLK with 480-cycle sampling */
    }
    return (uint16_t)ADC1->DR;       /* reading DR clears EOC           */
}

void temp_sensor_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    /* Common control: ADCCLK = PCLK2/2 (ADCPRE = 00), enable the
     * temperature sensor + VREFINT, make sure VBAT is not stealing ch18. */
    ADC_COMMON->CCR = (ADC_COMMON->CCR & ~(ADC_CCR_ADCPRE_Msk | ADC_CCR_VBATE))
                    | ADC_CCR_TSVREFE;

    ADC1->CR1 &= ~ADC_CR1_RES_Msk;                /* 12-bit resolution   */
    ADC1->CR2 &= ~(ADC_CR2_CONT | ADC_CR2_ALIGN); /* single, right-align */
    ADC1->SQR1 &= ~ADC_SQR1_L_Msk;                /* sequence length = 1 */
    adc_set_sample_time(ADC_CH_TEMP, ADC_SMP_480CYC);
    adc_set_sample_time(ADC_CH_VREFINT, ADC_SMP_480CYC);

    ADC1->CR2 |= ADC_CR2_ADON;
    /* ADC needs t_STAB (~3 us) after ADON; the sensor and VREFINT need
     * t_START (~10 us) after TSVREFE. 1 ms covers both generously. */
    delay_ms(1);

    /* Read the factory calibration. */
    s_cal.ts_cal1     = *TS_CAL1_ADDR;
    s_cal.ts_cal2     = *TS_CAL2_ADDR;
    s_cal.vrefint_cal = *VREFINT_CAL_ADDR;

    /* Measure VREFINT. Throw away the first conversion (the sample
     * capacitor may hold charge from whatever was converted before). */
    (void)adc_convert(ADC_CH_VREFINT);
    uint32_t sum = 0;
    for (unsigned i = 0; i < VREFINT_SAMPLES; i++) {
        sum += adc_convert(ADC_CH_VREFINT);
    }
    s_cal.vrefint_avg = (float)sum / (float)VREFINT_SAMPLES;

    /* Sanity check: blank calibration reads 0xFFFF or 0x0000, and the
     * sensor slope is positive, so CAL2 must exceed CAL1. */
    s_cal.factory_cal = s_cal.ts_cal2 > s_cal.ts_cal1
                     && s_cal.ts_cal2 != 0xFFFFu
                     && s_cal.vrefint_cal != 0xFFFFu
                     && s_cal.vrefint_cal != 0u;

    if (s_cal.factory_cal) {
        /* Two-point linear calibration, with the raw code first rescaled
         * to the 3.3 V conditions the calibration was taken at:
         *
         *   raw33 = raw * VREFINT_CAL / VREFINT_measured
         *   T     = 30 + (raw33 - TS_CAL1) * (110 - 30) / (TS_CAL2 - TS_CAL1)
         */
        s_cal.vdda_v = CAL_VDDA_V * (float)s_cal.vrefint_cal / s_cal.vrefint_avg;
        const float vdda_scale = (float)s_cal.vrefint_cal / s_cal.vrefint_avg;
        const float c_per_code = (TS_CAL2_TEMP_C - TS_CAL1_TEMP_C)
                               / (float)(s_cal.ts_cal2 - s_cal.ts_cal1);
        s_gain   = vdda_scale * c_per_code;
        s_offset = TS_CAL1_TEMP_C - (float)s_cal.ts_cal1 * c_per_code;
    } else {
        /* Datasheet typicals: V = raw * VDDA / 4095, T = (V - V25)/slope + 25.
         * Without VREFINT_CAL we can't measure VDDA, so assume 3.3 V. */
        s_cal.vdda_v = CAL_VDDA_V;
        const float v_per_code = CAL_VDDA_V / 4095.0f;
        s_gain   = v_per_code / TYP_SLOPE_V_PER_C;
        s_offset = 25.0f - TYP_V25_V / TYP_SLOPE_V_PER_C;
    }

    (void)adc_convert(ADC_CH_TEMP);  /* discard first sensor conversion */
}

uint16_t temp_sensor_read_raw(void)
{
    return adc_convert(ADC_CH_TEMP);
}

float temp_sensor_raw_to_celsius(uint16_t raw)
{
    return s_gain * (float)raw + s_offset;
}

const temp_cal_t *temp_sensor_calibration(void)
{
    return &s_cal;
}
