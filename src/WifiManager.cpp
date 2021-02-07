/* X-Cheng Wifi Module Source
 * Copyright (c) 2018-2028 Gene Kong
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "Arduino.h"

#include "WifiManager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"

#include "string.h"

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WifiManager"

#define CONNECT_BIT                     BIT0
#define CONNECTED_BIT                   BIT1
#define DISCONNECT_BIT                  BIT2
#define DISCONNECTED_BIT                BIT3

#define SMARTCONFIG_BIT                 BIT4
#define SMARTCONFIG_START_BIT           BIT5
#define SMARTCONFIG_STOP_BIT            BIT6
#define ESPTOUCH_DONE_BIT               BIT7

#define SCAN_START_BIT                  BIT8
#define SCAN_DONE_BIT                   BIT9
#define SCAN_STOP_BIT                   BIT10

#define STA_CONNECT                     BIT11
#define STA_CONNECTED                   BIT12

#define AP_CONNECT                      BIT13
#define AP_CONNECTED                    BIT14

namespace FEmbed {

std::shared_ptr<OSSignal> s_wifi_signal = nullptr;
wifi_ap_record_t *s_ap_records;
uint8_t s_phone_ip[4];              ///< smartconfig/ap-config phone's ip address.
bool s_wifi_is_init = true;

#if defined(ESP8266)
static void sc_callback(smartconfig_status_t status, void *pdata)
{
    switch (status) {
        case SC_STATUS_WAIT:
            log_i("SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            log_i("SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            log_i("SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
        {
            log_i("SC_STATUS_LINK");
            wifi_config_t *wifi_config = (wifi_config_t *)pdata;
            log_d("SSID:%s", wifi_config->sta.ssid);
            log_d("PASSWORD:%s", wifi_config->sta.password);
            esp_wifi_disconnect();
            ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        }
        case SC_STATUS_LINK_OVER:
            log_i("SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                sc_callback_data_t *sc_callback_data = (sc_callback_data_t *)pdata;
                switch (sc_callback_data->type) {
                    case SC_ACK_TYPE_ESPTOUCH:
                        memcpy(s_phone_ip, sc_callback_data->ip, 4);
                        log_i("Phone ip: %d.%d.%d.%d", sc_callback_data->ip[0], sc_callback_data->ip[1], sc_callback_data->ip[2], sc_callback_data->ip[3]);
                        log_i("TYPE: ESPTOUCH");
                        break;
                    case SC_ACK_TYPE_AIRKISS:
                        log_i("TYPE: AIRKISS");
                        break;
                    default:
                        log_i("TYPE: ERROR");
                        break;
                }
            }
            if(s_wifi_signal)
                s_wifi_signal->set(ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    (void) ctx;
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
    case SYSTEM_EVENT_WIFI_READY:
        break;
    case SYSTEM_EVENT_SCAN_DONE:
    {
        uint16_t num = 16;
        if(s_ap_records != NULL) vPortFree(s_ap_records);
        s_ap_records = (wifi_ap_record_t*)pvPortMalloc(sizeof(wifi_ap_record_t) * 16);
        if(s_ap_records != NULL)
        {
            esp_err_t err = esp_wifi_scan_get_ap_records(&num, s_ap_records);
            if(err == ESP_OK)
            {
            }
        }
        break;
    }
    case SYSTEM_EVENT_STA_START:
        if(WifiManager::get()->wifiState() == WIFI_STATE_SMARTCONFIG)
        {
            s_wifi_signal->set(SMARTCONFIG_START_BIT);
            log_d("Current is Smartconfig, then start!");
        }
        else
        {
            log_d("Current is Station config, then start(sta:%d)!", esp_wifi_connect());
        }
        break;
    case SYSTEM_EVENT_STA_STOP:
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        s_wifi_signal->set(STA_CONNECTED);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        log_e("Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        static uint8_t retry_connect = 0;
        if(WifiManager::get()->wifiState() == WIFI_STATE_SMARTCONFIG && retry_connect++ > 2)
        {
            wifi_config_t wifi_cfg;
            memset(&wifi_cfg, 0, sizeof(wifi_config_t));
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
            log_w("STA connected information is cleared");
            delay(1000);
            esp_restart();
        }
        else
            if(s_wifi_signal) s_wifi_signal->set(DISCONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        if(s_wifi_signal) s_wifi_signal->set(CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_LOST_IP:
        break;
    case SYSTEM_EVENT_AP_START:
        break;
    case SYSTEM_EVENT_AP_STOP:
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        break;
    default:
        break;
    }
    return ESP_OK;
}
#else
const smartconfig_start_config_t _sc_callback = SMARTCONFIG_START_CONFIG_DEFAULT()
const smartconfig_start_config_t *sc_callback = &_sc_callback;

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
static const int WIFI_ESPTOUCH_DONE_BIT = BIT20;
static void smartconfig_task(void * parm);
static int s_retry_num = 0;
#define CONFIG_ESP_MAXIMUM_RETRY    5
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY
#define CONFIG_ESP_WIFI_CHANNEL     1
#define CONFIG_ESP_MAX_STA_CONN     4
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define WIFI_CONNECTED_BIT          BIT21
#define WIFI_FAIL_BIT               BIT22

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static void ap_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        log_i("wifi softAP: station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        log_i("wifi softAP: station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void stasmartconfig_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if(WifiManager::get()->wifiState() == WIFI_STATE_SMARTCONFIG)
        {
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
            s_wifi_signal->set(SMARTCONFIG_START_BIT);
            log_d("Current is Smartconfig, then start!");
        }
        else
        {
            log_d("Current is Station config, then start(sta:%d)!", esp_wifi_connect());
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_retry_num++;
            log_i("wifi station: retry to connect to the AP");
        } else {
            if(s_wifi_signal) s_wifi_signal->set(DISCONNECTED_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        log_i("wifi station: got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if(s_wifi_signal) s_wifi_signal->set(CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        log_i("smartconfig: Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        log_i("smartconfig: Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        log_i("smartconfig: Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        log_i("smartconfig: SSID:%s", ssid);
        log_i("smartconfig: PASSWORD:%s", password);
        WifiManager::get()->setSTASsidAndPassword((const char *)ssid, (const char *)password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_ESPTOUCH_DONE_BIT);
    }
}

static void smartconfig_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | WIFI_ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            log_i("smartconfig: WiFi Connected to ap");
        }
        if(uxBits & WIFI_ESPTOUCH_DONE_BIT) {
            log_i("smartconfig: smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}
#endif

WifiManager::WifiManager()
    : OSTask("WifiManager")
{
    s_ap_records = NULL;
    s_wifi_signal.reset(new OSSignal());
    m_wifi_state = WIFI_STATE_SMARTCONFIG;
}

WifiManager::~WifiManager()
{
}

void  WifiManager::init()
{
    wifi_config_t wifi_config;
    wifi_mode_t wifi_mode;

    log_i("Wifi start!");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    esp_wifi_get_mode(&wifi_mode);

    /* All configuration default storage to RAM. */
    //esp_wifi_set_storage(WIFI_STORAGE_RAM);

    log_i("WIFI mode is %d", wifi_mode);
    if(wifi_mode == WIFI_MODE_STA)
    {
        s_wifi_event_group = xEventGroupCreate();
        esp_netif_create_default_wifi_sta();

        if(esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK)
        {
            strncpy((char *)m_sta_ssid, (const char *)wifi_config.sta.ssid, 32);
            strncpy((char *)m_sta_password, (const char *)wifi_config.sta.password, 64);
            esp_wifi_get_mac(WIFI_IF_STA, m_mac);
            log_i("STA ssid: %s, STA password: %s", m_sta_ssid,m_sta_password);
        }
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

        ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &stasmartconfig_event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &stasmartconfig_event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &stasmartconfig_event_handler, NULL) );
        if(strlen(m_sta_ssid) > 0 && strlen(m_sta_ssid) < 32)
        {
            m_wifi_state = WIFI_STATE_STA;
            this->startSTAConnect();
            log_i("Start STA Connect!");
        }
        else
        {
            this->startSmartConfig();
            log_i("Start SmartConfig!");
        }
    }
    else
    {
        esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL));
        if(esp_wifi_get_config(WIFI_IF_AP, &wifi_config) == ESP_OK)
        {
            strncpy((char *)m_ap_ssid, (const char *)wifi_config.ap.ssid, 32);
            strncpy((char *)m_ap_password, (const char *)wifi_config.ap.password, 64);
            esp_wifi_get_mac(WIFI_IF_AP, m_mac);
            tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &m_adp_ip);
        }
        wifi_config.ap.ssid_len = strlen((const char *)wifi_config.ap.ssid);
        wifi_config.ap.channel = EXAMPLE_ESP_WIFI_CHANNEL;
        wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        if (strlen((const char *)wifi_config.ap.password) == 0) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        log_i("wifi_init_softap finished. SSID:%s password:%s channel:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.channel);

        this->startAPConnect();
    }
}

