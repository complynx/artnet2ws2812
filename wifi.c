#include "espressif/esp_common.h"
#include "espressif/esp_sta.h"
#include "espressif/esp_softap.h"

#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "dhcpserver.h"

#include "logger.h"
#include "wifi.h"
#include "sysparam_macros.h"
#include "ssid_config.h"


static const char *TAG = "wifi_daemon";

static ip_addr_t ap_ip_self;
static ip_addr_t ap_ip_mask;

static struct station_settings_t *sta_settings;
static SemaphoreHandle_t wifi_station_settings_manipulation_lock;
static int current_station_index=0;
static uint8_t trials_count=0, connection_status=STATION_IDLE;
static bool stated_got_ip = false;

static char *wifi_ap_ssid = NULL;
static char *wifi_ap_pass = NULL;
static int8_t wifi_ap_always=false;

static char *chip_id_str = NULL;
char *get_chip_id_str(){
    if(!chip_id_str) {
        chip_id_str = malloc(9);
        snprintf(chip_id_str, 9, "%08x", sdk_system_get_chip_id());
    }
    return chip_id_str;
}

void ap_ssid_fill(char* ap_ssid) {
    int len = strlen(ap_ssid), i;
    char*id_str = get_chip_id_str(), *p=id_str+7;
    for(i=len-1;i>=0;--i) {
        if(ap_ssid[i] == '#') {
            if(p<id_str) ap_ssid[i] = '0';
            else ap_ssid[i] = *(p--);
        }
    }
}

static char *_base_ap_ssid=NULL;
char *base_ap_ssid(){
    if(!_base_ap_ssid) {
        _base_ap_ssid = strdup(AP_BASE_SSID);
        ap_ssid_fill(_base_ap_ssid);
    }
    return _base_ap_ssid;
}


#define SPTW_GET(type, what, where) SPTW_GETR(type, what, where, vTaskDelete(NULL);\
        return)
#define SPTW_SET(type, what, where) SPTW_SETR(type, what, where, vTaskDelete(NULL);\
        return)
#define SPTW_GETN(type, what) SPTW_GET(type, what, what)
#define SPTW_SETN(type, what) SPTW_SET(type, what, what)
#define SPTW_GETNN(type, what) SPTW_GETR(type, what, what, return NULL)
#define SPTW_SETNN(type, what) SPTW_SETR(type, what, what, return NULL)

void set_mode_bit(uint8_t mode_bits) {
    uint8_t opmode = sdk_wifi_get_opmode();
    uint8_t new_mode = opmode|mode_bits;
    if(new_mode != opmode)
        sdk_wifi_set_opmode(new_mode);
}
void unset_mode_bit(uint8_t mode_bits) {
    uint8_t opmode = sdk_wifi_get_opmode();
    uint8_t new_mode = opmode & (~mode_bits);
    if(new_mode != opmode)
        sdk_wifi_set_opmode(new_mode);
}

#define SET_TEST(what, to, test) do{\
    if((what)!=(to)) {\
        LOGV("setting "TOSTRING(what)" because it's different from "TOSTRING(to)", current value is %d", (what));\
        (what)=(to);\
        test=true;\
    }\
}while(0)

