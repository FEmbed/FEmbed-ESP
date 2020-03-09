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
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        static uint8_t retry_connect = 0;
        if(WifiManager::get()->wifiState() == WIFI_STATE_SMARTCONFIG && retry_connect++ > 2)
        {
            wifi_config_t wifi_cfg;
            memset(&wifi_cfg, 0, sizeof(wifi_config_t));
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
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
    ESP_ERROR_CHECK( nvs_flash_init() );
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    esp_wifi_get_mode(&wifi_mode);

    /* All configuration default storage to RAM. */
    //esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if(wifi_mode == WIFI_MODE_STA)
    {
        if(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config) == ESP_OK)
        {
            strncpy((char *)m_sta_ssid, (const char *)wifi_config.sta.ssid, 32);
            strncpy((char *)m_sta_password, (const char *)wifi_config.sta.password, 64);
            esp_wifi_get_mac(ESP_IF_WIFI_STA, m_mac);
        }

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
        if(esp_wifi_get_config(ESP_IF_WIFI_AP, &wifi_config) == ESP_OK)
        {
            strncpy((char *)m_ap_ssid, (const char *)wifi_config.ap.ssid, 32);
            strncpy((char *)m_ap_password, (const char *)wifi_config.ap.password, 64);
            esp_wifi_get_mac(ESP_IF_WIFI_AP, m_mac);
            tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &m_adp_ip);
        }

        this->startAPConnect();
    }
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
                    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
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
                ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
                ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
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
                strcpy((char *)wifi_cfg.sta.ssid, m_sta_ssid);
                if(strlen(m_sta_password))
                    strcpy((char *)wifi_cfg.sta.password, m_sta_password);
                else
                    wifi_cfg.sta.password[0] = 0;
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                esp_wifi_stop();
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
                ESP_ERROR_CHECK(esp_wifi_start());
                //log_d("Connect to AP:%s, with pass:***", wifi_cfg.sta.ssid);
                log_d("Connect to AP:%s, with pass:%s", wifi_cfg.sta.ssid, wifi_cfg.sta.password);
                s_wifi_is_init = false;
            }
            if(bits & STA_CONNECTED) {
                if(m_wifi_state == WIFI_STATE_STA)
                    m_wifi_state = WIFI_STATE_STA_CONNECTED;
#if 0
                ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_cfg));
                ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
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
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_cfg));
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
    {
        if(m_wifi_state == 0)
            this->stopSmartConfig();
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