void WifiManager::loop()
{
    wifi_config_t wifi_cfg;
    int bits = 0;
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    this->init();
    for(;;)
    {
        if(s_wifi_signal)
        {
            bits = s_wifi_signal->wait(0xffff, 100);
            if(bits & CONNECT_BIT) {
                if(esp_wifi_connect())
                {
                    log_w("Disconnected Wifi Module failed!");
                }
            }
            if(bits & CONNECTED_BIT) {
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &m_adp_ip);
                m_wifi_state = WIFI_STATE_STA_CONNECTED;
                log_i("WiFi Connected to ap");          ///< Update WifiState
            }
            if(bits & DISCONNECT_BIT) {
                if(esp_wifi_disconnect())
                {
                    log_w("Disconnected Wifi Module failed!");
                }
            }
            if(bits & DISCONNECTED_BIT) {
                log_i("WiFi disconnected to ap");       ///< Update WifiState
                esp_wifi_connect();                     ///< Always retry to connected.
                if(m_wifi_state == WIFI_STATE_STA_CONNECTED)
                {
                    m_wifi_state = WIFI_STATE_STA;
                }
            }
            if(bits & SMARTCONFIG_BIT)
            {
                if(m_wifi_state != WIFI_STATE_SMARTCONFIG)
                {
                    memset(&wifi_cfg, 0, sizeof(wifi_config_t));
                    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
                    delay(1000);
                    esp_restart();
                }
                else
                {
                    //esp_wifi_disconnect();
                    //esp_smartconfig_stop();
                    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
                    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
                    ESP_ERROR_CHECK( esp_wifi_start() );
                    m_wifi_state = WIFI_STATE_SMARTCONFIG;
                }
                s_wifi_is_init = false;
            }
            if(bits & SMARTCONFIG_START_BIT) {
                // ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
                // ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
            }
            if(bits & SMARTCONFIG_STOP_BIT) {
                log_d("Try stop smartconfig.");
                if(esp_smartconfig_stop())
                {
                    log_w("Stop smartconfig failed!");
                }
            }
            if(bits & ESPTOUCH_DONE_BIT) {
                esp_smartconfig_stop();
            }

            if(bits & SCAN_START_BIT) {

            }
            if(bits & SCAN_DONE_BIT) {

            }
            if(bits & SCAN_STOP_BIT) {

            }
            if(bits & STA_CONNECT) {
                log_d("Try start sta connecting...");
                m_wifi_state = WIFI_STATE_STA;
                memset(&wifi_cfg, 0, sizeof(wifi_config_t));
                strcpy((char *)wifi_cfg.sta.ssid, m_sta_ssid);
                if(strlen(m_sta_password))
                    strcpy((char *)wifi_cfg.sta.password, m_sta_password);
                else
                    wifi_cfg.sta.password[0] = 0;
                wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                wifi_cfg.sta.pmf_cfg.capable = true;
                wifi_cfg.sta.pmf_cfg.required = false;
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                esp_wifi_stop();
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
                ESP_ERROR_CHECK(esp_wifi_start());
                ESP_ERROR_CHECK( esp_wifi_connect() );
#if TEST
                log_d("Connect to AP:%s, with pass:%s", wifi_cfg.sta.ssid, wifi_cfg.sta.password);
#else
                log_d("Connect to AP:%s, with pass:***", wifi_cfg.sta.ssid);
#endif
                s_wifi_is_init = false;
            }
            if(bits & STA_CONNECTED) {
                if(m_wifi_state == WIFI_STATE_STA)
                    m_wifi_state = WIFI_STATE_STA_CONNECTED;
#if 0
                ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg));
                ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
                ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
#endif
            }
            if(bits & AP_CONNECT) {
                m_wifi_state = WIFI_STATE_AP;
                memset(&wifi_cfg, 0, (size_t)sizeof(wifi_ap_config_t));
                strcpy((char *)wifi_cfg.ap.ssid, m_ap_ssid);
                wifi_cfg.ap.ssid_len = strlen(m_ap_ssid);
                strcpy((char *)wifi_cfg.ap.password, m_ap_password);
                wifi_cfg.ap.max_connection = 4;
                wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
                if (strlen(m_ap_password) == 0) {
                    wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
                }
                log_d("Setup AP hotspot:%s, with pass:%s", wifi_cfg.ap.ssid, wifi_cfg.ap.password);
                esp_wifi_stop();
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
                delay(500);
                if(!s_wifi_is_init)
                {
                    esp_restart();  //Just restart for fast ap-config
                    s_wifi_is_init = false;
                }
                else
                {
                    ESP_ERROR_CHECK(esp_wifi_start());
                }
            }
            if(bits & AP_CONNECTED) {
            }
        }
        else
        {
            delay(1000);
        }
    }
}
/**
 * @brief     Connect the ESP WiFi station to the AP.
 */
