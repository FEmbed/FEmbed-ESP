/*
 WiFiSTA.cpp - WiFi library for esp32

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
#include "WiFiSTA.h"

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
#include "lwip/err.h"
#include "lwip/dns.h"
#include <esp_smartconfig.h>
}

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WiFiSta"

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------- Private functions ------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

static bool sta_config_equal(const wifi_config_t& lhs, const wifi_config_t& rhs);

/**
 * compare two STA configurations
 * @param lhs station_config
 * @param rhs station_config
 * @return equal
 */
static bool sta_config_equal(const wifi_config_t& lhs, const wifi_config_t& rhs)
{
    if(memcmp(&lhs, &rhs, sizeof(wifi_config_t)) != 0) {
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------- STA function -----------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

WiFiSTAClass::WiFiSTAClass()
	: _lock(new FEmbed::OSMutex())
{
	_useStaticIp = false;
	_autoReconnect = true;
	_hostname = "esp32-fembed";
	_smartConfigStarted = false;
	_smartConfigDone = false;
	_status = WL_NO_SHIELD;
}

WiFiSTAClass::~WiFiSTAClass()
{
}

void WiFiSTAClass::_setStatus(wl_status_t status)
{
	FEmbed::OSMutexLocker locker(_lock);
	_status = status;
}

/**
 * Return Connection status.
 * @return one of the value defined in wl_status_t
 *
 */
wl_status_t WiFiSTAClass::status()
{
	FEmbed::OSMutexLocker locker(_lock);
	return _status;
}

/**
 * Start Wifi connection
 * if passphrase is set the most secure supported mode will be automatically selected
 * @param ssid const char*          Pointer to the SSID string.
 * @param passphrase const char *   Optional. Passphrase. Valid characters in a passphrase must be between ASCII 32-126 (decimal).
 * @param bssid uint8_t[6]          Optional. BSSID / MAC of AP
 * @param channel                   Optional. Channel of AP
 * @param connect                   Optional. call connect
 * @return
 */
wl_status_t WiFiSTAClass::begin(const char* ssid, const char *passphrase, int32_t channel, const uint8_t* bssid, bool connect)
{

    if(!WiFi->enableSTA(true)) {
        log_e("STA enable failed!");
        return WL_CONNECT_FAILED;
    }

    if(!ssid || *ssid == 0x00 || strlen(ssid) > 31) {
        log_e("SSID too long or missing!");
        return WL_CONNECT_FAILED;
    }

    if(passphrase && strlen(passphrase) > 64) {
        log_e("passphrase too long!");
        return WL_CONNECT_FAILED;
    }

    wifi_config_t conf;
    memset(&conf, 0, sizeof(wifi_config_t));
    strcpy(reinterpret_cast<char*>(conf.sta.ssid), ssid);

    if(passphrase) {
        if (strlen(passphrase) == 64){ // it's not a passphrase, is the PSK
            memcpy(reinterpret_cast<char*>(conf.sta.password), passphrase, 64);
        } else {
            strcpy(reinterpret_cast<char*>(conf.sta.password), passphrase);
        }
    }

    if(bssid) {
        conf.sta.bssid_set = 1;
        memcpy((void *) &conf.sta.bssid[0], (void *) bssid, 6);
    }

    if(channel > 0 && channel <= 13) {
        conf.sta.channel = channel;
    }

    wifi_config_t current_conf;
    esp_wifi_get_config(WIFI_IF_STA, &current_conf);
    if(!sta_config_equal(current_conf, conf)) {
        if(esp_wifi_disconnect()){
            log_e("disconnect failed!");
            return WL_CONNECT_FAILED;
        }

        esp_wifi_set_config(WIFI_IF_STA, &conf);
    } else if(status() == WL_CONNECTED){
        return WL_CONNECTED;
    } else {
        esp_wifi_set_config(WIFI_IF_STA, &conf);
    }

    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(!_useStaticIp) {
        if(	(_sta == NULL) ||
        	(esp_netif_dhcpc_start(_sta) == ESP_ERR_TCPIP_ADAPTER_DHCPC_START_FAILED)) {
            log_e("dhcp client start failed!");
            return WL_CONNECT_FAILED;
        }
    } else {
    	if(_sta != NULL) esp_netif_dhcpc_stop(_sta);
    }

    if(connect && esp_wifi_connect()) {
        log_e("connect failed!");
        return WL_CONNECT_FAILED;
    }

    return status();
}

wl_status_t WiFiSTAClass::begin(char* ssid, char *passphrase, int32_t channel, const uint8_t* bssid, bool connect)
{
    return begin((const char*) ssid, (const char*) passphrase, channel, bssid, connect);
}

/**
 * Use to connect to SDK config.
 * @return wl_status_t
 */
wl_status_t WiFiSTAClass::begin()
{
    if(!WiFi->enableSTA(true)) {
        log_e("STA enable failed!");
        return WL_CONNECT_FAILED;
    }

    wifi_config_t current_conf;
    if(esp_wifi_get_config(WIFI_IF_STA, &current_conf) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &current_conf) != ESP_OK) {
        log_e("config failed");
        return WL_CONNECT_FAILED;
    }

    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(!_useStaticIp) {
        if(	(_sta == NULL) ||
        	(esp_netif_dhcpc_start(_sta) == ESP_ERR_TCPIP_ADAPTER_DHCPC_START_FAILED)) {
            log_e("dhcp client start failed!");
            return WL_CONNECT_FAILED;
        }
    } else {
    	if(_sta != NULL) esp_netif_dhcpc_stop(_sta);
    }

    return status();
}

/**
 * will force a disconnect and then start reconnecting to AP
 * @return true when successful
 */
bool WiFiSTAClass::reconnect()
{
    if(WiFi->getMode() & WIFI_MODE_STA) {
        if(esp_wifi_disconnect() == ESP_OK) {
            return esp_wifi_connect() == ESP_OK;
        }
    }
    return false;
}

/**
 * Disconnect from the network
 * @param wifioff
 * @return  one value of wl_status_t enum
 */
bool WiFiSTAClass::disconnect(bool wifioff, bool eraseap)
{
    wifi_config_t conf;

    if(WiFi->getMode() & WIFI_MODE_STA){
        if(eraseap){
            memset(&conf, 0, sizeof(wifi_config_t));
            if(esp_wifi_set_config(WIFI_IF_STA, &conf)){
                log_e("clear config failed!");
            }
        }
        if(esp_wifi_disconnect()){
            log_e("disconnect failed!");
            return false;
        }
        if(wifioff) {
             return WiFi->enableSTA(false);
        }
        return true;
    }

    return false;
}

/**
 * Change IP configuration settings disabling the dhcp client
 * @param local_ip   Static ip configuration
 * @param gateway    Static gateway configuration
 * @param subnet     Static Subnet mask
 * @param dns1       Static DNS server 1
 * @param dns2       Static DNS server 2
 */
bool WiFiSTAClass::config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2)
{
    esp_err_t err = ESP_OK;

    if(!WiFi->enableSTA(true)) {
        return false;
    }

    esp_netif_ip_info_t info;

    if(local_ip != (uint32_t)0x00000000 && local_ip != INADDR_NONE){
        info.ip.addr = static_cast<uint32_t>(local_ip);
        info.gw.addr = static_cast<uint32_t>(gateway);
        info.netmask.addr = static_cast<uint32_t>(subnet);
    } else {
        info.ip.addr = 0;
        info.gw.addr = 0;
        info.netmask.addr = 0;
    }

    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(_sta == NULL)
    {
    	log_e("STA netif init failed!");
    	return false;
    }

    err = esp_netif_dhcpc_stop(_sta);
    if(err != ESP_OK && err != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STOPPED){
        log_e("DHCP could not be stopped! Error: %d", err);
        return false;
    }

    err = esp_netif_set_ip_info(_sta, &info);
    if(err != ERR_OK){
        log_e("STA IP could not be configured! Error: %d", err);
        return false;
    }

    if(info.ip.addr){
        _useStaticIp = true;
    } else {
        err = esp_netif_dhcpc_start(_sta);
        if(err == ESP_ERR_TCPIP_ADAPTER_DHCPC_START_FAILED){
            log_e("dhcp client start failed!");
            return false;
        }
        _useStaticIp = false;
    }

    ip_addr_t d;
    d.type = IPADDR_TYPE_V4;

    if(dns1 != (uint32_t)0x00000000 && dns1 != INADDR_NONE) {
        // Set DNS1-Server
        d.u_addr.ip4.addr = static_cast<uint32_t>(dns1);
        dns_setserver(0, &d);
    }

    if(dns2 != (uint32_t)0x00000000 && dns2 != INADDR_NONE) {
        // Set DNS2-Server
        d.u_addr.ip4.addr = static_cast<uint32_t>(dns2);
        dns_setserver(1, &d);
    }

    return true;
}

