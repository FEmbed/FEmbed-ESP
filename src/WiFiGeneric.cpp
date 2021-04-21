/*
 ESP8266WiFiGeneric.cpp - WiFi library for esp8266

 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 Port/Rewrite for FEmbed by Gene Kong, April 2021

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
#include "WiFiGeneric.h"

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
#include "lwip/ip_addr.h"
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/dns.h"
#include "esp_ipc.h"
#include "esp_smartconfig.h"
#include "esp_mesh.h"
#include "nvs_flash.h"

} //extern "C"

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WiFiGeneric"

static void event_handler(void* event_handler_arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data) {
    WiFi->_eventCallback(event_base, event_id, event_data);
}

static bool _esp_wifi_started = false;
// -----------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------- Generic WiFi function -----------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

WiFiGenericClass::WiFiGenericClass():
      _network_event_group(new FEmbed::OSSignal())
    , lowLevelInitDone(false)
{
    _persistent = true;
    _long_range = false;
    _forceSleepLastMode = WIFI_MODE_NULL;
    _default_sta = NULL;
    _default_ap = NULL;
}

WiFiGenericClass::~WiFiGenericClass()
{
}

bool WiFiGenericClass::wifiLowLevelDeinit() {
    log_d("wifiLowLevelDeinit begin");
    if(lowLevelInitDone)
    {
        //1. Unregister all WiFi events into current object.
        esp_event_handler_unregister(WIFI_EVENT,         ESP_EVENT_ANY_ID, &event_handler);
        esp_event_handler_unregister(MESH_EVENT,         ESP_EVENT_ANY_ID, &event_handler);
        esp_event_handler_unregister(SC_EVENT,             ESP_EVENT_ANY_ID, &event_handler);
        esp_event_handler_unregister(IP_EVENT,             ESP_EVENT_ANY_ID, &event_handler);
        esp_event_handler_unregister(WIFI_PROV_EVENT,     ESP_EVENT_ANY_ID, &event_handler);

        //2. Destory sta/ap netif
        if(_default_sta)
            esp_netif_destroy((esp_netif_t *)_default_sta);
        if(_default_ap)
            esp_netif_destroy((esp_netif_t *)_default_ap);

        //3. Deinit wifi driver.
        esp_wifi_deinit();
#if 0    // Not stable for del loop task.
        //4.Delete background thread.
        esp_event_loop_delete_default();

        //5. No use
        esp_netif_deinit();
#endif
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        lowLevelInitDone = false;
    }
    log_d("wifiLowLevelDeinit end");
    return true;
}


bool WiFiGenericClass::wifiLowLevelInit(bool persistent, wifi_mode_t m){
    log_d("wifiLowLevelInit begin");
    if(!lowLevelInitDone) {
        //1. Do netif init, no deinit.
        static bool initialized = false;
        if(!initialized){
            nvs_flash_init();
            esp_netif_init();

            //1.1. Background thread create.
            esp_event_loop_create_default();

            //1.2. Start Wi-Fi thread to manage wifi driver.
            WiFi->start();
        }
        //2.
        //3. Create default sta/ap netif by mode.
        switch(m)
        {
            case WIFI_MODE_STA:
                _default_sta = esp_netif_create_default_wifi_sta();
                break;
            case WIFI_MODE_AP:
                _default_ap = esp_netif_create_default_wifi_ap();
                break;
            case WIFI_MODE_APSTA:
                _default_sta = esp_netif_create_default_wifi_sta();
                _default_ap = esp_netif_create_default_wifi_ap();
                break;
            default:;
        }

        //4. Init wifi driver by default settings.
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if(err){
            log_e("esp_wifi_init %d.", err);
            return false;
        }

        //5. Register all WiFi events into current object.
        esp_event_handler_register(WIFI_EVENT,         ESP_EVENT_ANY_ID, &event_handler, this);
        esp_event_handler_register(MESH_EVENT,         ESP_EVENT_ANY_ID, &event_handler, this);
        esp_event_handler_register(SC_EVENT,         ESP_EVENT_ANY_ID, &event_handler, this);
        esp_event_handler_register(IP_EVENT,         ESP_EVENT_ANY_ID, &event_handler, this);
        esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, this);

        if(!persistent){
            esp_wifi_set_storage(WIFI_STORAGE_RAM);
        }

        initialized = true;
        lowLevelInitDone = true;
        _network_event_group->set(WIFI_DNS_IDLE_BIT);
    }
    else
    {
        if(m == WIFI_MODE_APSTA)
        {
            if(!_default_sta)
                _default_sta = esp_netif_create_default_wifi_sta();
            if(!_default_ap)
                _default_ap = esp_netif_create_default_wifi_ap();
        }
    }
    log_d("wifiLowLevelInit end");
    return true;
}

bool WiFiGenericClass::espWiFiStart() {
    if(_esp_wifi_started){
        return true;
    }
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        log_e("esp_wifi_start %d", err);
        return false;
    }
    _esp_wifi_started = true;
    return true;
}

bool WiFiGenericClass::espWiFiStop(){
    esp_err_t err;
    if(!_esp_wifi_started){
        return true;
    }
    _esp_wifi_started = false;
    err = esp_wifi_stop();
    if(err){
        log_e("Could not stop WiFi! %d", err);
        _esp_wifi_started = true;
        return false;
    }
    return wifiLowLevelDeinit();
}

int WiFiGenericClass::setStatusBits(int bits){
    return _network_event_group->set(bits);
}

int WiFiGenericClass::clearStatusBits(int bits){
    return _network_event_group->clear(bits);
}

int WiFiGenericClass::getStatusBits(){
    return _network_event_group->get();
}

int WiFiGenericClass::waitStatusBits(int bits, uint32_t timeout_ms){
    return _network_event_group->wait(bits, timeout_ms, pdFALSE, pdTRUE) & bits;
}

/**
 * Return the current channel associated with the network
 * @return channel (1-13)
 */
