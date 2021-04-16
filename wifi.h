#ifndef WIFI_H_
#define WIFI_H_

#ifndef AP_MAX_CONNECTIONS
    #define AP_MAX_CONNECTIONS 4
#elif AP_MAX_CONNECTIONS > 8
    #error ESP8266 supports not more than 8 connections
#endif

#ifndef AP_BEACON_INTERVAL
    #define AP_BEACON_INTERVAL 100
#endif

#ifndef AP_IP_SELf
    #define AP_IP_SELf "192.168.191.1"
#endif

#ifndef AP_IP_MASK
    #define AP_IP_MASK "255.255.255.0"
#endif

#ifndef AP_MAX_DHCP
    #define AP_MAX_DHCP AP_MAX_CONNECTIONS
#endif

#ifndef AP_BASE_PASS
    #define AP_BASE_PASS "hackmehackme"
#endif

#ifndef AP_BASE_SSID
    /**
     * All the hashes (#) in the string will be filled with device ID in hex representation.
     * (%0Nx where N is number of hashes)
     * Device ID will be filled in right to left mode, truncated or padded with zeros.
     * Hashes don't need to be sequential, they can appear anywhere in the string.
     */
    #define AP_BASE_SSID "ESP_########"
#endif

#ifndef STA_IDLE_TIMER
#define STA_IDLE_TIMER 1000 /* every 1 seconds */
#endif
#ifndef STA_TRIALS_BEFORE_AP
#define STA_TRIALS_BEFORE_AP 3 /* after 3 trials on each of the networks, the AP will be created */
#endif

void wifi_init();
int update_wifi_station_settings(char* buf, size_t len);
int update_wifi_ap_settings(char* buf, size_t len);

#endif /* WIFI_H_ */