void set_ap(char*ssid, char*pass, bool must_set) {
    if(!ssid || ssid[0] == 0) {
        LOGI("WiFi AP disabling");
        dhcpserver_stop();
        unset_mode_bit(SOFTAP_MODE);
        return;
    }
    bool need_setup = must_set;
    struct sdk_softap_config sap_config;
    sdk_wifi_softap_get_config(&sap_config);

    if(strcmp(ssid,(char*)sap_config.ssid)){
        need_setup = true;
        strncpy((char*)sap_config.ssid, ssid, sizeof(sap_config.ssid)-1);
        sap_config.ssid[sizeof(sap_config.ssid)-1] = 0;
    }

    SET_TEST(sap_config.ssid_hidden, 0, need_setup);
    sap_config.channel = 0;
    sap_config.max_connection = AP_MAX_CONNECTIONS;
    SET_TEST(sap_config.ssid_len, strlen((char*)sap_config.ssid), need_setup);
    SET_TEST(sap_config.beacon_interval, AP_BEACON_INTERVAL, need_setup);

    if(pass && pass[0] != 0){
        int passlen = strlen(pass);
        if(passlen<8) {
            SET_TEST(sap_config.password[0], 0, need_setup);
            SET_TEST(sap_config.authmode, AUTH_OPEN, need_setup);
            LOGW("WiFi password must be at least 8 characters long");
        } else {
            if(strcmp(pass,(char*)sap_config.password)){
                need_setup = true;
                strncpy((char*)sap_config.password, pass, sizeof(sap_config.password)-1);
                sap_config.password[sizeof(sap_config.password)-1] = 0;
            }
            SET_TEST(sap_config.authmode, AUTH_WPA2_PSK, need_setup);
        }
    } else {
        SET_TEST(sap_config.password[0], 0, need_setup);
        SET_TEST(sap_config.authmode, AUTH_OPEN, need_setup);
    }
    uint8_t opmode = sdk_wifi_get_opmode();
    if(!(opmode & SOFTAP_MODE)) need_setup = true;

    bool need_ip_setup=false;
    struct ip_info ap_ip;
    sdk_wifi_get_ip_info(SOFTAP_IF, &ap_ip);
    SET_TEST(ip_addr_get_ip4_u32(&ap_ip.gw), 0, need_ip_setup);
    SET_TEST(ip_addr_get_ip4_u32(&ap_ip.ip), ip_addr_get_ip4_u32(&ap_ip_self), need_ip_setup);
    SET_TEST(ip_addr_get_ip4_u32(&ap_ip.netmask), ip_addr_get_ip4_u32(&ap_ip_mask), need_ip_setup);
    if(need_ip_setup) {
        need_setup = true;
        sdk_wifi_set_ip_info(SOFTAP_IF, &ap_ip);
    }

    ip_addr_t first_dhcp_ip;
    ip_addr_set_ip4_u32(&first_dhcp_ip, htonl(ntohl(ip_addr_get_ip4_u32(&ap_ip.ip))+1));

    if(!need_setup) return;
    LOGI("WiFi AP creating, SSID: %s password: %s", ssid, pass);
    set_mode_bit(SOFTAP_MODE);
    sdk_wifi_softap_set_config(&sap_config);
    dhcpserver_start(&first_dhcp_ip, AP_MAX_DHCP);
}

void set_sta(char*ssid, char*pass, bool must_set) {
    if(!ssid || ssid[0] == 0) {
        LOGI("WiFi station disabling");
        unset_mode_bit(STATION_MODE);
        return;
    }
    bool need_setup = false;
    struct sdk_station_config sta_config;
    sdk_wifi_station_get_config(&sta_config);

    if(strcmp(ssid, (char*)sta_config.ssid)){
        need_setup = true;
        strncpy((char*)sta_config.ssid, ssid, sizeof(sta_config.ssid)-1);
        sta_config.ssid[sizeof(sta_config.ssid)-1] = 0;
    }
    SET_TEST(sta_config.bssid_set, 0, need_setup);
    if(!pass) pass="";

    if(strcmp(pass, (char*)sta_config.password)){
        need_setup = true;
        strncpy((char*)sta_config.password, pass, sizeof(sta_config.password)-1);
        sta_config.password[sizeof(sta_config.password)-1] = 0;
    }

    uint8_t opmode = sdk_wifi_get_opmode();
    if(!(opmode & STATION_MODE)) need_setup = true;

    if(!need_setup && !must_set) return;
    LOGI("WiFi station connecting to %s", ssid);

    if(opmode & STATION_MODE) sdk_wifi_station_disconnect();
    else set_mode_bit(STATION_MODE);

    sdk_wifi_station_set_config(&sta_config);
    sdk_wifi_station_connect();
}

struct station_settings_t {
    uint8_t trials_count;
    char* ssid;
    char* pass;
};
static const char *wifi_sta_settings_name="wifi_sta_settings";