/**
 * is STA interface connected?
 * @return true if STA is connected to an AP
 */
bool WiFiSTAClass::isConnected()
{
    return (status() == WL_CONNECTED);
}


/**
 * Setting the ESP32 station to connect to the AP (which is recorded)
 * automatically or not when powered on. Enable auto-connect by default.
 * @param autoConnect bool
 * @return if saved
 */
bool WiFiSTAClass::setAutoConnect(bool autoConnect)
{
    /*bool ret;
    ret = esp_wifi_set_auto_connect(autoConnect);
    return ret;*/
    return false;//now deprecated
}

/**
 * Checks if ESP32 station mode will connect to AP
 * automatically or not when it is powered on.
 * @return auto connect
 */
bool WiFiSTAClass::getAutoConnect()
{
    /*bool autoConnect;
    esp_wifi_get_auto_connect(&autoConnect);
    return autoConnect;*/
    return false;//now deprecated
}

bool WiFiSTAClass::setAutoReconnect(bool autoReconnect)
{
    _autoReconnect = autoReconnect;
    return true;
}

bool WiFiSTAClass::getAutoReconnect()
{
    return _autoReconnect;
}

/**
 * Wait for WiFi connection to reach a result
 * returns the status reached or disconnect if STA is off
 * @return wl_status_t
 */
