/*
 ESP8266WiFiSTA.cpp - WiFi library for esp8266

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
#include "WiFiAP.h"

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
#include <lwip/ip_addr.h>
#include "dhcpserver/dhcpserver_options.h"
}

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WiFiAp"

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------- Private functions ------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

static bool softap_config_equal(const wifi_config_t& lhs, const wifi_config_t& rhs);



/**
 * compare two AP configurations
 * @param lhs softap_config
 * @param rhs softap_config
 * @return equal
 */
static bool softap_config_equal(const wifi_config_t& lhs, const wifi_config_t& rhs)
{
    if(strcmp(reinterpret_cast<const char*>(lhs.ap.ssid), reinterpret_cast<const char*>(rhs.ap.ssid)) != 0) {
        return false;
    }
    if(strcmp(reinterpret_cast<const char*>(lhs.ap.password), reinterpret_cast<const char*>(rhs.ap.password)) != 0) {
        return false;
    }
    if(lhs.ap.channel != rhs.ap.channel) {
        return false;
    }
    if(lhs.ap.ssid_hidden != rhs.ap.ssid_hidden) {
        return false;
    }
    if(lhs.ap.max_connection != rhs.ap.max_connection) {
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------------------------------------------------
// ----------------------------------------------------- AP function -----------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------


/**
 * Set up an access point
 * @param ssid              Pointer to the SSID (max 63 char).
 * @param passphrase        (for WPA2 min 8 char, for open use NULL)
 * @param channel           WiFi channel number, 1 - 13.
 * @param ssid_hidden       Network cloaking (0 = broadcast SSID, 1 = hide SSID)
 * @param max_connection    Max simultaneous connected clients, 1 - 4.
*/
bool WiFiAPClass::softAP(const char* ssid, const char* passphrase, int channel, int ssid_hidden, int max_connection)
{

    if(!WiFi->enableAP(true)) {
        // enable AP failed
        log_e("enable AP failed!");
        return false;
    }

    if(!ssid || *ssid == 0) {
        // fail SSID missing
        log_e("SSID missing!");
        return false;
    }

    if(passphrase && (strlen(passphrase) > 0 && strlen(passphrase) < 8)) {
        // fail passphrase too short
        log_e("passphrase too short!");
        return false;
    }

    wifi_config_t conf;
    strlcpy(reinterpret_cast<char*>(conf.ap.ssid), ssid, sizeof(conf.ap.ssid));
    conf.ap.channel = channel;
    conf.ap.ssid_len = strlen(reinterpret_cast<char *>(conf.ap.ssid));
    conf.ap.ssid_hidden = ssid_hidden;
    conf.ap.max_connection = max_connection;
    conf.ap.beacon_interval = 100;

    if(!passphrase || strlen(passphrase) == 0) {
        conf.ap.authmode = WIFI_AUTH_OPEN;
        *conf.ap.password = 0;
    } else {
        conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy(reinterpret_cast<char*>(conf.ap.password), passphrase, sizeof(conf.ap.password));
    }

    wifi_config_t conf_current;
    esp_wifi_get_config(WIFI_IF_AP, &conf_current);
    if(!softap_config_equal(conf, conf_current) && esp_wifi_set_config(WIFI_IF_AP, &conf) != ESP_OK) {
        return false;
    }

    if(!WiFi->espWiFiStart()){
        return false;
    }

    return true;
}


/**
 * Configure access point
 * @param local_ip      access point IP
 * @param gateway       gateway IP
 * @param subnet        subnet mask
 */
bool WiFiAPClass::softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet)
{
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;

    if(!WiFi->enableAP(true) || _ap == NULL) {
        // enable AP failed
        log_w("Ap started failed.");
        return false;
    }

    if(!WiFi->espWiFiStart()){
        log_w("WiFi started failed.");
        return false;
    }

    ///< wait Ap start
    if(!WiFi->waitApStarted(5000, false))
    {
        log_w("Wait Ap started failed.");
        return false;
    }

    esp_netif_ip_info_t info;
    info.ip.addr = static_cast<uint32_t>(local_ip);
    info.gw.addr = static_cast<uint32_t>(gateway);
    info.netmask.addr = static_cast<uint32_t>(subnet);
    esp_netif_dhcps_stop(_ap);
    if(esp_netif_set_ip_info(_ap, &info) == ESP_OK) {
        dhcps_lease_t lease;
        lease.enable = true;
        lease.start_ip.addr = static_cast<uint32_t>(local_ip) + (1 << 24);
        lease.end_ip.addr = static_cast<uint32_t>(local_ip) + (11 << 24);

        esp_netif_dhcps_option(
            _ap,
            ESP_NETIF_OP_SET,
            ESP_NETIF_REQUESTED_IP_ADDRESS,
            (void*)&lease, sizeof(dhcps_lease_t)
        );

        return esp_netif_dhcps_start(_ap) == ESP_OK;
    }
    return false;
}

bool WiFiAPClass::softAPConfig(const char* local_ip, const char* gateway, const char* subnet)
{
    IPAddress ip(local_ip);
    IPAddress gw(gateway);
    IPAddress mask(subnet);
    return softAPConfig(ip, gw, mask);
}



/**
 * Disconnect from the network (close AP)
 * @param wifioff disable mode?
 * @return one value of wl_status_t enum
 */
bool WiFiAPClass::softAPdisconnect(bool wifioff)
{
    bool ret;

    if(WiFi->getMode() == WIFI_MODE_NULL){
        return ESP_ERR_INVALID_STATE;
    }

    if(wifioff) {
        ret = WiFi->enableAP(false) == ESP_OK;
    }
    else
    {
        ret = esp_wifi_set_mode((wifi_mode_t)(WiFi->getMode() & (~WIFI_MODE_AP))) == ESP_OK;
    }

    return ret;
}


/**
 * Get the count of the Station / client that are connected to the softAP interface
 * @return Stations count
 */
uint8_t WiFiAPClass::softAPgetStationNum()
{
    wifi_sta_list_t clients;
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return 0;
    }
    if(esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
        return clients.num;
    }
    return 0;
}