int save_wifi_station_settings(struct station_settings_t *settings) {
    int i, err=SYSPARAM_OK;
    char*buf=NULL,*p;
    size_t len=0;
    for(i=0;settings[i].ssid;++i) {
        len += strlen(settings[i].ssid) +1;
        len += strlen(settings[i].pass) +1;
    }
    len +=2;
    buf=malloc(len);
    p = buf;
    for(i=0;settings[i].ssid;++i) {
        strcpy(p, settings[i].ssid);
        p += strlen(p) + 1;
        strcpy(p, settings[i].pass);
        p += strlen(p) + 1;
    }
    *p++=0;
    *p=0;
    if((err=sysparam_set_data(wifi_sta_settings_name, (uint8_t*)buf, len, true)) != SYSPARAM_OK) {
        LOGE("sysparam_set_data %s failed (%d)", wifi_sta_settings_name, err);
    }

    free(buf);
    return err;
}

void free_wifi_station_settings(struct station_settings_t *settings) {
    int i;
    for(i=0;settings[i].ssid;++i) {
        free(settings[i].ssid);
        free(settings[i].pass);
    }
    free(settings);
}

struct station_settings_t * parse_binary_wifi_station_settings(char*wifi_sta_settings_bin,
        size_t wifi_sta_settings_length) {

    struct station_settings_t *ret=NULL;
    size_t len, i;
    char*p,*begin;

    if(wifi_sta_settings_bin) {
        i=0;
        for(p=wifi_sta_settings_bin;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {
            for(begin=p;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {};// ssid
            IFLOGV(if(p-wifi_sta_settings_bin<wifi_sta_settings_length)LOGV("--%d ssid: %s", i,begin);)
            for(++p,begin=p;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {};// pass
            IFLOGV(if(p-wifi_sta_settings_bin<wifi_sta_settings_length)LOGV("--%d pass: %s", i,begin);)
            if(p-wifi_sta_settings_bin<wifi_sta_settings_length && !(*p)) ++i;
        }
        LOGV("Found %d stations", i);
        if(i > 0) {
            len = i;
            ret = (struct station_settings_t *)malloc(sizeof(struct station_settings_t)*(len+1));
            i=0;
            for(p=wifi_sta_settings_bin;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {
                begin=p;
                for(;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {};// ssid
                ret[i].ssid = strdup(begin);

                begin=++p;
                for(;p-wifi_sta_settings_bin<wifi_sta_settings_length && *p;++p) {};// pass
                ret[i].pass = strdup(begin);

                ret[i].trials_count = 0;

                ++i;
                if(i>=len) break;
            }
            ret[len].ssid = NULL;
            ret[len].pass = NULL;
            ret[len].trials_count = 0;
        }
    }

    return ret;
}

int update_wifi_station_settings(char* buf, size_t len){
    LOGV("Update wifi_sta_settings, len: %d", len);
    struct station_settings_t *new_settings = parse_binary_wifi_station_settings(buf, len), *old_settings=NULL;
    if(new_settings) {
        if(xSemaphoreTake(wifi_station_settings_manipulation_lock, 1000) == pdTRUE) {
            LOGV("Switching to new wifi_sta_settings");
            old_settings=sta_settings;
            sta_settings=new_settings;
            current_station_index=0;
            trials_count=0;
            stated_got_ip = false;

            set_sta(sta_settings[current_station_index].ssid, sta_settings[current_station_index].pass, false);
            save_wifi_station_settings(new_settings);
            xSemaphoreGive(wifi_station_settings_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
            free_wifi_station_settings(new_settings);
            return -1;
        }
        IFLOGI(
                LOGI("New WiFi stations:");
                struct station_settings_t *I=NULL;
                int count;
                for(I=new_settings, count=0;I->ssid;++I,++count){
                    LOGI("-%2d. SSID: %s password: %s", count, I->ssid, I->pass);
                }
        )

        if(old_settings)
            free_wifi_station_settings(old_settings);
    }else{
        LOGE("Wrong buffer");
        return -2;
    }
    return 0;
}

int update_wifi_ap_settings(char* buf, size_t len){
    LOGV("Update wifi_ap_settings, len: %d", len);
    int err;
    char *p, *begin_ap_pass, *begin_ap_ssid = buf+1;
    if(len<3) {
        LOGE("Wrong buffer (too small)");
        return -2;
    }
    int new_ap_always = buf[0]&0xff;
    p=begin_ap_ssid;
    for(;p<buf+len && *p;++p) {}
    if(p-begin_ap_ssid < 1) {
        LOGE("ap ssid is too small");
        return -2;
    }
    if(++p<buf+len) {
        begin_ap_pass = p;
    }
    for(;p<buf+len && *p;++p) {}
    if(p>=buf+len) {
        LOGE("Wrong buffer, no EOS found");
        return -2;
    }

    LOGD("Got wifi AP update, ssid tpl: %s, pass: %s, always: %d", begin_ap_ssid, begin_ap_pass, new_ap_always);
    char*old_ssid=NULL,*old_pass=NULL;
    if(xSemaphoreTake(wifi_station_settings_manipulation_lock, 1000) == pdTRUE) {
        LOGV("Saving new AP");
        old_ssid = wifi_ap_ssid;
        wifi_ap_ssid = strdup(begin_ap_ssid);
        ap_ssid_fill(wifi_ap_ssid);
        LOGD("Filled AP SSID: %s", wifi_ap_ssid);
        old_pass = wifi_ap_pass;
        wifi_ap_pass = strdup(begin_ap_pass);
        SPTW_SETR(string, wifi_ap_ssid, wifi_ap_ssid, return -3);
        SPTW_SETR(string, wifi_ap_pass, wifi_ap_pass, return -3);

        wifi_ap_always = new_ap_always;
        SPTW_SETR(int8, wifi_ap_always, wifi_ap_always, return -3);

        LOGI("New WiFi AP SSID: %s, pass: %s, always: %d", wifi_ap_ssid, wifi_ap_pass, wifi_ap_always);
        if(wifi_ap_always || connection_status != STATION_GOT_IP) {
            LOGV("Setting new AP");
            set_ap(wifi_ap_ssid, wifi_ap_pass, false);
        } else if(!wifi_ap_always && connection_status == STATION_GOT_IP) {
            LOGV("Disabling AP, because new mode is exclusive and station is connected");
            set_ap(wifi_ap_ssid, wifi_ap_pass, false);
        }
        xSemaphoreGive(wifi_station_settings_manipulation_lock);
    }else{
        LOGE("FAILED TO TAKE LOCK");
        return -1;
    }

    if(old_ssid) free(old_ssid);
    if(old_pass) free(old_pass);
    return 0;
}

struct station_settings_t * get_wifi_station_settings() {
    int err;
    size_t wifi_sta_settings_length=0,i, len;
    struct station_settings_t *ret=NULL;
    char* wifi_sta_settings_bin=NULL;
    bool is_binary;
    if((err=sysparam_get_data(wifi_sta_settings_name, (uint8_t**)&wifi_sta_settings_bin,
            &wifi_sta_settings_length, &is_binary)) < SYSPARAM_OK) {
        LOGE("sysparam_get_data %s failed (%d)", wifi_sta_settings_name, err);
        return NULL;
    }

    LOGV("Got wifi_sta_settings of len: %d", wifi_sta_settings_length);

    if(wifi_sta_settings_bin){
        ret = parse_binary_wifi_station_settings(wifi_sta_settings_bin, wifi_sta_settings_length);

        free(wifi_sta_settings_bin);
    }
    if(!ret) {
        len = 1;
        i=0;
        ret = (struct station_settings_t *)malloc(sizeof(struct station_settings_t)*(len+1));

        ret[i].ssid = strdup(WIFI_SSID);
        ret[i].pass = strdup(WIFI_PASS);
        ret[i].trials_count = 0;
        if(++i==len) goto end_of_settings; // here goto prevents accidental buffer overflow

end_of_settings:
        ret[i].ssid = NULL;
        ret[i].pass = NULL;
        ret[i].trials_count = 0;
    }
    IFLOGI(
            LOGI("WiFi stations:");
            struct station_settings_t *I=NULL;
            int count;
            for(I=ret, count=0;I->ssid;++I,++count){
                LOGI("-%2d. SSID: %s password: %s", count, I->ssid, I->pass);
            }
    )

    return ret;
}

static void wifi_daemon_task(void *pvParameters)
{
    LOGI("Started task");
    int err;

    ipaddr_aton(AP_IP_SELf, &ap_ip_self);
    ipaddr_aton(AP_IP_MASK, &ap_ip_mask);

    if(xSemaphoreTake(wifi_station_settings_manipulation_lock, 1000) == pdTRUE) {
        SPTW_GETN(string, wifi_ap_ssid);
        if (!wifi_ap_ssid) {
            wifi_ap_ssid = base_ap_ssid();
            SPTW_SETN(string, wifi_ap_ssid);
        }
        SPTW_GETN(string, wifi_ap_pass);
        if (!wifi_ap_pass) {
            wifi_ap_pass = strdup(AP_BASE_PASS);
            SPTW_SETN(string, wifi_ap_pass);
        }
        LOGV("Got wifi_ap ssid: %s, pass: %s", wifi_ap_ssid, wifi_ap_pass);

        SPTW_GETN(int8, wifi_ap_always);
        LOGV("Got wifi_ap_always: %d", wifi_ap_always);
        if(wifi_ap_always) {
            set_ap(wifi_ap_ssid, wifi_ap_pass, true);
        }
        LOGI("WiFi AP SSID: %s, pass: %s, always: %d", wifi_ap_ssid, wifi_ap_pass, wifi_ap_always);
        xSemaphoreGive(wifi_station_settings_manipulation_lock);
    }else{
        LOGE("FAILED TO TAKE LOCK");
        vTaskDelete(0);
        return;
    }

    if(xSemaphoreTake(wifi_station_settings_manipulation_lock, 1000) == pdTRUE) {
        sta_settings = get_wifi_station_settings();
        if(!sta_settings) {
            LOGE("get_wifi_station_settings() failed");
            vTaskDelete(0);
            return;
        }
        xSemaphoreGive(wifi_station_settings_manipulation_lock);
    }else{
        LOGE("FAILED TO TAKE LOCK");
        vTaskDelete(0);
        return;
    }
    set_sta(sta_settings[current_station_index].ssid, sta_settings[current_station_index].pass, true);

    while (1) {
        if(xSemaphoreTake(wifi_station_settings_manipulation_lock, 1000) == pdTRUE) {
            if(sta_settings[current_station_index].ssid){
                connection_status = sdk_wifi_station_get_connect_status();

                switch(connection_status) {
                case STATION_IDLE:
                    set_sta(sta_settings[current_station_index].ssid, sta_settings[current_station_index].pass, true);
                    break;
                case STATION_CONNECTING:
                    // noop
                    break;
                case STATION_CONNECT_FAIL:
                case STATION_WRONG_PASSWORD:
                case STATION_NO_AP_FOUND:
                    LOGI("WiFi station %s status: %s", sta_settings[current_station_index].ssid,
                            connection_status==STATION_NO_AP_FOUND ? "not found" :
                                    connection_status==STATION_WRONG_PASSWORD ? "wrong password" :
                                            "failure");
                    ++current_station_index;
                    if(!sta_settings[current_station_index].ssid){
                        current_station_index=0;
                        if(trials_count < STA_TRIALS_BEFORE_AP) ++trials_count;
                        else {
                            set_ap(wifi_ap_ssid, wifi_ap_pass, false);
                        }
                    }
                    set_sta(sta_settings[current_station_index].ssid, sta_settings[current_station_index].pass, false);
                    break;
                case STATION_GOT_IP:
                    if(!stated_got_ip) {
                        LOGI("WiFi station %s connected", sta_settings[current_station_index].ssid);
                        trials_count = 0;

                        if(!wifi_ap_always) {
                            set_ap(NULL, NULL, false);
                        }
                    }
                    break;
                default:
                    LOGE("NOT IMPLEMENTED: connection status %d", connection_status);
                }
                stated_got_ip = (connection_status == STATION_GOT_IP);
            }
            xSemaphoreGive(wifi_station_settings_manipulation_lock);
        }else{
            LOGE("FAILED TO TAKE LOCK");
        }
        vTaskDelay(STA_IDLE_TIMER / portTICK_PERIOD_MS); // throttle
    }
    vTaskDelete(NULL);
}

void wifi_init() {
    wifi_station_settings_manipulation_lock = xSemaphoreCreateMutex();
    xTaskCreate(wifi_daemon_task, TAG, 512, NULL, 1, NULL);
}