uint8_t WiFiSTAClass::waitForConnectResult()
{
    //1 and 3 have STA enabled
    if((WiFi->getMode() & WIFI_MODE_STA) == 0) {
        return WL_DISCONNECTED;
    }
    int i = 0;
    while((!status() || status() >= WL_DISCONNECTED) && i++ < 100) {
        delay(100);
    }
    return status();
}

/**
 * Get the station interface IP address.
 * @return IPAddress station IP
 */
IPAddress WiFiSTAClass::localIP()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPAddress();
    }

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return IPAddress(ip.ip.addr);
}


/**
 * Get the station interface MAC address.
 * @param mac   pointer to uint8_t array with length WL_MAC_ADDR_LENGTH
 * @return      pointer to uint8_t *
 */
uint8_t* WiFiSTAClass::macAddress(uint8_t* mac)
{
    if(WiFi->getMode() != WIFI_MODE_NULL){
        esp_wifi_get_mac(WIFI_IF_STA, mac);	
    }
    else{
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    return mac;
}

/**
 * Get the station interface MAC address.
 * @return String mac
 */
String WiFiSTAClass::macAddress(void)
{
    uint8_t mac[6];
    char macStr[18] = { 0 };
    if(WiFi->getMode() == WIFI_MODE_NULL){
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    else{
        esp_wifi_get_mac(WIFI_IF_STA, mac);
    }
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

/**
 * Get the interface subnet mask address.
 * @return IPAddress subnetMask
 */
IPAddress WiFiSTAClass::subnetMask()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPAddress();
    }

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return IPAddress(ip.netmask.addr);
}

/**
 * Get the gateway ip address.
 * @return IPAddress gatewayIP
 */
IPAddress WiFiSTAClass::gatewayIP()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return IPAddress(ip.gw.addr);
}

/**
 * Get the DNS ip address.
 * @param dns_no
 * @return IPAddress DNS Server IP
 */
IPAddress WiFiSTAClass::dnsIP(uint8_t dns_no)
{
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return IPAddress();
    }
    const ip_addr_t* dns_ip = dns_getserver(dns_no);
    return IPAddress(dns_ip->u_addr.ip4.addr);
}

/**
 * Get the broadcast ip address.
 * @return IPAddress broadcastIP
 */
IPAddress WiFiSTAClass::broadcastIP()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return WiFi->calculateBroadcast(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the network id.
 * @return IPAddress networkID
 */
IPAddress WiFiSTAClass::networkID()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return WiFi->calculateNetworkID(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the subnet CIDR.
 * @return uint8_t subnetCIDR
 */
uint8_t WiFiSTAClass::subnetCIDR()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return (uint8_t)0;
    }
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(_sta, &ip);
    return WiFi->calculateSubnetCIDR(IPAddress(ip.netmask.addr));
}

/**
 * Return the current SSID associated with the network
 * @return SSID
 */
String WiFiSTAClass::SSID() const
{
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return String();
    }
    wifi_ap_record_t info;
    if(!esp_wifi_sta_get_ap_info(&info)) {
        return String(reinterpret_cast<char*>(info.ssid));
    }
    return String();
}

