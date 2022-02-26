/*
 * BluFi.h
 *
 * Copyright (c) 2021 Gene Kong
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

#ifndef LIB_FEMBED_ESP_SRC_BLUFI_H_
#define LIB_FEMBED_ESP_SRC_BLUFI_H_
#include "sdkconfig.h"

#if defined(CONFIG_BT_ENABLED)
#include <osTask.h>
#include <BLEHIDDevice.h>
#include "WString.h"

extern "C"
{
#include "esp_blufi.h"
#include "esp_gap_ble_api.h"
#include "esp_wifi.h"
}

namespace FEmbed {

typedef void (*blufi_custom_data_recv_cb_t)(uint8_t *data, uint32_t data_len);
typedef void (*blufi_custom_wifi_mode_chg_cb_t)();
typedef void (*blufi_custom_sta_conn_cb_t)();
struct blufi_security_t;

/**
 * @brief BluFi object used as a global static
 */
class BluFi {
public:
    BluFi();
    virtual ~BluFi();

    /**
     * @brief 初始化BluFi服务
     * @param deviceName
     */
    static void init(String deviceName);

    /**
     * @brief 关闭BluFi服务
     */
    static void deinit();

    /// 认证相关的函数
    /// Update for same auth/pin method.
    static void setAuthUserOrPIN(String val);
    static void setCurrentAuth(String val);
    static String getCurrentAuth();
    static String getAuth();
    static String getPIN();
    static void setAuthKey(String key);
    static void setAuthPIN(String pin);
    static String refreshPIN();
    static bool isAuthPassed();
    static bool isKeyAuthPassed();

    /**
     * @brief 发送自定义数据
     * @param data 自定义数据
     * @param data_len 自定义数据长度
     * @return 返回是否发送成功
     */
    static bool sendCustomData(uint8_t *data, uint32_t data_len);
    /**
     * @brief 自定义数据接收回调
     * @param cb 回调对象
     */
    static void setCustomRecvHandle(blufi_custom_data_recv_cb_t cb);
    /**
     * @brief 自定义连接回调
     * @param cb 回调对象
     */
    static void setCustomConnHandle(blufi_custom_sta_conn_cb_t cb);
    static void setCustomModeChgHandle(blufi_custom_wifi_mode_chg_cb_t cb);

    /**
     * @brief Custom WiFi callback.
     * @param event_base WiFi event base object
     * @param event_id event id for current base
     * @param event_data event data for current id
     * @return process success or not
     */
    static esp_err_t handleWiFiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
    /**
     * @brief Custom WiFi scan callback
     * @param count WiFi scan result count
     * @param result result objects
     */
    static void handleScanDone(uint16_t count, void *result);

    // Custom defined BLE Event.
    static void handleBLEEvent(esp_gap_ble_cb_event_t  event,
            esp_ble_gap_cb_param_t* param);

    // BluFi callbacks
    static void eventHandler(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);
    static void negotiateDataHandler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free);
    static int encryptFunc(uint8_t iv8, uint8_t *crypt_data, int cyprt_len);
    static int decryptFunc(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
    static uint16_t checksumFunc(uint8_t iv8, uint8_t *data, int len);

private:
    static int securityInit(void);
    static void securityDeinit(void);
    static void HIDInit();

    /**
     * @brief Mix HID Device with BluFi for fast access
     */
    static std::shared_ptr<BLEHIDDevice> hid;
    static std::shared_ptr<BLECharacteristic> input;
    static std::shared_ptr<BLECharacteristic> output;

    static blufi_custom_data_recv_cb_t _custom_data_recv_cb;
    static blufi_custom_sta_conn_cb_t _custom_sta_conn_cb;
    static blufi_custom_wifi_mode_chg_cb_t _custom_wifi_mode_chg_cb;

    /* Auth operate */
    static String _auth_key;
    static String _auth_pin;
    static String _auth_user_or_pin;
    static String _auth_curr_user;

    static blufi_security_t *_blufi_sec;
    static uint8_t _server_if;
    static uint16_t _conn_id;

    /* store the station info for send back to phone */
    static bool _gl_sta_connected;
    static bool _ble_is_connected;
    static uint8_t _gl_sta_bssid[6];
    static uint8_t _gl_sta_ssid[32];
    static int _gl_sta_ssid_len;
    static wifi_config_t _sta_config;
    static wifi_config_t _ap_config;

};
} /* namespace FEmbed */
#endif

#endif /* LIB_FEMBED_ESP_SRC_BLUFI_H_ */