int32_t WiFiGenericClass::channel(void)
{
    uint8_t primaryChan = 0;
    wifi_second_chan_t secondChan = WIFI_SECOND_CHAN_NONE;
    if(!lowLevelInitDone){
        return primaryChan;
    }
    esp_wifi_get_channel(&primaryChan, &secondChan);
    return primaryChan;
}


/**
 * store WiFi config in SDK flash area
 * @param persistent
 */
void WiFiGenericClass::persistent(bool persistent)
{
    _persistent = persistent;
}

/**
 * enable WiFi long range mode
 * @param enable
 */
void WiFiGenericClass::enableLongRange(bool enable)
{
    _long_range = enable;
}

/**
 * set new mode
 * @param m WiFiMode_t
 */
bool WiFiGenericClass::mode(wifi_mode_t m)
{
    wifi_mode_t cm = getMode();
    if(cm == m) {
        return true;
    }

    log_d("mode change from %d to %d.", cm , m);
    if(m){
        if(!wifiLowLevelInit(_persistent, m)){
            return false;
        }
    } else if(cm && !m){
        return espWiFiStop();
    }

    esp_err_t err;
    err = esp_wifi_set_mode(m);
    if(err){
        log_e("Could not set mode! %d", err);
        return false;
    }
    if(_long_range) {
        if(m & WIFI_MODE_STA){
            err = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
            if(err != ESP_OK){
                log_e("Could not enable long range on STA! %d", err);
                return false;
            }
        }
        if(m & WIFI_MODE_AP){
            err = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR);
            if(err != ESP_OK){
                log_e("Could not enable long range on AP! %d", err);
                return false;
            }
        }
    }
    return true;
}

/**
 * get WiFi mode
 * @return WiFiMode
 */
wifi_mode_t WiFiGenericClass::getMode()
{
    if(!lowLevelInitDone || !_esp_wifi_started){
        return WIFI_MODE_NULL;
    }
    wifi_mode_t mode;
    if(esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT){
        log_w("WiFi not started");
        return WIFI_MODE_NULL;
    }
    return mode;
}

/**
 * control STA mode
 * @param enable bool enable STA or Deinit STA.
 * @return OK
 */
bool WiFiGenericClass::enableSTA(bool enable)
{

    wifi_mode_t currentMode = getMode();
    bool isEnabled = ((currentMode & WIFI_MODE_STA) != 0);

    if(isEnabled != enable) {
        if(enable) {
            return mode((wifi_mode_t)(currentMode | WIFI_MODE_STA));
        }
        return mode((wifi_mode_t)(currentMode & (~WIFI_MODE_STA)));
    }
    return true;
}

/**
 * control AP mode
 * @param enable bool If current is not Ap, swith to it.
 * @return ok
 */
bool WiFiGenericClass::enableAP(bool enable)
{

    wifi_mode_t currentMode = getMode();
    bool isEnabled = ((currentMode & WIFI_MODE_AP) != 0);

    if(isEnabled != enable) {
        if(enable) {
            return mode((wifi_mode_t)(currentMode | WIFI_MODE_AP));
        }
        return mode((wifi_mode_t)(currentMode & (~WIFI_MODE_AP)));
    }
    return true;
}

/**
 * control modem sleep when only in STA mode
 * @param enable bool
 * @return ok
 */
bool WiFiGenericClass::setSleep(bool enable)
{
    if((getMode() & WIFI_MODE_STA) == 0){
        log_w("STA has not been started");
        return false;
    }
    return esp_wifi_set_ps(enable?WIFI_PS_MIN_MODEM:WIFI_PS_NONE) == ESP_OK;
}

/**
 * control modem sleep when only in STA mode
 * @param mode wifi_ps_type_t
 * @return ok
 */
