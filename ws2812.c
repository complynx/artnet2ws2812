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
#include "sysparam.h"

#include "sysparam_macros.h"
#include "color_conv.h"

static const char* TAG = "ws2812";

static EventGroupHandle_t ws2812_event_group;
static SemaphoreHandle_t ws2812_pixels_manipulation_lock;
static ws2812_pixel_t *pixels=NULL;
static uint8_t program = 0;
#define REFRESH_PIXELS_BIT BIT0

struct program_rainbow {
    uint8_t id; // unique id for this setting. If the same, don't update
    uint16_t delay; // in ms
    uint16_t step_time; // 0-360
    uint16_t step_length; // 0-360
    color_HSV current;
    ws2812_pixel_t tint; // pixel = tint*tint_level/255 + pixel_raw*(255-tiny_level)/255;
    uint8_t tint_level;
    uint8_t tint_type; // rgb (<128) or hsl (>=128)
};
union program_settings_t {
    struct program_rainbow rainbow;
};
static union program_settings_t program_settings = {};

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
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
            uint8_t new_id = *(rgbbytes++);
            int err;

            program_settings.rainbow.delay = *(rgbbytes++);
            program_settings.rainbow.delay <<=8;
            program_settings.rainbow.delay += *(rgbbytes++);

            program_settings.rainbow.step_time = *(rgbbytes++);
            program_settings.rainbow.step_time <<=8;
            program_settings.rainbow.step_time += *(rgbbytes++);

            program_settings.rainbow.step_length = *(rgbbytes++);
            program_settings.rainbow.step_length <<=8;
            program_settings.rainbow.step_length += *(rgbbytes++);

            if(program == new_program
                    && new_id > 0
                    && new_id == program_settings.rainbow.id) {
                LOGD("Same Rainbow ID, skipping setting starting color.");
                rgbbytes += 3;
            } else {
                program_settings.rainbow.id = new_id;
                ws2812_pixel_t begin;
                begin.red = *(rgbbytes++);
                begin.green = *(rgbbytes++);
                begin.blue = *(rgbbytes++);

                SPTW_SETR(int8,program_settings.rainbow.begin.red,begin.red,);
                SPTW_SETR(int8,program_settings.rainbow.begin.green,begin.green,);
                SPTW_SETR(int8,program_settings.rainbow.begin.blue,begin.blue,);

                rgb2hsv(&begin, &program_settings.rainbow.current);
            }
            program = new_program;

            program_settings.rainbow.tint.red = *(rgbbytes++);
            program_settings.rainbow.tint.green = *(rgbbytes++);
            program_settings.rainbow.tint.blue = *(rgbbytes++);
            program_settings.rainbow.tint_level = *(rgbbytes++);

            if(len>14){
                program_settings.rainbow.tint_type = *(rgbbytes++);
            } else {
                program_settings.rainbow.tint_type = 0;
            }

            LOGV("Delay %d, T step: %d, L step: %d, L[0] color: %.0f %.0f %.0f, tint: %02x%02x%02x, level: %d",
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


            int32_t tmp;
            tmp = program_settings.rainbow.delay;
            SPTW_SETR(int32,program_settings.rainbow.delay,tmp,);

            tmp = program_settings.rainbow.step_time;
            SPTW_SETR(int32,program_settings.rainbow.step_time,tmp,);

            tmp = program_settings.rainbow.step_length;
            SPTW_SETR(int32,program_settings.rainbow.step_length,tmp,);

            SPTW_SETNR(int8,program_settings.rainbow.tint.red,);
            SPTW_SETNR(int8,program_settings.rainbow.tint.green,);
            SPTW_SETNR(int8,program_settings.rainbow.tint.blue,);

            SPTW_SETNR(int8,program_settings.rainbow.tint_level,);
            SPTW_SETNR(int8,program_settings.rainbow.tint_type,);
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
    int err;
    TickType_t current_delay = portMAX_DELAY;

    if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 1000) == pdTRUE) {
        ws2812_i2s_init(LED_NUMBER, PIXEL_RGB);
        memset(pixels, 0, sizeof(ws2812_pixel_t) * LED_NUMBER);


        program = DMX_RAINBOW;

        int32_t tmp;
        tmp = portMAX_DELAY;
        SPTW_GETR(int32,program_settings.rainbow.delay,tmp,);
        program_settings.rainbow.delay = tmp;

        tmp = 0;
        SPTW_GETR(int32,program_settings.rainbow.step_time,tmp,);
        program_settings.rainbow.step_time = tmp;

        tmp = 0;
        SPTW_GETR(int32,program_settings.rainbow.step_length,tmp,);
        program_settings.rainbow.step_length = tmp;

        ws2812_pixel_t begin={
                .red = 0,
                .green = 0,
                .blue = 0
        };
        SPTW_GETR(int8,program_settings.rainbow.begin.red,begin.red,);
        SPTW_GETR(int8,program_settings.rainbow.begin.green,begin.green,);
        SPTW_GETR(int8,program_settings.rainbow.begin.blue,begin.blue,);

        rgb2hsv(&begin, &program_settings.rainbow.current);

        program_settings.rainbow.tint.red=0;
        program_settings.rainbow.tint.green=0;
        program_settings.rainbow.tint.blue=0;
        SPTW_GETNR(int8,program_settings.rainbow.tint.red,);
        SPTW_GETNR(int8,program_settings.rainbow.tint.green,);
        SPTW_GETNR(int8,program_settings.rainbow.tint.blue,);

        program_settings.rainbow.tint_level=0;
        program_settings.rainbow.tint_type=0;
        SPTW_GETNR(int8,program_settings.rainbow.tint_level,);
        SPTW_GETNR(int8,program_settings.rainbow.tint_type,);

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
                if(current_delay < 1) current_delay = 1;
                program_settings.rainbow.current.h += program_settings.rainbow.step_time;

                color_HSV L = program_settings.rainbow.current, LT, HSVTINT;
                int is_hsv_tint = program_settings.rainbow.tint_type >= 128;
                float tint_norm = (float)program_settings.rainbow.tint_level / 255.;

                if(is_hsv_tint){
                    rgb2hsv(&program_settings.rainbow.tint, &HSVTINT);
                }

                IFLOGV(ws2812_pixel_t p1;hsv2rgb(&L, &p1);)
                LOGV("start color: %.0f %.0f %.0f, rgb: %02x%02x%02x",
                        L.h, L.s, L.v,
                        p1.red, p1.green, p1.blue);
                for(int i=0;i<LED_NUMBER;++i){
                    ws2812_pixel_t p;
                    if(is_hsv_tint) {
                        LT.h = (L.h * (1.-tint_norm)) + (HSVTINT.h * tint_norm);
                        LT.s = (L.s * (1.-tint_norm)) + (HSVTINT.s * tint_norm);
                        LT.v = (L.v * (1.-tint_norm)) + (HSVTINT.v * tint_norm);
                        hsv2rgb(&LT, &(pixels[i]));
                    } else {
                        hsv2rgb(&L, &p);
                        pixels[i].red = (uint8_t)((((uint16_t)p.red)*(255-program_settings.rainbow.tint_level) +
                                ((uint16_t)program_settings.rainbow.tint.red)*(program_settings.rainbow.tint_level))/255);
                        pixels[i].green = (uint8_t)((((uint16_t)p.green)*(255-program_settings.rainbow.tint_level) +
                                ((uint16_t)program_settings.rainbow.tint.green)*(program_settings.rainbow.tint_level))/255);
                        pixels[i].blue = (uint8_t)((((uint16_t)p.blue)*(255-program_settings.rainbow.tint_level) +
                                ((uint16_t)program_settings.rainbow.tint.blue)*(program_settings.rainbow.tint_level))/255);
                    }

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

