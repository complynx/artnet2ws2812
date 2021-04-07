#ifndef __WS2812_H1__
#define __WS2812_H1__

#include <stdint.h>

#ifndef LED_NUMBER
    #define LED_NUMBER 3
#endif

void ws2812_update(uint8_t *rgbbytes, int len);
void ws2812_init();

#endif//__WS2812_H1__