void WifiManager::connect()
{
    if(s_wifi_signal)
        s_wifi_signal->set(CONNECT_BIT);
}

/**
 * @brief     Disconnect the ESP WiFi station from the AP.
 *
 */
void WifiManager::disconnect()
{
    if(s_wifi_signal)
        s_wifi_signal->set(DISCONNECT_BIT);
}

void WifiManager::startScan()
{
    if(s_wifi_signal)
        s_wifi_signal->set(SCAN_START_BIT);
}

void WifiManager::stopScan()
{
    if(s_wifi_signal)
        s_wifi_signal->set(SCAN_STOP_BIT);
}

void WifiManager::startSmartConfig()
{
    if(s_wifi_signal)
        s_wifi_signal->set(SMARTCONFIG_BIT);
}

void WifiManager::stopSmartConfig()
{
    if(s_wifi_signal)
        s_wifi_signal->set(SMARTCONFIG_STOP_BIT);
}

shared_ptr<String> WifiManager::getWebsocketHost()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "host", NULL, &size) == ESP_OK && size > 0)
        {
            char *host = (char *)malloc(size);
            nvs_get_str(nvs_h, "host", host, &size);
            ret.reset(new String(host));
            free(host);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

shared_ptr<String> WifiManager::getWebsocketUrl()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "url", NULL, &size) == ESP_OK && size > 0)
        {
            char *url = (char *)malloc(size);
            nvs_get_str(nvs_h, "url", url, &size);
            ret.reset(new String(url));
            free(url);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

shared_ptr<String> WifiManager::getWebsocketProtocol()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "prot", NULL, &size) == ESP_OK && size > 0)
        {
            char *prot = (char *)malloc(size);
            nvs_get_str(nvs_h, "prot", prot, &size);
            ret.reset(new String(prot));
            free(prot);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

uint32_t WifiManager::getWebsocketPort()
{
    uint32_t port = 0;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        nvs_get_u32(nvs_h, "port", &port);
        nvs_close(nvs_h);
    }
    return port;
}

