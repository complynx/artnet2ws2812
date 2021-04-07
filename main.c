#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "esp/uart.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "ssid_config.h"

#include "ws2812.h"
#include "art_net.h"

void user_init(void)
{
    uart_set_baud(0, 115200);
    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    /* required to call wifi_set_opmode before station_set_config */
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    ws2812_init();
    init_server();
}
