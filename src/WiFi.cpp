/*
 ESP8266WiFi.cpp - WiFi library for esp8266

 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 Reworked on 28 Dec 2015 by Markus Sattler

 */
#include "WiFi.h"

extern "C" {
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_smartconfig.h>
#include <esp_mesh.h>
}

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WiFi"

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------- Debug ------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------


/**
 * Output WiFi settings to an object derived from Print interface (like Serial).
 * @param p Print interface
 */
void WiFiClass::printDiag(Print& p)
{
    const char* modes[] = { "NULL", "STA", "AP", "STA+AP" };

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    uint8_t primaryChan;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);

    p.print("Mode: ");
    p.println(modes[mode]);

    p.print("Channel: ");
    p.println(primaryChan);
    /*
        p.print("AP id: ");
        p.println(wifi_station_get_current_ap_id());

        p.print("Status: ");
        p.println(wifi_station_get_connect_status());
    */

    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);

    const char* ssid = reinterpret_cast<const char*>(conf.sta.ssid);
    p.print("SSID (");
    p.print(strlen(ssid));
    p.print("): ");
    p.println(ssid);

    const char* passphrase = reinterpret_cast<const char*>(conf.sta.password);
    p.print("Passphrase (");
    p.print(strlen(passphrase));
    p.print("): ");
    p.println(passphrase);

    p.print("BSSID set: ");
    p.println(conf.sta.bssid_set);
}

void WiFiClass::enableProv(bool status)
{
    prov_enable = status;
}

bool WiFiClass::isProvEnabled()
{
    return prov_enable;
}

void WiFiClass::loop()
{
    for(;;)
    {
        delay(1000);
    }
}

/**
 * callback for WiFi events
 * @param arg
 */
esp_err_t WiFiClass::_eventCallback(esp_event_base_t event_base,
        int32_t event_id, void* event_data)
{

    if(event_base == WIFI_EVENT)
        _wifiCallback(event_id, event_data);
    else if(event_base == IP_EVENT)
        _ipCallback(event_id, event_data);
    else if(event_base == SC_EVENT)
        _smartConfigCallback(event_id, event_data);
    else if(event_base == MESH_EVENT)
        _meshCallback(event_id, event_data);
    else if(event_base == WIFI_PROV_EVENT)
        _provCallback(event_id, event_data);
    else
        return ESP_FAIL;
    return ESP_OK;
}

