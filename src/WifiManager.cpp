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
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
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
#define SMARTCONFIG_STOP_BIT            BIT5
#define ESPTOUCH_DONE_BIT               BIT6

#define SCAN_START_BIT                  BIT7
#define SCAN_DONE_BIT                   BIT8
#define SCAN_STOP_BIT                   BIT9

#define STA_CONNECT                     BIT10
#define STA_CONNECTED                   BIT11

#define AP_CONNECT                      BIT12
#define AP_CONNECTED                    BIT13

namespace FEmbed {

std::shared_ptr<OSSignal> s_wifi_signal = nullptr;
wifi_ap_record_t *s_ap_records;

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
            log_i("SSID:%s", wifi_config->sta.ssid);
            log_i("PASSWORD:%s", wifi_config->sta.password);
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        }
        case SC_STATUS_LINK_OVER:
            log_i("SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                sc_callback_data_t *sc_callback_data = (sc_callback_data_t *)pdata;
                switch (sc_callback_data->type) {
                    case SC_ACK_TYPE_ESPTOUCH:
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
        break;
    case SYSTEM_EVENT_STA_STOP:
        break;
    case SYSTEM_EVENT_STA_CONNECTED:

        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        log_e("Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        static uint8_t retry_connect = 0;
        if(WifiManager::get()->wifiState() == WIFI_STATE_SMARTCONFIG && retry_connect++ > 2)
        {
            /**
             * When in smartconfig mode, after 3 times retry, then re-enter
             * smartconfig start mode.
             */
            retry_connect = 0;
            s_wifi_signal->set(SMARTCONFIG_BIT);
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

WifiManager::WifiManager()
    : OSTask("WifiManager")
{
    s_ap_records = NULL;
    s_wifi_signal.reset(new OSSignal());
    m_wifi_state = 0;
}

WifiManager::~WifiManager()
{
    
}

void  WifiManager::init()
{
    wifi_config_t wifi_config;
    wifi_mode_t wifi_mode;

    log_i("Wifi start!");
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    if(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config) == ESP_OK)
    {
        strncpy((char *)m_sta_ssid, (const char *)wifi_config.sta.ssid, 32);
        strncpy((char *)m_sta_password, (const char *)wifi_config.sta.password, 64);
    }
    if(esp_wifi_get_config(ESP_IF_WIFI_AP, &wifi_config) == ESP_OK)
    {
        strncpy((char *)m_ap_ssid, (const char *)wifi_config.sta.ssid, 32);
        strncpy((char *)m_ap_password, (const char *)wifi_config.sta.password, 64);
    }
    esp_wifi_get_mode(&wifi_mode);

    /* All configuration default storage to RAM. */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if(wifi_mode == WIFI_MODE_STA)
    {
        if(strlen(m_sta_ssid) > 0 && strlen(m_sta_ssid) < 32)
        {
            this->startSTAConnect();
        }
        else
        {
            this->startSmartConfig();
        }
    }
    else
    {
        this->startAPConnect();
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(wifi_mode) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void WifiManager::loop()
{
    wifi_config_t wifi_cfg;
    int bits = 0;
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
                log_i("WiFi Connected to ap");      ///< Update WifiState
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
                ESP_ERROR_CHECK( esp_wifi_disconnect() );
                ESP_ERROR_CHECK( esp_smartconfig_stop());
                ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
                ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
                m_wifi_state = WIFI_STATE_SMARTCONFIG;
            }
            if(bits & SMARTCONFIG_STOP_BIT) {
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
                m_wifi_state = WIFI_STATE_STA;
                memset(&wifi_cfg, 0, sizeof(wifi_config_t));
                strcpy((char *)&wifi_cfg.sta.ssid, m_sta_ssid);
                if(strlen(m_sta_password))
                    strcpy((char *)&wifi_cfg.sta.password, m_sta_password);
                else
                    wifi_cfg.sta.password[0] = 0;
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
                ESP_ERROR_CHECK( esp_wifi_connect() );
                log_d("Connect to AP:%s, with pass:%s", wifi_cfg.sta.ssid, wifi_cfg.sta.password);
            }
            if(bits & STA_CONNECTED) {
                m_wifi_state = WIFI_STATE_STA_CONNECTED;
                ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_cfg));
                esp_wifi_set_storage(WIFI_STORAGE_FLASH);
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
                esp_wifi_set_storage(WIFI_STORAGE_RAM);
            }
            if(bits & AP_CONNECT) {
                m_wifi_state = WIFI_STATE_AP;
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

void WifiManager::setSTASsid(const char *ssid)
{
    strncpy((char *)m_sta_ssid, ssid, 32);
}

void WifiManager::setSTAPassword(const char *password)
{
    strncpy((char *)m_sta_password, password, 64);
}

void WifiManager::setSTASsidAndPassword(const char *ssid, const char *password)
{
    this->setSTASsid(ssid);
    this->setSTAPassword(password);
}


void WifiManager::startSTAConnect()
{
    if(s_wifi_signal)
        s_wifi_signal->set(STA_CONNECT);
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
        s_wifi_signal->set(AP_CONNECT);
}

}
