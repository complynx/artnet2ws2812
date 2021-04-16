#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "esp/uart.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "ssid_config.h"
#include "sysparam.h"

#include "ws2812.h"
#include "art_net.h"
#include "wifi.h"
#include "logger.h"

#include "sysparam_macros.h"

static const char* TAG = "user_init";

int clean_mem() {
    uint32_t base_addr, num_sectors;
    int err;

    num_sectors = DEFAULT_SYSPARAM_SECTORS;
    base_addr = sdk_flashchip.chip_size - (5 + num_sectors) * sdk_flashchip.sector_size;

    err = sysparam_create_area(base_addr, num_sectors, true);
    if (err == SYSPARAM_OK) {
        err = sysparam_init(base_addr, 0);
        if(err != SYSPARAM_OK) {
            LOGE("failed to sysparam_init (%d)", err);
            return -1;
        }
    } else {
        LOGE("failed to sysparam_create_area (%d)", err);
        return -1;
    }
    int8_t memory_ok_magic = MEMORY_OK_MAGIC;
    SPTW_SETR(int8, memory_ok_magic, memory_ok_magic, return -3);
    return 0;
}


void user_init(void)
{
    uart_set_baud(0, 115200);

    uint32_t base_addr,num_sectors;

    int err = sysparam_get_info(&base_addr, &num_sectors);
    if (err == SYSPARAM_OK) {
        LOGD("current sysparam region is at 0x%08x (%d sectors)", base_addr, num_sectors);
    } else {
        err = sysparam_init(base_addr, 0);
        if(err != SYSPARAM_OK) {
            LOGI("No current sysparam region (initialization problem during boot?)");
            if(clean_mem() != 0){
                LOGE("Cleaning memory failed.");
                return;
            }
        }
    }

    int8_t memory_ok_magic = MEMORY_OK_MAGIC-1;
    SPTW_GETR(int8, memory_ok_magic, memory_ok_magic, return);
    if(memory_ok_magic != MEMORY_OK_MAGIC) {
        LOGI("Memory magic mismatched, clearing memory..");
        if(clean_mem() != 0) {
            LOGE("Cleaning memory failed.");
            return;
        }
    }


    wifi_init();
    ws2812_init();
    init_server();
}
