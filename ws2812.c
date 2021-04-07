#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#include "esp/uart.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "ws2812_i2s/ws2812_i2s.h"
#include "ws2812.h"
#include "logger.h"

static const char* TAG = "ws2812";

static EventGroupHandle_t ws2812_event_group;
static SemaphoreHandle_t ws2812_pixels_manipulation_lock;
static ws2812_pixel_t *pixels=NULL;
#define REFRESH_PIXELS_BIT BIT0

void ws2812_update(uint8_t *rgbbytes, int len) {
    len /= 3;
    if(len>LED_NUMBER) len=LED_NUMBER;
    LOGD("Starting ws2812_update, len: %d", len);
    if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 100) == pdTRUE) {
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
    xEventGroupSetBits(ws2812_event_group, REFRESH_PIXELS_BIT);
}

static void ws2812_updater(void *pvParameters) {
    static const char* TAG = "ws2812_updater";
    EventBits_t bits = REFRESH_PIXELS_BIT;

    if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 1000) == pdTRUE) {
        ws2812_i2s_init(LED_NUMBER, PIXEL_RGB);
        memset(pixels, 0, sizeof(ws2812_pixel_t) * LED_NUMBER);
        xSemaphoreGive(ws2812_pixels_manipulation_lock);
    }else{
        LOGE("FAILED TO TAKE LOCK");
    }
    LOGI("Started task");

    while (1) {
        LOGD("Sending pixels to ws2812...");
        if(xSemaphoreTake(ws2812_pixels_manipulation_lock, 10) == pdTRUE) {
            ws2812_i2s_update(pixels, PIXEL_RGB);
            xSemaphoreGive(ws2812_pixels_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }

        vTaskDelay(40 / portTICK_PERIOD_MS); // throttle
        bits = xEventGroupWaitBits(
                ws2812_event_group,
                REFRESH_PIXELS_BIT,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY);
        LOGV("got bits %d", bits);
    }
    vTaskDelete( NULL );
}

void ws2812_init() {
    pixels=malloc(sizeof(ws2812_pixel_t)*LED_NUMBER);
    ws2812_event_group = xEventGroupCreate();
    ws2812_pixels_manipulation_lock = xSemaphoreCreateMutex();
    xTaskCreate(&ws2812_updater, "ws2812_updater", 256, NULL, 10, NULL);
}