/**
 * Return the current pre shared key associated with the network
 * @return  psk string
 */
String WiFiSTAClass::psk() const
{
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return String();
    }
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    return String(reinterpret_cast<char*>(conf.sta.password));
}

/**
 * Return the current bssid / mac associated with the network if configured
 * @return bssid uint8_t *
 */
uint8_t* WiFiSTAClass::BSSID(void)
{
    static uint8_t bssid[6];
    wifi_ap_record_t info;
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return NULL;
    }
    if(!esp_wifi_sta_get_ap_info(&info)) {
        memcpy(bssid, info.bssid, 6);
        return reinterpret_cast<uint8_t*>(bssid);
    }
    return NULL;
}

/**
 * Return the current bssid / mac associated with the network if configured
 * @return String bssid mac
 */
String WiFiSTAClass::BSSIDstr(void)
{
    uint8_t* bssid = BSSID();
    if(!bssid){
        return String();
    }
    char mac[18] = { 0 };
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(mac);
}

/**
 * Return the current network RSSI.
 * @return  RSSI value
 */
int8_t WiFiSTAClass::RSSI(void)
{
    if(WiFi->getMode() == WIFI_MODE_NULL){
        return 0;
    }
    wifi_ap_record_t info;
    if(!esp_wifi_sta_get_ap_info(&info)) {
        return info.rssi;
    }
    return 0;
}

/**
 * Get the station interface Host name.
 * @return char array hostname
 */
const char * WiFiSTAClass::getHostname()
{
    const char * hostname = NULL;
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return hostname;
    }
    if(esp_netif_get_hostname(_sta, &hostname)){
        return NULL;
    }
    return hostname;
}

/**
 * Set the station interface Host name.
 * @param  hostname  pointer to const string
 * @return true on   success
 */
bool WiFiSTAClass::setHostname(const char * hostname)
{
    _hostname = hostname;
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return false;
    }
    return esp_netif_set_hostname(_sta, hostname) == 0;
}

/**
 * Enable IPv6 on the station interface.
 * @return true on success
 */
bool WiFiSTAClass::enableIpV6()
{
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return false;
    }
    return esp_netif_create_ip6_linklocal(_sta) == 0;
}

/**
 * Get the station interface IPv6 address.
 * @return IPv6Address
 */
IPv6Address WiFiSTAClass::localIPv6()
{
    static esp_ip6_addr_t addr;
    esp_netif_t *_sta = (esp_netif_t *)WiFi->_default_sta;
    if(WiFi->getMode() == WIFI_MODE_NULL || _sta == NULL){
        return IPv6Address();
    }
    if(esp_netif_get_ip6_linklocal(_sta, &addr)){
        return IPv6Address();
    }
    return IPv6Address(addr.addr);
}

bool WiFiSTAClass::beginSmartConfig() {
    if (_smartConfigStarted) {
        return false;
    }

    if (!WiFi->mode(WIFI_STA)) {
        return false;
    }

    esp_wifi_disconnect();

    esp_err_t err;
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    //reinterpret_cast<sc_callback_t>(&WiFiSTAClass::_smartConfigCallback)
    err = esp_smartconfig_start(&cfg);
    if (err == ESP_OK) {
        _smartConfigStarted = true;
        _smartConfigDone = false;
        return true;
    }
    return false;
}

bool WiFiSTAClass::stopSmartConfig() {
    if (!_smartConfigStarted) {
        return true;
    }

    if (esp_smartconfig_stop() == ESP_OK) {
        _smartConfigStarted = false;
        return true;
    }

    return false;
}

bool WiFiSTAClass::smartConfigDone() {
    if (!_smartConfigStarted) {
        return false;
    }

    return _smartConfigDone;
}

void WiFiSTAClass::_smartConfigCallback(uint32_t event_id, void* event_data) {
    if (event_id == SC_EVENT_SCAN_DONE) {
        log_d("Scan done");
    } else if (event_id == SC_EVENT_FOUND_CHANNEL) {
        log_d("Found channel");
    } else if (event_id == SC_EVENT_GOT_SSID_PSWD) {
        log_d("Got SSID and password");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        log_d("SSID:%s", ssid);
        log_d("PASSWORD:%s", password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            log_d("RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
        _smartConfigDone = true;
    } else if (event_id == SC_EVENT_SCAN_DONE) {
        WiFi->stopSmartConfig();
    }
}