/**
 * Get the softAP interface IP address.
 * @return IPAddress softAP IP
 */
IPAddress WiFiAPClass::softAPIP()
{
    esp_netif_ip_info_t ip;
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;

    if(WiFi->getMode() == WIFI_MODE_NULL || _ap == NULL){
        return IPAddress();
    }
    esp_netif_get_ip_info(_ap, &ip);
    return IPAddress(ip.ip.addr);
}

/**
 * Get the softAP broadcast IP address.
 * @return IPAddress softAP broadcastIP
 */
IPAddress WiFiAPClass::softAPBroadcastIP()
{
    esp_netif_ip_info_t ip;
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;

    if(WiFi->getMode() == WIFI_MODE_NULL || _ap == NULL){
        return IPAddress();
    }
    esp_netif_get_ip_info(_ap, &ip);

    return WiFi->calculateBroadcast(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the softAP network ID.
 * @return IPAddress softAP networkID
 */
IPAddress WiFiAPClass::softAPNetworkID()
{
    esp_netif_ip_info_t ip;
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;

    if(WiFi->getMode() == WIFI_MODE_NULL || _ap == NULL){
        return IPAddress();
    }
    esp_netif_get_ip_info(_ap, &ip);
    return WiFi->calculateNetworkID(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the softAP subnet CIDR.
 * @return uint8_t softAP subnetCIDR
 */
uint8_t WiFiAPClass::softAPSubnetCIDR()
{
    tcpip_adapter_ip_info_t ip;
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return (uint8_t)0;
    }
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip);
    return WiFi->calculateSubnetCIDR(IPAddress(ip.netmask.addr));
}

/**
 * Get the softAP interface MAC address.
 * @param mac   pointer to uint8_t array with length WL_MAC_ADDR_LENGTH
 * @return      pointer to uint8_t*
 */
uint8_t* WiFiAPClass::softAPmacAddress(uint8_t* mac)
{
    if(WiFi->getMode() != WIFI_MODE_NULL){
        esp_wifi_get_mac(WIFI_IF_AP, mac);
    }
    return mac;
}

/**
 * Get the softAP interface MAC address.
 * @return String mac
 */
String WiFiAPClass::softAPmacAddress(void)
{
    uint8_t mac[6];
    char macStr[18] = { 0 };
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return String();
    }
    esp_wifi_get_mac(WIFI_IF_AP, mac);

    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

/**
 * Get the softAP interface Host name.
 * @return char array hostname
 */
const char * WiFiAPClass::softAPgetHostname()
{
    const char * hostname = NULL;
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;
    if((WiFi->getMode() == WIFI_MODE_NULL) || _ap == NULL) {
        return hostname;
    }
    if(esp_netif_get_hostname(_ap, &hostname)) {
        return hostname;
    }
    return hostname;
}

/**
 * Set the softAP    interface Host name.
 * @param  hostname  pointer to const string
 * @return true on   success
 */
bool WiFiAPClass::softAPsetHostname(const char * hostname)
{
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;
    if((WiFi->getMode() == WIFI_MODE_NULL) || _ap == NULL) {
        return false;
    }
    return esp_netif_set_hostname(_ap, hostname) == ESP_OK;
}

/**
 * Enable IPv6 on the softAP interface.
 * @return true on success
 */
bool WiFiAPClass::softAPenableIpV6()
{
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;
    if((WiFi->getMode() == WIFI_MODE_NULL) || _ap == NULL) {
        return false;
    }
    return esp_netif_create_ip6_linklocal(_ap) == ESP_OK;
}

/**
 * Get the softAP interface IPv6 address.
 * @return IPv6Address softAP IPv6
 */
IPv6Address WiFiAPClass::softAPIPv6()
{
    static esp_ip6_addr_t addr;
    esp_netif_t *_ap = (esp_netif_t *)WiFi->_default_ap;
    if((WiFi->getMode() == WIFI_MODE_NULL) || _ap == NULL) {
        return IPv6Address();
    }
    if(esp_netif_get_ip6_linklocal(_ap, &addr)) {
        return IPv6Address();
    }
    return IPv6Address(addr.addr);
}
