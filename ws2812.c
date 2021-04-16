#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#include "esp/uart.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <time.h>

#include "ws2812_i2s/ws2812_i2s.h"
#include "ws2812.h"
#include "logger.h"

static const char* TAG = "ws2812";

static EventGroupHandle_t ws2812_event_group;
static SemaphoreHandle_t ws2812_pixels_manipulation_lock;
static ws2812_pixel_t *pixels=NULL;
static uint8_t program = 0;
#define REFRESH_PIXELS_BIT BIT0

typedef struct {
    double h;       // angle in degrees
    double s;       // a fraction between 0 and 1
    double v;       // a fraction between 0 and 1
} color_HSV;
#define HUE_UNDEFINED -0.0000001
struct program_rainbow {
    uint8_t id; // unique id for this setting. If the same, don't update
    uint16_t delay; // in ms
    uint16_t step_time; // 0-360
    uint16_t step_length; // 0-360
    color_HSV current;
    ws2812_pixel_t tint; // pixel = tint*tint_level/255 + pixel_raw*(255-tiny_level)/255;
    uint8_t tint_level;
};
union program_settings_t {
    struct program_rainbow rainbow;
};
static union program_settings_t program_settings = {};

#define max3(a, b, c) (((a)>(b))?(((c)>(a))?(c):(a)):((c)>(b))?(c):(b))
#define min3(a, b, c) (((a)<(b))?(((c)<(a))?(c):(a)):((c)<(b))?(c):(b))
#define clip(a, M, m) ((a)>(M)?(M):(a)<(m)?(m):(a))

void rgb2hsv(ws2812_pixel_t*in, color_HSV*out)
{
    if(in == NULL || out == NULL) return;
    uint8_t min, max, delta;

    min = min3(in->red,in->green,in->blue);
    max = max3(in->red,in->green,in->blue);

    out->v = ((double)max)/255.; // v
    delta = max - min;
    if (delta == 0) {
        out->s = 0;
        out->h = HUE_UNDEFINED;
        return;
    }
    if( max > 0 ) { // NOTE: if Max is == 0, this divide would cause a crash
        out->s = ((double)delta / (double)max); // s
    } else {
        // if max is 0, then r = g = b = 0
        out->s = 0;
        out->h = HUE_UNDEFINED;
        return;
    }
    if( in->red == max )
        out->h = ((double)( in->green - in->blue )) / (double)delta; // between yellow & magenta
    else
    if( in->green == max )
        out->h = 2.0 + ((double)( in->blue - in->red )) / (double)delta;  // between cyan & yellow
    else
        out->h = 4.0 + ((double)( in->red - in->green )) / (double)delta;  // between magenta & cyan

    out->h *= 60.0;                              // to degrees

    if( out->h < 0.0 )
        out->h += 360.0; // rollover
}

void hsv2rgb(color_HSV*in, ws2812_pixel_t*out)
{
    if(in == NULL || out == NULL) return;
    double      hh, p, q, t, ff;
    long        i;

    if(in->s <= 0.0) {       // < is wrong, yet for compiler
        out->red = in->v*(255); // grey
        out->green = in->v*(255);
        out->blue = in->v*(255);
        return;
    }
    hh = in->h / 360.0;
    hh = modf(hh, &p) * 360;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in->v * (1.0 - in->s);
    q = in->v * (1.0 - (in->s * ff));
    t = in->v * (1.0 - (in->s * (1.0 - ff)));

    switch(i) {
    case 0:
        out->red = in->v*255;
        out->green = t*255;
        out->blue = p*255;
        break;
    case 1:
        out->red = q*255;
        out->green = in->v*255;
        out->blue = p*255;
        break;
    case 2:
        out->red = p*255;
        out->green = in->v*255;
        out->blue = t*255;
        break;
    case 3:
        out->red = p*255;
        out->green = q*255;
        out->blue = in->v*255;
        break;
    case 4:
        out->red = t*255;
        out->green = p*255;
        out->blue = in->v*255;
        break;
    case 5:
    default:
        out->red = in->v*255;
        out->green = p*255;
        out->blue = q*255;
        break;
    }
}