shared_ptr<String> WifiManager::getWebsocketUser()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "user", NULL, &size) == ESP_OK && size > 0)
        {
            char *user = (char *)malloc(size);
            nvs_get_str(nvs_h, "user", user, &size);
            ret.reset(new String(user));
            free(user);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

shared_ptr<String> WifiManager::getWebsocketPass()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "pass", NULL, &size) == ESP_OK && size > 0)
        {
            char *pass = (char *)malloc(size);
            nvs_get_str(nvs_h, "pass", pass, &size);
            ret.reset(new String(pass));
            free(pass);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

shared_ptr<String>  WifiManager::getWebsocketCPId()
{
    shared_ptr<String> ret;
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        size_t size;
        if(nvs_get_str(nvs_h, "cpid", NULL, &size) == ESP_OK && size > 0)
        {
            char *cpid = (char *)malloc(size);
            nvs_get_str(nvs_h, "cpid", cpid, &size);
            ret.reset(new String(cpid));
            free(cpid);
        }
        nvs_close(nvs_h);
    }
    return ret;
}

void  WifiManager::saveWebsocketCPId(char *cp_id)
{
    nvs_handle nvs_h;
    esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        if(nvs_set_str(nvs_h, "cpid", cp_id) != ESP_OK)
        {
            log_d("Save cpid failed!");
        }
        nvs_close(nvs_h);
    }
}


