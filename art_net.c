#include "espressif/esp_common.h"

#include <unistd.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ws2812.h"
#include "art_net.h"
#include "wifi.h"
#include "logger.h"
#include "sysparam_macros.h"


#define ART_NET_DMX 0x5000
#define ART_NET_WIFI_SETTINGS_STA 0xf823
#define ART_NET_WIFI_SETTINGS_AP 0xf824
#define ART_NET_DMX_SETTINGS 0xf825
#define ART_NET_MAX_PACKET 600
static const char ART_NET_TAG[8] = "Art-Net";
static const char *TAG = ART_NET_TAG;
static uint16_t universe=ART_NET_UNIVERSE;
static uint16_t shift=ART_NET_SHIFT;

#define SEQUENCE_MIN 0x01
#define SEQUENCE_MAX 0xff

static uint8_t previous_sequence=0;
void parse_dmx(uint8_t sequence, uint16_t _universe, uint16_t length, uint8_t* values) {
    LOGD("Got Art-Net DMX seq: %d, univ: %d, len: %d, %02x%02x%02x%02x%02x%02x...",
            sequence, universe, length, values[0], values[1], values[2], values[3], values[4], values[5]);
    if(_universe != universe){
        LOGD("Wrong universe, not for us");
        return;
    }

    if(sequence>0) {
        if(sequence <= previous_sequence) {
            if(sequence>SEQUENCE_ROLLOVER_TOLERANCE + SEQUENCE_MIN ||
                    previous_sequence < SEQUENCE_MAX - SEQUENCE_ROLLOVER_TOLERANCE) {
                return;
            }
        }
    }
    previous_sequence = sequence;
    if(length>shift) ws2812_update(values+shift, length-shift);
}

int parse_dmx_settings(size_t len, uint8_t* buf) {
    LOGV("Update dmx_settings, len: %d", len);
    int32_t temp;
    int err;

    if(len<4) {
        LOGE("DMX settings buf is too small");
        return -1;
    }
    uint8_t *I=buf;

    uint16_t new_universe = I[1];
    new_universe <<= 8;
    new_universe += I[0];

    I+=2;//16

    uint16_t new_shift = I[1];
    new_shift <<= 8;
    new_shift += I[0];
    LOGV("Updating universe_number: %d, shift: %d, saving...", new_universe, new_shift);

    temp = new_universe;
    SPTW_SETR(int32, dmx_universe, temp, return -2);
    universe = new_universe;

    temp = new_shift;
    SPTW_SETR(int32, dmx_shift, temp, return -2);
    shift = new_shift;

    LOGI("New DMX Universe: %d, shift (DMX Address): %d", new_universe, new_shift);
    return 0;
}

void parse_art_net(int len, uint8_t* buf) {
    uint8_t* I, *end=buf+len;
    if(len<10 || memcmp(buf, ART_NET_TAG, sizeof(ART_NET_TAG))){
        LOGD("Packet is not Art-Net packet");
        // not art-net
        return;
    }
    I = buf+8;
    uint16_t opcode = I[1];
    opcode <<= 8;
    opcode += I[0];
    I += 2;//10
    LOGV("Got Art-Net packet with opcode %04x", opcode);
    switch(opcode){
    case ART_NET_DMX:
        if(len<18) {
            LOGD("Art-Net DMX packet has insufficient length to hold arguments");
            return; // insufficient length
        }
        if(I[0]!=0 || I[1]!=14){
            LOGW("Art-Net DMX packet protocol version mismatch. Got %02x%02x", I[0], I[1]);
            return; // protocol mismatch
        }
        I+=2;//12
        uint8_t sequence = *I;
        I+=2; //14 skip port
        uint16_t universe = I[1];
        universe <<= 8;
        universe += I[0];
        I+=2;//16
        uint16_t length = *(I++);
        length <<=8;
        length += *(I++);//18
        if(length > len-18){
            LOGW("Art-Net DMX packet length (%d) is insufficient to fit stated payload length %d",
                    len, length);
            return; // insufficient payload length
        }
        parse_dmx(sequence, universe, length, I);
        break;
    case ART_NET_WIFI_SETTINGS_STA:
        if(end-I > 0){
            int err = update_wifi_station_settings((char*)I, end-I);
            if(err != 0){
                LOGW("Art-Net WIFI_SETTINGS_STA execution failure (%d)", err);
            }
        }
        break;
    case ART_NET_WIFI_SETTINGS_AP:
        if(end-I > 0){
            int err = update_wifi_ap_settings((char*)I, end-I);
            if(err != 0){
                LOGW("Art-Net WIFI_SETTINGS_AP execution failure (%d)", err);
            }
        }
        break;
    case ART_NET_DMX_SETTINGS:
        if(end-I > 0){
            int err = parse_dmx_settings(end-I, I);
            if(err != 0){
                LOGW("Art-Net DMX_SETTINGS execution failure (%d)", err);
            }
        }
        break;
    default:
        LOGV("Ignoring opcode %04x", opcode);
        break;
    }
}

static void udp_server_task(void *pvParameters)
{
    char* rx_buffer=malloc(ART_NET_MAX_PACKET);
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    LOGI("Started task");

#if LOGGER_LEVEL >= LOGGER_DEBUG
    uint32_t s1=sdk_system_get_time(),s2,s3,s4;
    uint32_t d1,d2,d3;
#endif

    LOGI("DMX Universe: %d, shift (DMX Address): %d", universe, shift);
    while (1) {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(ART_NET_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            LOGE("Unable to create socket: errno %d", errno);
            break;
        }
        LOGD("Socket created");

        int err = bind(sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err < 0) {
            LOGE("Socket unable to bind: errno %d", errno);
        }
        LOGI("Listening on port %d", ART_NET_PORT);

        while (1) {
            IFLOGD(s2=sdk_system_get_time();
            d1=s2-s1;
            s1=s2;)
            struct sockaddr_in sourceAddr;
            socklen_t socklen = sizeof(sourceAddr);
            int len = recvfrom(sock, rx_buffer, ART_NET_MAX_PACKET, 0, (struct sockaddr *)&sourceAddr, &socklen);
            IFLOGD(s3=sdk_system_get_time();
            d2=s3-s2;)

            // Error occured during receiving
            if (len < 0) {
                LOGD(LOG_COLOR(LOG_COLOR_CYAN)"UDP process: wait packets %d, reloop: %d, total: %d"LOG_RESET_COLOR, d2,d1,d1+d2);
                LOGE("recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                LOGV("Received %d bytes from %s", len, addr_str);
                parse_art_net(len, (uint8_t*)rx_buffer);
            }
            IFLOGD(s4=sdk_system_get_time();
            d3=s4-s3;)
            LOGD(LOG_COLOR(LOG_COLOR_CYAN)"UDP process: wait packets %d, reloop: %d, process: %d, total: %d"LOG_RESET_COLOR, d2,d1,d3,d1+d2+d3);
            while((len = recvfrom(sock, rx_buffer, ART_NET_MAX_PACKET, MSG_DONTWAIT,
                    (struct sockaddr *)&sourceAddr, &socklen))>0) {} // skip packets that arrived during processing time
            if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                LOGE("while clearing, recvfrom failed: errno %d", errno);
                break;
            }
        }

        if (sock != -1) {
            LOGI("Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void init_server() {
    int err;
    int32_t temp;

    temp = universe;
    SPTW_GETR(int32, dmx_universe, temp, return);
    universe = temp;

    temp = shift;
    SPTW_GETR(int32, dmx_shift, temp, return);
    shift = temp;

    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
}











