/*
 ESP8266WiFiGeneric.h - esp8266 Wifi support.
 Based on WiFi.h from Ardiono WiFi shield library.
 Copyright (c) 2011-2014 Arduino.  All right reserved.
 Modified by Ivan Grokhotkov, December 2014
 Reworked by Markus Sattler, December 2015
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
 */

#ifndef ESP32WIFIGENERIC_H_
#define ESP32WIFIGENERIC_H_

#include <esp_err.h>
#include <esp_event.h>
#include <functional>
#include "WiFiType.h"
#include "IPAddress.h"
#include <wifi_provisioning/manager.h>
#include <osTask.h>
#include <osMessage.h>
#include <osSignal.h>
#include <osMutex.h>

typedef enum {
    WIFI_POWER_19_5dBm = 78,    // 19.5dBm
    WIFI_POWER_19dBm = 76,      // 19dBm
    WIFI_POWER_18_5dBm = 74,    // 18.5dBm
    WIFI_POWER_17dBm = 68,      // 17dBm
    WIFI_POWER_15dBm = 60,      // 15dBm
    WIFI_POWER_13dBm = 52,      // 13dBm
    WIFI_POWER_11dBm = 44,      // 11dBm
    WIFI_POWER_8_5dBm = 34,     // 8.5dBm
    WIFI_POWER_7dBm = 28,       // 7dBm
    WIFI_POWER_5dBm = 20,       // 5dBm
    WIFI_POWER_2dBm = 8,        // 2dBm
    WIFI_POWER_MINUS_1dBm = -4  // -1dBm
} wifi_power_t;

static const int AP_STARTED_BIT    = BIT0;
static const int AP_HAS_IP6_BIT    = BIT1;
static const int AP_HAS_CLIENT_BIT = BIT2;
static const int STA_STARTED_BIT   = BIT3;
static const int STA_CONNECTED_BIT = BIT4;
static const int STA_HAS_IP_BIT    = BIT5;
static const int STA_HAS_IP6_BIT   = BIT6;
static const int ETH_STARTED_BIT   = BIT7;
static const int ETH_CONNECTED_BIT = BIT8;
static const int ETH_HAS_IP_BIT    = BIT9;
static const int ETH_HAS_IP6_BIT   = BIT10;
static const int WIFI_SCANNING_BIT = BIT11;
static const int WIFI_SCAN_DONE_BIT= BIT12;
static const int WIFI_DNS_IDLE_BIT = BIT13;
static const int WIFI_DNS_DONE_BIT = BIT14;

class WiFiGenericClass
{
  public:
    WiFiGenericClass();
    ~WiFiGenericClass();

    int getStatusBits();
    int waitStatusBits(int bits, uint32_t timeout_ms);

    int32_t channel(void);

    void persistent(bool persistent);
    void enableLongRange(bool enable);

    bool mode(wifi_mode_t);
    wifi_mode_t getMode();

    bool enableSTA(bool enable);
    bool enableAP(bool enable);

    bool setSleep(bool enable);
    bool setSleep(wifi_ps_type_t mode);
    bool getSleep();

    bool setTxPower(wifi_power_t power);
    wifi_power_t getTxPower();

    FE_NOTIFY_SIGNAL(14, WifiDNSDone, _network_event_group)
  protected:
    bool _persistent;
    bool _long_range;
    wifi_mode_t _forceSleepLastMode;
    void *_default_sta;
    void *_default_ap;

    int setStatusBits(int bits);
    int clearStatusBits(int bits);

    void _meshCallback(uint32_t event_id, void* event_data);
    void _provCallback(uint32_t event_id, void* event_data);
  private:
    bool wifiLowLevelInit(bool persistent, wifi_mode_t m);
    bool wifiLowLevelDeinit();
    bool espWiFiStart();
    bool espWiFiStop();

    std::shared_ptr<FEmbed::OSSignal> _network_event_group;
    bool lowLevelInitDone;
  public:

    int hostByName(const char *aHostname, IPAddress &aResult);

    IPAddress calculateNetworkID(IPAddress ip, IPAddress subnet);
    IPAddress calculateBroadcast(IPAddress ip, IPAddress subnet);
    uint8_t calculateSubnetCIDR(IPAddress subnetMask);

  protected:
    friend class WiFiSTAClass;
    friend class WiFiScanClass;
    friend class WiFiAPClass;
};

#endif /* ESP32WIFIGENERIC_H_ */
