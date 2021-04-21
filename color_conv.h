/*
 * color_conv.h
 *
 *  Created on: 19 апр. 2021 г.
 *      Author: compl
 */

#ifndef COLOR_CONV_H_
#define COLOR_CONV_H_

#include <stdint.h>

#ifdef __xtensa__
#include "ws2812_i2s/ws2812_i2s.h"
#else
typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} ws2812_pixel_t;
#endif

#define HUE_UNDEFINED (-0.000001)

typedef struct {
    float h;       // angle in degrees
    float s;       // 0-255
    float v;       // 0-255
} color_HSV;

void rgb2hsv(ws2812_pixel_t*in, color_HSV*out);
void hsv2rgb(color_HSV*in, ws2812_pixel_t*out);

#endif /* COLOR_CONV_H_ */