void WifiManager::saveRawWebsocketConfig(char *buf)
{
    char *pch = strtok(buf, "\n");
    if(pch != NULL)
    {
        log_d("Ws indentify:%s", pch);
        pch = strtok(NULL, "\n");
        if(pch == NULL)
        {
            log_e("Websocket url must be set!");
            return;
        }
        std::shared_ptr<String> ws_host(new String(pch));
        log_d("Find Host:%s", pch);

        pch = strtok(NULL, "\n");
        if(pch == NULL)
        {
            log_e("Websocket port must be set!");
            return;
        }
        std::shared_ptr<String> ws_port(new String(pch));
        log_d("Find Port:%s", pch);

        std::shared_ptr<String> ws_url;
        pch = strtok(NULL, "\n");
        if(pch != NULL)
        {
            ws_url.reset(new String(pch));
            log_d("Find URL:%s", pch);
        }
        std::shared_ptr<String> ws_prot;
        pch = strtok(NULL, "\n");
        if(pch != NULL)
        {
            ws_prot.reset(new String(pch));
            log_d("Find Prot:%s", pch);
        }
        std::shared_ptr<String> ws_usr;
        std::shared_ptr<String> ws_pass;
        pch = strtok(NULL, "\n");
        if(pch != NULL)
        {
            ws_usr.reset(new String(pch));
            log_d("Find User:%s", pch);
            pch = strtok(NULL, "\n");
            if(pch != NULL)
            {
                ws_pass.reset(new String(pch));
                log_d("Find Pass:%s", pch);
            }
        }

        ///< Save config into nvs flash.
        nvs_handle nvs_h;
        esp_err_t err = nvs_open("websocket", NVS_READWRITE, &nvs_h);
        if (err != ESP_OK) {
            printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        } else {
            nvs_set_str(nvs_h, "host", ws_host->c_str());
            nvs_set_u32(nvs_h, "port", atoi(ws_port->c_str()));
            if(ws_url)
                nvs_set_str(nvs_h, "url", ws_url->c_str());
            else
                nvs_set_str(nvs_h, "url", "/");
            if(ws_prot)
                nvs_set_str(nvs_h, "prot", ws_prot->c_str());
            else
                nvs_set_str(nvs_h, "prot", "");
            if(ws_usr)
                nvs_set_str(nvs_h, "user", ws_usr->c_str());
            else
                nvs_set_str(nvs_h, "user", "");
            if(ws_pass)
                nvs_set_str(nvs_h, "pass", ws_pass->c_str());
            else
                nvs_set_str(nvs_h, "pass", "");
            if(nvs_commit(nvs_h) != ESP_OK)
            {
                log_e("Commit to nvs flash error!");
            }
            nvs_close(nvs_h);
        }
    }
}

void WifiManager::setSTASsid(const char *ssid)
{
    strncpy((char *)m_sta_ssid, ssid, 32);
    log_i("set STA Ssid: %s", ssid);
}

void WifiManager::setSTAPassword(const char *password)
{
    strncpy((char *)m_sta_password, password, 64);
    log_i("set STA Password: %s", password);
}

void WifiManager::setSTASsidAndPassword(const char *ssid, const char *password)
{
    this->setSTASsid(ssid);
    this->setSTAPassword(password);
}

void WifiManager::startSTAConnect()
{
    if(s_wifi_signal)
    {
        log_i("WIFI state is %d", m_wifi_state);
        if(m_wifi_state == 0)
        {
            this->stopSmartConfig();
        }
        s_wifi_signal->set(STA_CONNECT);
    }
}

void WifiManager::setAPSsid(const char *ssid)
{
    strncpy((char *)m_ap_ssid, ssid, 32);
}

void WifiManager::setAPPassword(const char *password)
{
    strncpy((char *)m_ap_password, password, 64);
}

void WifiManager::setAPSsidAndPassword(const char *ssid, const char *password)
{
    this->setAPSsid(ssid);
    this->setAPPassword(password);
}

void WifiManager::startAPConnect()
{
    if(s_wifi_signal)
    {
        if(m_wifi_state == 0)
            this->stopSmartConfig();
        s_wifi_signal->set(AP_CONNECT);
    }
}

}