void ws2812_update(uint8_t *rgbbytes, int len) {
    LOGV("Starting ws2812_update, len %d", len);
    if(len<1) {
        LOGD("No bytes to process, skipping");
        return;
    }
    uint8_t new_program = *rgbbytes;

    ++rgbbytes;
    --len;
    LOGD("Procedure number %d, rest len %d", new_program, len);

    switch(new_program) {
    case DMX_STRAIGHT:
        len /= 3;
        if(len>LED_NUMBER) len=LED_NUMBER;
        LOGD("Starting ws2812_update DMX_STRAIGHT, len %d", len);
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
            program = new_program;
            if(pixels){
                for(int i=0;i<len;++i, rgbbytes+=3) {
                    pixels[i].red   = rgbbytes[0];
                    pixels[i].green = rgbbytes[1];
                    pixels[i].blue  = rgbbytes[2];
                }
            }
            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }
        break;
    case DMX_CHAIN:
        if(len<3) {
            LOGD("Not enough data for chain.");
            return;
        }
        LOGD("Starting ws2812_update DMX_CHAIN");
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
            program = new_program;
            if(pixels){
                for(int i=LED_NUMBER-1;i>0;--i) {
                    pixels[i].red   = pixels[i-1].red;
                    pixels[i].green = pixels[i-1].green;
                    pixels[i].blue  = pixels[i-1].blue;
                }
                pixels[0].red   = rgbbytes[0];
                pixels[0].green = rgbbytes[1];
                pixels[0].blue  = rgbbytes[2];
                LOGV("New color: %02x%02x%02x", pixels[0].red, pixels[0].green, pixels[0].blue);
            }
            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }
        break;
    case DMX_CHAIN_REVERSED:
        if(len<3){
            LOGD("Not enough data for chain.");
            return;
        }
        LOGD("Starting ws2812_update DMX_CHAIN_REVERSED");
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
            program = new_program;
            if(pixels){
                for(int i=0;i<LED_NUMBER - 1;++i) {
                    pixels[i].red   = pixels[i+1].red;
                    pixels[i].green = pixels[i+1].green;
                    pixels[i].blue  = pixels[i+1].blue;
                }
                pixels[LED_NUMBER - 1].red   = rgbbytes[0];
                pixels[LED_NUMBER - 1].green = rgbbytes[1];
                pixels[LED_NUMBER - 1].blue  = rgbbytes[2];
                LOGV("New color: %02x%02x%02x",
                        pixels[LED_NUMBER - 1].red, pixels[LED_NUMBER - 1].green, pixels[LED_NUMBER - 1].blue);
            }
            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }
        break;
    case DMX_RAINBOW:
        if(len<14){
            LOGD("Not enough data for rainbow.");
            return;
        }
        LOGD("Starting ws2812_update DMX_RAINBOW");
        if(program != new_program && program_settings.rainbow.id > 0 && *(rgbbytes) == program_settings.rainbow.id) {
            LOGD("Same Rainbow ID, skipping.");
            return;
        }
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
            program = new_program;
            program_settings.rainbow.id = *(rgbbytes++);

            program_settings.rainbow.delay = *(rgbbytes++);
            program_settings.rainbow.delay <<=8;
            program_settings.rainbow.delay += *(rgbbytes++);

            program_settings.rainbow.step_time = *(rgbbytes++);
            program_settings.rainbow.step_time <<=8;
            program_settings.rainbow.step_time += *(rgbbytes++);

            program_settings.rainbow.step_length = *(rgbbytes++);
            program_settings.rainbow.step_length <<=8;
            program_settings.rainbow.step_length += *(rgbbytes++);

            ws2812_pixel_t begin;
            begin.red = *(rgbbytes++);
            begin.green = *(rgbbytes++);
            begin.blue = *(rgbbytes++);

            rgb2hsv(&begin, &program_settings.rainbow.current);

            program_settings.rainbow.tint.red = *(rgbbytes++);
            program_settings.rainbow.tint.green = *(rgbbytes++);
            program_settings.rainbow.tint.blue = *(rgbbytes++);
            program_settings.rainbow.tint_level = *(rgbbytes++);

            LOGV("Delay %d, T step: %d, L step: %d, start color: %.0f %.0f %.0f, tint: %02x%02x%02x, level: %d",
                    program_settings.rainbow.delay,
                    program_settings.rainbow.step_time,
                    program_settings.rainbow.step_length,
                    program_settings.rainbow.current.h,
                    program_settings.rainbow.current.s,
                    program_settings.rainbow.current.v,
                    program_settings.rainbow.tint.red,
                    program_settings.rainbow.tint.green,
                    program_settings.rainbow.tint.blue,
                    program_settings.rainbow.tint_level);

            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }
        break;
    default:
        LOGW("Undefined DMX program %d", new_program);
        break;
    }
    xEventGroupSetBits(ws2812_event_group, REFRESH_PIXELS_BIT);

}
#define MAX_THROTTLE (40/portTICK_PERIOD_MS)
static void ws2812_updater(void *pvParameters) {
    static const char* TAG = "ws2812_updater";
    TickType_t current_delay = portMAX_DELAY;

    if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 1000) == pdTRUE) {
        ws2812_i2s_init(LED_NUMBER, PIXEL_RGB);
        memset(pixels, 0, sizeof(ws2812_pixel_t) * LED_NUMBER);
        xSemaphoreGive(ws2812_pixels_manipulation_lock);
    }else{
        LOGE("FAILED TO TAKE LOCK");
    }
    LOGI("Started task");

    while (1) {
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 10) == pdTRUE) {
            switch(program) {
            case DMX_RAINBOW:
                current_delay = program_settings.rainbow.delay / portTICK_PERIOD_MS;
                program_settings.rainbow.current.h += program_settings.rainbow.step_time;

                color_HSV L = program_settings.rainbow.current;

                IFLOGV(ws2812_pixel_t p1;hsv2rgb(&L, &p1);)
                LOGV("start color: %.0f %.0f %.0f, rgb: %02x%02x%02x",
                        L.h, L.s, L.v,
                        p1.red, p1.green, p1.blue);
                for(int i=0;i<LED_NUMBER;++i){
                    ws2812_pixel_t p;
                    hsv2rgb(&L, &p);
                    pixels[i].red = (uint8_t)((((uint16_t)p.red)*(255-program_settings.rainbow.tint_level) +
                            ((uint16_t)program_settings.rainbow.tint.red)*(program_settings.rainbow.tint_level))/255);
                    pixels[i].green = (uint8_t)((((uint16_t)p.green)*(255-program_settings.rainbow.tint_level) +
                            ((uint16_t)program_settings.rainbow.tint.green)*(program_settings.rainbow.tint_level))/255);
                    pixels[i].blue = (uint8_t)((((uint16_t)p.blue)*(255-program_settings.rainbow.tint_level) +
                            ((uint16_t)program_settings.rainbow.tint.blue)*(program_settings.rainbow.tint_level))/255);

                    L.h += program_settings.rainbow.step_length;
                }
                break;
            default:
                current_delay = portMAX_DELAY;
            }
            ws2812_i2s_update(pixels, PIXEL_RGB);
            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }

        if(current_delay > MAX_THROTTLE) {
            current_delay -= MAX_THROTTLE;
            vTaskDelay(MAX_THROTTLE);
            xEventGroupWaitBits(
                    ws2812_event_group,
                    REFRESH_PIXELS_BIT,
                    pdTRUE,
                    pdFALSE,
                    current_delay);
        } else {
            vTaskDelay(current_delay);
            xEventGroupClearBits(ws2812_event_group, REFRESH_PIXELS_BIT);
        }

    }
    vTaskDelete( NULL );
}

void ws2812_init() {
    pixels=malloc(sizeof(ws2812_pixel_t)*LED_NUMBER);
    ws2812_event_group = xEventGroupCreate();
    ws2812_pixels_manipulation_lock = xSemaphoreCreateMutex();
    xTaskCreate(&ws2812_updater, "ws2812_updater", 512, NULL, 10, NULL);
}