static const char *s_wifi_event_str[] = {
        "WIFI_READY",                      /**< ESP32 WiFi ready */
        "SCAN_DONE",                    /**< ESP32 finish scanning AP */
        "STA_START",                    /**< ESP32 station start */
        "STA_STOP",                     /**< ESP32 station stop */
        "STA_CONNECTED",                /**< ESP32 station connected to AP */
        "STA_DISCONNECTED",             /**< ESP32 station disconnected from AP */
        "STA_AUTHMODE_CHANGE",          /**< the auth mode of AP connected by ESP32 station changed */

        "STA_WPS_ER_SUCCESS",           /**< ESP32 station wps succeeds in enrollee mode */
        "STA_WPS_ER_FAILED",            /**< ESP32 station wps fails in enrollee mode */
        "STA_WPS_ER_TIMEOUT",           /**< ESP32 station wps timeout in enrollee mode */
        "STA_WPS_ER_PIN",               /**< ESP32 station wps pin code in enrollee mode */
        "STA_WPS_ER_PBC_OVERLAP",       /**< ESP32 station wps overlap in enrollee mode */

        "AP_START",                     /**< ESP32 soft-AP start */
        "AP_STOP",                      /**< ESP32 soft-AP stop */
        "AP_STACONNECTED",              /**< a station connected to ESP32 soft-AP */
        "AP_STADISCONNECTED",           /**< a station disconnected from ESP32 soft-AP */
        "AP_PROBEREQRECVED",            /**< Receive probe request packet in soft-AP interface */

        "FTM_REPORT",                   /**< Receive report of FTM procedure */

        /* Add next events after this only */
        "STA_BSS_RSSI_LOW",             /**< AP's RSSI crossed configured threshold */
        "ACTION_TX_STATUS",             /**< Status indication of Action Tx operation */
        "ROC_DONE",                     /**< Remain-on-Channel operation complete */
};
void WiFiClass::_wifiCallback(uint32_t event_id, void* event_data)
{
    log_d("Get common WiFi evt %s.", s_wifi_event_str[event_id]);
    switch(event_id)
    {
        case WIFI_EVENT_WIFI_READY:                /**< ESP32 WiFi ready */
            break;
        case WIFI_EVENT_SCAN_DONE:                 /**< ESP32 finish scanning AP */
            scanDone();
            break;
        case WIFI_EVENT_STA_START:               /**< ESP32 station start */
            WiFiSTAClass::setStatus(WL_IDLE_STATUS);
            setStatusBits(STA_STARTED_BIT);
            if(_default_sta != NULL)
            {
                esp_netif_set_hostname((esp_netif_t*)_default_sta, WiFiSTAClass::_hostname.c_str());
                esp_wifi_connect();
            }
            else
                log_w("STA netif init failed.");
            break;
        case WIFI_EVENT_STA_STOP:                /**< ESP32 station stop */
            WiFiSTAClass::setStatus(WL_NO_SHIELD);
            clearStatusBits(STA_STARTED_BIT | STA_CONNECTED_BIT | STA_HAS_IP_BIT | STA_HAS_IP6_BIT);
            break;
        case WIFI_EVENT_STA_CONNECTED:              /**< ESP32 station connected to AP */
            WiFiSTAClass::setStatus(WL_CONNECTED);
            setStatusBits(STA_CONNECTED_BIT);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:         /**< ESP32 station disconnected from AP */
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            uint8_t reason = event->reason;
            log_w("Reason: %u.", reason);
            if(reason == WIFI_REASON_NO_AP_FOUND) {
                WiFiSTAClass::setStatus(WL_NO_SSID_AVAIL);
            } else if(reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_ASSOC_FAIL) {
                WiFiSTAClass::setStatus(WL_CONNECT_FAILED);
            } else if(reason == WIFI_REASON_BEACON_TIMEOUT || reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
                WiFiSTAClass::setStatus(WL_CONNECTION_LOST);
            } else if(reason == WIFI_REASON_AUTH_EXPIRE) {

            } else {
                WiFiSTAClass::setStatus(WL_DISCONNECTED);
            }
            clearStatusBits(STA_CONNECTED_BIT | STA_HAS_IP_BIT | STA_HAS_IP6_BIT);
            if(((reason == WIFI_REASON_AUTH_EXPIRE) ||
                (reason >= WIFI_REASON_BEACON_TIMEOUT && reason != WIFI_REASON_AUTH_FAIL)) &&
               WiFi->getAutoReconnect())
            {
                esp_wifi_connect();
            }
            break;
        }
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:      /**< the auth mode of AP connected by ESP32 station changed */
            break;
        case WIFI_EVENT_STA_WPS_ER_SUCCESS:       /**< ESP32 station wps succeeds in enrollee mode */
            break;
        case WIFI_EVENT_STA_WPS_ER_FAILED:        /**< ESP32 station wps fails in enrollee mode */
            break;
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:       /**< ESP32 station wps timeout in enrollee mode */
            break;
        case WIFI_EVENT_STA_WPS_ER_PIN:           /**< ESP32 station wps pin code in enrollee mode */
            break;
        case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP:   /**< ESP32 station wps overlap in enrollee mode */
            break;

        case WIFI_EVENT_AP_START:                 /**< ESP32 soft-AP start */
            setStatusBits(AP_STARTED_BIT);
            break;
        case WIFI_EVENT_AP_STOP:                  /**< ESP32 soft-AP stop */
            clearStatusBits(AP_STARTED_BIT | AP_HAS_CLIENT_BIT);
            break;
        case WIFI_EVENT_AP_STACONNECTED:          /**< a station connected to ESP32 soft-AP */
            setStatusBits(AP_HAS_CLIENT_BIT);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:       /**< a station disconnected from ESP32 soft-AP */
        {
            wifi_sta_list_t clients;
            if(esp_wifi_ap_get_sta_list(&clients) != ESP_OK || !clients.num){
                clearStatusBits(AP_HAS_CLIENT_BIT);
            }
            break;
        }
        case WIFI_EVENT_AP_PROBEREQRECVED:        /**< Receive probe request packet in soft-AP interface */
            break;

        case WIFI_EVENT_FTM_REPORT:               /**< Receive report of FTM procedure */
            break;

        /* Add next events after this only */
        case WIFI_EVENT_STA_BSS_RSSI_LOW:         /**< AP's RSSI crossed configured threshold */
            break;
        case WIFI_EVENT_ACTION_TX_STATUS:         /**< Status indication of Action Tx operation */
            break;
        case WIFI_EVENT_ROC_DONE:                 /**< Remain-on-Channel operation complete */
            break;
        default:
            break;
    }
}

void WiFiClass::_ipCallback(uint32_t event_id, void* event_data)
{
    switch(event_id)
    {
    case IP_EVENT_STA_GOT_IP:               /*!< station got IP from connected AP */
    {
#if 0    // espidf has useful logs.
        if(_default_sta != NULL)
        {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
        log_d("%s ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR, esp_netif_get_desc((esp_netif_t *)_default_sta),
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw));
        }
#endif
        WiFiSTAClass::setStatus(WL_CONNECTED);
        setStatusBits(STA_HAS_IP_BIT | STA_CONNECTED_BIT);
        break;
    }
    case IP_EVENT_STA_LOST_IP:              /*!< station lost IP and the IP is reset to 0 */
        WiFiSTAClass::setStatus(WL_IDLE_STATUS);
        clearStatusBits(STA_HAS_IP_BIT);
        break;
    case IP_EVENT_AP_STAIPASSIGNED:         /*!< soft-AP assign an IP to a connected station */
        break;
    case IP_EVENT_GOT_IP6:                  /*!< station or ap or ethernet interface v6IP addr is preferred */
    {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        if(event->if_index == TCPIP_ADAPTER_IF_AP){
            setStatusBits(AP_HAS_IP6_BIT);
        } else if(event->if_index == TCPIP_ADAPTER_IF_STA){
            setStatusBits(STA_CONNECTED_BIT | STA_HAS_IP6_BIT);
        } else if(event->if_index == TCPIP_ADAPTER_IF_ETH){
            setStatusBits(ETH_CONNECTED_BIT | ETH_HAS_IP6_BIT);
        }
        break;
    }
    case IP_EVENT_ETH_GOT_IP:               /*!< ethernet got IP from connected AP */
        break;
    case IP_EVENT_PPP_GOT_IP:               /*!< PPP interface got IP */
        break;
    case IP_EVENT_PPP_LOST_IP:              /*!< PPP interface lost IP */
        break;
    default:;
    }
}
