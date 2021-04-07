#include "espressif/esp_common.h"
#include "esp/uart.h"

#include <unistd.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ws2812.h"
#include "art_net.h"
#include "logger.h"


#define ART_NET_DMX 0x5000
#define ART_NET_MAX_PACKET 600
static const char ART_NET_TAG[8] = "Art-Net";
static const char *TAG = ART_NET_TAG;


void parse_dmx(uint8_t sequence, uint16_t universe, uint16_t length, uint8_t* values) {
    LOGD("Got Art-Net DMX seq: %d, univ: %d, len: %d, %02x%02x%02x%02x%02x%02x...",
            sequence, universe, length, values[0], values[1], values[2], values[3], values[4], values[5]);

    if(length>SHIFT) ws2812_update(values+SHIFT, length-SHIFT);
}

void parse_art_net(int len, uint8_t* buf) {
    uint8_t* I;
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

    while (1) {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
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
        LOGI("Listening on port %d", PORT);

        while (1) {
            struct sockaddr_in sourceAddr;
            socklen_t socklen = sizeof(sourceAddr);
            int len = recvfrom(sock, rx_buffer, ART_NET_MAX_PACKET, 0, (struct sockaddr *)&sourceAddr, &socklen);

            // Error occured during receiving
            if (len < 0) {
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
    xTaskCreate(udp_server_task, "udp_server", 8092, NULL, 5, NULL);
}


