/**
 * @file    temp_sensor.h
 * @brief   On-chip temperature sensor read through ADC1, with factory
 *          calibration and VDDA compensation.
 */
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/** Calibration data used to turn ADC codes into degrees Celsius. */
typedef struct {
    uint16_t ts_cal1;      /* ADC code at 30 C, VDDA = 3.3 V (factory)    */
    uint16_t ts_cal2;      /* ADC code at 110 C, VDDA = 3.3 V (factory)   */
    uint16_t vrefint_cal;  /* VREFINT ADC code at VDDA = 3.3 V (factory)  */
    float    vrefint_avg;  /* VREFINT ADC code measured now (averaged)    */
    float    vdda_v;       /* VDDA derived from the two values above      */
    bool     factory_cal;  /* false -> fell back to datasheet typicals    */
} temp_cal_t;

/** Power up ADC1, enable the sensor and VREFINT, measure VDDA. */
void temp_sensor_init(void);

/** One raw 12-bit conversion of the temperature sensor channel. */
uint16_t temp_sensor_read_raw(void);

/** Convert a raw temperature-sensor code to degrees Celsius. */
float temp_sensor_raw_to_celsius(uint16_t raw);

/** The calibration in use (for printing at start-up). */
const temp_cal_t *temp_sensor_calibration(void);

#endif /* TEMP_SENSOR_H */
