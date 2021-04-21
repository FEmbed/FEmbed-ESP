/*
 ESP8266WiFiScan.cpp - WiFi library for esp8266

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
#include "WiFiScan.h"

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
}

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "WiFiScan"

WiFiScanClass::WiFiScanClass()
{
    _scanAsync = false;
    _scanStarted = 0;
    _scanTimeout = 10000;
    _scanCount = 0;
    _scanResult = NULL;
}

WiFiScanClass::~WiFiScanClass()
{

}
/**
 * Start scan WiFi networks available
 * @param async         run in async mode
 * @param show_hidden   show hidden networks
 * @return Number of discovered networks
 */
int16_t WiFiScanClass::scanNetworks(bool async, bool show_hidden, bool passive, uint32_t max_ms_per_chan, uint8_t channel)
{
    if(WiFi->getStatusBits() & WIFI_SCANNING_BIT) {
        return WIFI_SCAN_RUNNING;
    }

    _scanTimeout = max_ms_per_chan * 20;
    _scanAsync = async;

    WiFi->enableSTA(true);

    scanDelete();

    wifi_scan_config_t config;
    config.ssid = 0;
    config.bssid = 0;
    config.channel = channel;
    config.show_hidden = show_hidden;
    if(passive){
        config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        config.scan_time.passive = max_ms_per_chan;
    } else {
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        config.scan_time.active.min = 100;
        config.scan_time.active.max = max_ms_per_chan;
    }
    if(esp_wifi_scan_start(&config, false) == ESP_OK) {
        _scanStarted = millis();
        if (!_scanStarted) { //Prevent 0 from millis overflow
            ++_scanStarted;
        }

        WiFi->clearStatusBits(WIFI_SCAN_DONE_BIT);
        WiFi->setStatusBits(WIFI_SCANNING_BIT);

        if(_scanAsync) {
            return WIFI_SCAN_RUNNING;
        }
        if(WiFi->waitStatusBits(WIFI_SCAN_DONE_BIT, 10000)){
            return (int16_t) _scanCount;
        }
    }
    return WIFI_SCAN_FAILED;
}


/**
 * private
 * scan callback
 * @param result  void *arg
 * @param status STATUS
 */
void WiFiScanClass::scanDone()
{
    esp_wifi_scan_get_ap_num(&(_scanCount));
    if(_scanCount) {
        _scanResult = new wifi_ap_record_t[_scanCount];
        if(!_scanResult || esp_wifi_scan_get_ap_records(&(_scanCount), (wifi_ap_record_t*)_scanResult) != ESP_OK) {
            _scanCount = 0;
        }
    }
    _scanStarted=0; //Reset after a scan is completed for normal behavior
    WiFi->setStatusBits(WIFI_SCAN_DONE_BIT);
    WiFi->clearStatusBits(WIFI_SCANNING_BIT);
}

/**
 * called to get the scan state in Async mode
 * @return scan result or status
 *          -1 if scan not fin
 *          -2 if scan not triggered
 */
int16_t WiFiScanClass::scanState()
{
    if (_scanStarted && (millis()-_scanStarted) > _scanTimeout) { //Check is scan was started and if the delay expired, return WIFI_SCAN_FAILED in this case
        WiFi->clearStatusBits(WIFI_SCANNING_BIT);
        return WIFI_SCAN_FAILED;
    }

    if(WiFi->getStatusBits() & WIFI_SCAN_DONE_BIT) {
        return _scanCount;
    }

    if(WiFi->getStatusBits() & WIFI_SCANNING_BIT) {
        return WIFI_SCAN_RUNNING;
    }

    return WIFI_SCAN_FAILED;
}

/**
 * delete last scan result from RAM
 */
void WiFiScanClass::scanDelete()
{
    WiFi->clearStatusBits(WIFI_SCAN_DONE_BIT);
    if(_scanResult) {
        delete[] reinterpret_cast<wifi_ap_record_t*>(_scanResult);
        _scanResult = 0;
        _scanCount = 0;
    }
}

/**
 *
 * @param i specify from which network item want to get the information
 * @return bss_info *
 */
void * WiFiScanClass::getScanInfoByIndex(int i)
{
    if(!_scanResult || (size_t) i >= _scanCount) {
        return NULL;
    }
    return reinterpret_cast<wifi_ap_record_t*>(_scanResult) + i;
}

/**
 * loads all infos from a scanned wifi in to the ptr parameters
 * @param networkItem uint8_t
 * @param ssid  const char**
 * @param encryptionType uint8_t *
 * @param RSSI int32_t *
 * @param BSSID uint8_t **
 * @param channel int32_t *
 * @return (true if ok)
 */
bool WiFiScanClass::getNetworkInfo(uint8_t i, String &ssid, uint8_t &encType, int32_t &rssi, uint8_t* &bssid, int32_t &channel)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return false;
    }
    ssid = (const char*) it->ssid;
    encType = it->authmode;
    rssi = it->rssi;
    bssid = it->bssid;
    channel = it->primary;
    return true;
}


/**
 * Return the SSID discovered during the network scan.
 * @param i     specify from which network item want to get the information
 * @return       ssid string of the specified item on the networks scanned list
 */
String WiFiScanClass::SSID(uint8_t i)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return String();
    }
    return String(reinterpret_cast<const char*>(it->ssid));
}


/**
 * Return the encryption type of the networks discovered during the scanNetworks
 * @param i specify from which network item want to get the information
 * @return  encryption type (enum wl_enc_type) of the specified item on the networks scanned list
 */
wifi_auth_mode_t WiFiScanClass::encryptionType(uint8_t i)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return WIFI_AUTH_OPEN;
    }
    return it->authmode;
}

/**
 * Return the RSSI of the networks discovered during the scanNetworks
 * @param i specify from which network item want to get the information
 * @return  signed value of RSSI of the specified item on the networks scanned list
 */
int32_t WiFiScanClass::RSSI(uint8_t i)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return 0;
    }
    return it->rssi;
}

/**
 * return MAC / BSSID of scanned wifi
 * @param i specify from which network item want to get the information
 * @return uint8_t * MAC / BSSID of scanned wifi
 */
uint8_t * WiFiScanClass::BSSID(uint8_t i)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return 0;
    }
    return it->bssid;
}

/**
 * return MAC / BSSID of scanned wifi
 * @param i specify from which network item want to get the information
 * @return String MAC / BSSID of scanned wifi
 */
String WiFiScanClass::BSSIDstr(uint8_t i)
{
    char mac[18] = { 0 };
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return String();
    }
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", it->bssid[0], it->bssid[1], it->bssid[2], it->bssid[3], it->bssid[4], it->bssid[5]);
    return String(mac);
}

int32_t WiFiScanClass::channel(uint8_t i)
{
    wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(getScanInfoByIndex(i));
    if(!it) {
        return 0;
    }
    return it->primary;
}

