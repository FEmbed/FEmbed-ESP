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


#ifndef __WIFICONFIGMANAGER_H__
#define __WIFICONFIGMANAGER_H__

#include "osTask.h"
#include "osSignal.h"
#include "WString.h"

#include "tcpip_adapter.h"

#include <memory>
using std::shared_ptr;

#define WIFI_STATE_SMARTCONFIG                  (0)
#define WIFI_STATE_AP                           (1)
#define WIFI_STATE_STA                          (2)
#define WIFI_STATE_STA_CONNECTED                (3)

namespace FEmbed {

/**
 * @brief WifiManager
 * We use this class to manage Wifi connections, such as APConfig or Smartconfig.
 */
class WifiManager :
    public OSTask {
public:
    virtual ~WifiManager();

    virtual void loop();
    
    void connect();
    void disconnect();

    ///< Scan Wifi AP information.
    void startScan();
    void stopScan();

    ///< SmartConfig
    void startSmartConfig();
    void stopSmartConfig();

    /**
     * Save Raw Websocket config from phone
     */
    void saveRawWebsocketConfig(char *buf);
    void saveWebsocketCPId(char *cp_id);
    
    shared_ptr<String> getWebsocketHost();
    uint32_t getWebsocketPort();
    shared_ptr<String> getWebsocketUrl();
    shared_ptr<String> getWebsocketProtocol();
    shared_ptr<String> getWebsocketUser();
    shared_ptr<String> getWebsocketPass();
    shared_ptr<String> getWebsocketCPId();

    void setSTASsid(const char *ssid);
    void setSTAPassword(const char *password);
    void setSTASsidAndPassword(const char *ssid, const char *password);
    void startSTAConnect();

    void setAPSsid(const char *ssid);
    void setAPPassword(const char *password);
    void setAPSsidAndPassword(const char *ssid, const char *password);
    void startAPConnect();

    int wifiState() { return m_wifi_state; }

    /**
     * @brief Get Device MAC string
     * 
     * @return char* mac
     */
    uint8_t *getMAC() const { return (uint8_t *)m_mac; }
    tcpip_adapter_ip_info_t *getAdapterIpInfo() { return &m_adp_ip; }

    static WifiManager *get() {
        static WifiManager *instance = NULL;
        if(instance == NULL)
        {
            instance = new WifiManager();
            assert(instance);
        }
        return instance;
    }
protected:
    /**
     * DON'T USE New to create `WifiManager`, just use get().
     */
    WifiManager();
    /**
     * Wifi Module initial function.
     */
    void init();

private:
    char m_sta_ssid[32];
    char m_ap_ssid[32];
    char m_sta_password[64];
    char m_ap_password[64];

    uint8_t m_mac[6];              ///< device default mac information
    tcpip_adapter_ip_info_t m_adp_ip;
    
    /**
     * After Boot, module will check wifi mode, if it's STA, then check ssid, no
     * ssid for Smartconfig else normal STA.
     */
    uint8_t m_wifi_state;       ///< 0-> SMARTCONFIG, 1-> AP, 2-> STA, 3-> STA_CONNECTED
};

}
#endif