bool WiFiGenericClass::setSleep(wifi_ps_type_t mode)
{
    if((getMode() & WIFI_MODE_STA) == 0){
        log_w("STA has not been started");
        return false;
    }
    return esp_wifi_set_ps(mode) == ESP_OK;
}

/**
 * get modem sleep enabled
 * @return true if modem sleep is enabled
 */
bool WiFiGenericClass::getSleep()
{
    wifi_ps_type_t ps;
    if((getMode() & WIFI_MODE_STA) == 0){
        log_w("STA has not been started");
        return false;
    }
    if(esp_wifi_get_ps(&ps) == ESP_OK){
        return ps == WIFI_PS_MIN_MODEM;
    }
    return false;
}

/**
 * control wifi tx power
 * @param power enum maximum wifi tx power
 * @return ok
 */
bool WiFiGenericClass::setTxPower(wifi_power_t power){
    if((getStatusBits() & (STA_STARTED_BIT | AP_STARTED_BIT)) == 0){
        log_w("Neither AP or STA has been started");
        return false;
    }
    return esp_wifi_set_max_tx_power(power) == ESP_OK;
}

wifi_power_t WiFiGenericClass::getTxPower(){
    int8_t power;
    if((getStatusBits() & (STA_STARTED_BIT | AP_STARTED_BIT)) == 0){
        log_w("Neither AP or STA has been started");
        return WIFI_POWER_19_5dBm;
    }
    if(esp_wifi_get_max_tx_power(&power)){
        return WIFI_POWER_19_5dBm;
    }
    return (wifi_power_t)power;
}

// -----------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------ Generic Network function ---------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

/**
 * DNS callback
 * @param name
 * @param ipaddr
 * @param callback_arg
 */
static void wifi_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if(ipaddr) {
        (*reinterpret_cast<IPAddress*>(callback_arg)) = ipaddr->u_addr.ip4.addr;
    }
    WiFi->notifyWiFiDNSDone();
}

/**
 * Resolve the given hostname to an IP address.
 * @param aHostname     Name to be resolved
 * @param aResult       IPAddress structure to store the returned IP address
 * @return 1 if aIPAddrString was successfully converted to an IP address,
 *          else error code
 */
int WiFiGenericClass::hostByName(const char* aHostname, IPAddress& aResult)
{
    ip_addr_t addr;
    aResult = static_cast<uint32_t>(0);
    waitStatusBits(WIFI_DNS_IDLE_BIT, 16000);
    clearStatusBits(WIFI_DNS_IDLE_BIT | WIFI_DNS_DONE_BIT);
    err_t err = dns_gethostbyname(aHostname, &addr, &wifi_dns_found_callback, &aResult);
    if(err == ERR_OK && addr.u_addr.ip4.addr) {
        aResult = addr.u_addr.ip4.addr;
    } else if(err == ERR_INPROGRESS) {
        waitStatusBits(WIFI_DNS_DONE_BIT, 15000);  //real internal timeout in lwip library is 14[s]
        clearStatusBits(WIFI_DNS_DONE_BIT);
    }
    setStatusBits(WIFI_DNS_IDLE_BIT);
    if((uint32_t)aResult == 0){
        log_e("DNS Failed for %s", aHostname);
    }
    return (uint32_t)aResult != 0;
}

IPAddress WiFiGenericClass::calculateNetworkID(IPAddress ip, IPAddress subnet) {
    IPAddress networkID;

    for (size_t i = 0; i < 4; i++)
        networkID[i] = subnet[i] & ip[i];

    return networkID;
}

IPAddress WiFiGenericClass::calculateBroadcast(IPAddress ip, IPAddress subnet) {
    IPAddress broadcastIp;
    
    for (int i = 0; i < 4; i++)
        broadcastIp[i] = ~subnet[i] | ip[i];

    return broadcastIp;
}

uint8_t WiFiGenericClass::calculateSubnetCIDR(IPAddress subnetMask) {
    uint8_t CIDR = 0;

    for (uint8_t i = 0; i < 4; i++) {
        if (subnetMask[i] == 0x80)  // 128
            CIDR += 1;
        else if (subnetMask[i] == 0xC0)  // 192
            CIDR += 2;
        else if (subnetMask[i] == 0xE0)  // 224
            CIDR += 3;
        else if (subnetMask[i] == 0xF0)  // 242
            CIDR += 4;
        else if (subnetMask[i] == 0xF8)  // 248
            CIDR += 5;
        else if (subnetMask[i] == 0xFC)  // 252
            CIDR += 6;
        else if (subnetMask[i] == 0xFE)  // 254
            CIDR += 7;
        else if (subnetMask[i] == 0xFF)  // 255
            CIDR += 8;
    }

    return CIDR;
}

void WiFiGenericClass::_meshCallback(uint32_t event_id, void* event_data)
{

}

void WiFiGenericClass::_provCallback(uint32_t event_id, void* event_data)
{

}
