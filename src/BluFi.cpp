/*
 * BluFi.cpp
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
#include <BluFi.h>
#if defined(CONFIG_BT_ENABLED)

#include <BLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>

#include "mbedtls/aes.h"
#include "mbedtls/dhm.h"
#include "mbedtls/md5.h"

#include "esp_bt_defs.h"
#include "esp_crc.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BluFi"

extern "C" void esp_blufi_disconnect(void);

namespace FEmbed {

struct blufi_security_t {
#define DH_SELF_PUB_KEY_LEN 128
#define DH_SELF_PUB_KEY_BIT_LEN (DH_SELF_PUB_KEY_LEN * 8)
    uint8_t self_public_key[DH_SELF_PUB_KEY_LEN];
#define SHARE_KEY_LEN 128
#define SHARE_KEY_BIT_LEN (SHARE_KEY_LEN * 8)
    uint8_t share_key[SHARE_KEY_LEN];
    size_t share_len;
#define PSK_LEN 16
    uint8_t psk[PSK_LEN];
    uint8_t *dh_param;
    int dh_param_len;
    uint8_t iv[16];
    mbedtls_dhm_context dhm;
    mbedtls_aes_context aes;
};

//first uuid, 16bit, [2],[3] ff ff is the value
#define _blufi_service_uuid "0000ffff-0000-1000-8000-00805f9b34fb"

blufi_security_t *BluFi::_blufi_sec = NULL;

uint8_t BluFi::_server_if = 0;
uint16_t BluFi::_conn_id = 0;

bool BluFi::_gl_sta_connected = false;
bool BluFi::_ble_is_connected = false;
uint8_t BluFi::_gl_sta_bssid[6] = {
    0,
};
uint8_t BluFi::_gl_sta_ssid[32] = {
    0,
};
int BluFi::_gl_sta_ssid_len = 0;

wifi_config_t BluFi::_sta_config = {
    {{0}},
};
wifi_config_t BluFi::_ap_config = {
    {{0}},
};

blufi_custom_data_recv_cb_t BluFi::_custom_data_recv_cb = NULL;
blufi_custom_sta_conn_cb_t BluFi::_custom_sta_conn_cb = NULL;
blufi_custom_wifi_mode_chg_cb_t BluFi::_custom_wifi_mode_chg_cb = NULL;
/**
 * @fn  BluFi()
 *
 * # Overview
 *
 * The BluFi for ESP32 is a Wi-Fi network configuration function via Bluetooth channel.
 * It provides a secure protocol to pass Wi-Fi configuration and credentials to the ESP32.
 * Using this information ESP32 can then e.g. connect to an AP or establish a SoftAP.
 *
 * Fragmenting, data encryption, checksum verification in the BluFi layer are the key
 * elements of this process.
 * You can customize symmetric encryption, asymmetric encryption and checksum support
 * customization.
 *
 * Here we use the DH algorithm for key negotiation, 128-AES algorithm for data encryption,
 *  and CRC16 algorithm for checksum verification.
 *
 *  More information please refer https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/blufi.html
 */
BluFi::BluFi() {
}

BluFi::~BluFi() {
}

static esp_blufi_callbacks_t blufi_callbacks = {
    .event_cb = BluFi::eventHandler,
    .negotiate_data_handler = BluFi::negotiateDataHandler,
    .encrypt_func = BluFi::encryptFunc,
    .decrypt_func = BluFi::decryptFunc,
    .checksum_func = BluFi::checksumFunc,
};

class SecurityCallback : public BLESecurityCallbacks {

    uint32_t onPassKeyRequest() {
        return 0;
    }

    void onPassKeyNotify(uint32_t pass_key) {}

    bool onConfirmPIN(uint32_t pass_key) {
        vTaskDelay(5000);
        return true;
    }

    bool onSecurityRequest() {
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
        if (cmpl.success) {
            log_d("   - SecurityCallback - Authentication Success");
        } else {
            log_d("   - SecurityCallback - Authentication Failure*");
        }
    }
};

static void __attribute__((unused)) remove_all_bonded_devices(void)
{
    int dev_num = esp_ble_get_bond_device_num();

    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list);
    for (int i = 0; i < dev_num; i++) {
        esp_ble_remove_bond_device(dev_list[i].bd_addr);
    }

    free(dev_list);
}

void BluFi::HIDInit()
{
    BLEServer *pServer = BLEDevice::createServer();
    /*
        * Instantiate hid device
    */
    hid.reset(new BLEHIDDevice(pServer));

    input.reset(hid->inputReport(1));
    output.reset(hid->outputReport(1));
    hid->manufacturer()->setValue("ESP");
    /*
     * Set pnp parameters (MANDATORY)
     * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.pnp_id.xml
     */
    hid->pnp(0x02, 0x0005, 0x0001, 0x0100);
    /*
     * Set hid informations (MANDATORY)
     * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.hid_information.xml
     */
    hid->hidInfo(0x00,0x01);

    static const uint8_t simple_keyboard[] = {
            0x05, 0x01,     /* USAGE_PAGE (Generic Desktop) */
            0x09, 0x06,     /* USAGE (Keyboard) */
            0xa1, 0x01,     /* COLLECTION (Application) */
            0x05, 0x07,   /*   USAGE_PAGE (Keyboard) */
            0x85, 0x01,    /*   REPORT_ID (1) */
            0x05, 0x07,   /*   USAGE_PAGE (Keyboard) */
            0x19, 0x00,   /*   USAGE_MINIMUM (Reserved (no event indicated)) */
            0x29, 0x65,   /*   USAGE_MAXIMUM (Keyboard Application) */
            0x15, 0x00,   /*   LOGICAL_MINIMUM (0) */
            0x25, 0x65,   /*   LOGICAL_MAXIMUM (101) */
            0x95, 0x06,   /*   REPORT_COUNT (6) */
            0x75, 0x08,   /*   REPORT_SIZE (8) */
            0x81, 0x00,   /*   INPUT (Data,Ary,Abs) */
            0xc0
    };
    hid->reportMap((uint8_t*)simple_keyboard, sizeof(simple_keyboard));
    hid->startServices();
}

void BluFi::init(String deviceName) {
    esp_err_t ret;
    BLEDevice::init(deviceName.c_str());
    BLEDevice::setSecurityCallbacks(new SecurityCallback());

#if CONFIG_FEMBED_BLUFI_BOND_ENABLE
    HIDInit();

    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
#endif

    /// Add BluFi handler.
    BLEDevice::setCustomGapHandler(BluFi::handleBLEEvent);
    WiFi->addCustomWiFiHandler(BluFi::handleWiFiEvent);
    WiFi->setScanDoneHandle(BluFi::handleScanDone);

    BluFi::securityInit();
    ret = esp_blufi_register_callbacks(&blufi_callbacks);
    if (ret) {
        log_e("%s Blufi register failed, error code = %x", __func__, ret);
        return;
    }

    esp_blufi_profile_init();
}

void BluFi::deinit() {
    esp_blufi_profile_deinit();
    BluFi::securityDeinit();
    BLEDevice::deinit(true);
}

/**
 * @brief 用于认证的相关变量
 * 
 */
String BluFi::_auth_key;                    ///  设备中存储的认证KEY，一般用用户的idtoken
String BluFi::_auth_pin;                    ///  设备中存储的认证PIN，一般用户存储的PIN码
String BluFi::_auth_user_or_pin;            ///  当前通过认证的信息，可能是用户KEY，或PIN值
String BluFi::_auth_curr_user;              ///  当前连接的用户KEY
std::shared_ptr<BLEHIDDevice> BluFi::hid;
std::shared_ptr<BLECharacteristic> BluFi::input;
std::shared_ptr<BLECharacteristic> BluFi::output;

/**
 * @fn void setAuthKey(String)
 *
 * Set BluFi auth key, if key is null or empty, no auth.
 *
 * @param key auth key value.
 */
void BluFi::setAuthKey(String key) {
    _auth_key = key;
}

void BluFi::setAuthPIN(String pin) {
    _auth_pin = pin;
}

// 认证用的
void BluFi::setAuthUserOrPIN(String val) {
    _auth_user_or_pin = val;
}

void BluFi::setCurrentAuth(String val) {
    _auth_curr_user = val;
}

String BluFi::getCurrentAuth() {
    return _auth_curr_user;
}

String BluFi::getAuth() {
    return _auth_key;
}

String BluFi::getPIN() {
    return _auth_pin;
}

// Random generate PIN - 6 numbers
String BluFi::refreshPIN() {
    char _pin[7];
    randomSeed(sys_now());
    for (int i = 0; i < 6; i++)
        _pin[i] = random('0', '9');
    _pin[6] = 0;
    _auth_pin = _pin;
    return _auth_pin;
}

bool BluFi::isAuthPassed() {
#if ONLY_USE_BLUETOOTH
    return true;
#endif
    if (_auth_key.length() == 0)
        return true;

    if (_auth_key == _auth_user_or_pin)
        return true;

    if (_auth_pin == _auth_user_or_pin)
        return true;
#if LFS_AGING_TEST == 0
    log_d("Auth is not pass!");
#endif
    return false;
}

/**
 * 确认当前连接的用户或KEY认证是通过的
 *
 * @return true 通过/false 不通过
 */
bool BluFi::isKeyAuthPassed()
{
#if ONLY_USE_BLUETOOTH
    return true;
#endif
    if (_auth_key.length() == 0)
        return true;

    if (_auth_key == _auth_user_or_pin)
        return true;
#if LFS_AGING_TEST == 0
    log_d("Auth is not pass!");
#endif
    return false;
}

/**
 * @fn bool sendCustomData(uint8_t*, uint32_t)
 *
 * Send custom data by ble.
 *
 * @param data data contend.
 * @param data_len data length.
 *
 * @return send data succussful or not.
 */
bool BluFi::sendCustomData(uint8_t *data, uint32_t data_len) {
    if (_ble_is_connected == true) {
        return esp_blufi_send_custom_data(data, data_len) == ESP_OK;
    }
    return false;
}

void BluFi::setCustomRecvHandle(blufi_custom_data_recv_cb_t cb) {
    _custom_data_recv_cb = cb;
}

void BluFi::setCustomConnHandle(blufi_custom_sta_conn_cb_t cb) {
    _custom_sta_conn_cb = cb;
}

void BluFi::setCustomModeChgHandle(blufi_custom_wifi_mode_chg_cb_t cb) {
    _custom_wifi_mode_chg_cb = cb;
}


int BluFi::securityInit(void) {
    _blufi_sec = (blufi_security_t *)malloc(sizeof(blufi_security_t));
    if (_blufi_sec == NULL) {
        return ESP_FAIL;
    }

    memset(_blufi_sec, 0x0, sizeof(blufi_security_t));

    mbedtls_dhm_init(&_blufi_sec->dhm);
    mbedtls_aes_init(&_blufi_sec->aes);

    memset(_blufi_sec->iv, 0x0, 16);
    return 0;
}

void BluFi::securityDeinit(void) {
    if (_blufi_sec == NULL) {
        return;
    }
    if (_blufi_sec->dh_param) {
        free(_blufi_sec->dh_param);
        _blufi_sec->dh_param = NULL;
    }
    mbedtls_dhm_free(&_blufi_sec->dhm);
    mbedtls_aes_free(&_blufi_sec->aes);

    memset(_blufi_sec, 0x0, sizeof(blufi_security_t));

    free(_blufi_sec);
    _blufi_sec = NULL;
}

esp_err_t BluFi::handleWiFiEvent(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data) {
    wifi_event_sta_connected_t *event;
    wifi_mode_t mode;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_CONNECTED:
            _gl_sta_connected = true;
            event = (wifi_event_sta_connected_t *)event_data;
            memcpy(_gl_sta_bssid, event->bssid, 6);
            memcpy(_gl_sta_ssid, event->ssid, event->ssid_len);
            _gl_sta_ssid_len = event->ssid_len;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
                   auto-reassociate. */
            _gl_sta_connected = false;
            memset(_gl_sta_ssid, 0, 32);
            memset(_gl_sta_bssid, 0, 6);
            _gl_sta_ssid_len = 0;
            break;
        case WIFI_EVENT_AP_START:
            esp_wifi_get_mode(&mode);

            /* TODO: get config or information of softap, then set to report extra_info */
            if (_ble_is_connected == true) {
                if (_gl_sta_connected) {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
                } else {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
                }
            } else {
                log_i("BLUFI is not connected at ap start");
            }
            break;
        default:;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            esp_blufi_extra_info_t info;
            esp_wifi_get_mode(&mode);
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, _gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = _gl_sta_ssid;
            info.sta_ssid_len = _gl_sta_ssid_len;
            if (_ble_is_connected == true) {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
            } else {
                log_i("BLUFI BLE is not connected at got ip.");
            }
            ///< bind ok with WiFi connected.
            if (isKeyAuthPassed()
                && _auth_curr_user.length() > 0) {
                setAuthKey(_auth_curr_user);
                refreshPIN();
            }
            if (_custom_sta_conn_cb != NULL)
                _custom_sta_conn_cb();
            break;
        }
        default:
            break;
        }
    }
    return ESP_OK;
}

void BluFi::handleScanDone(uint16_t apCount, void *result) {
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)result;
    if (apCount > 0) {
        esp_blufi_ap_record_t *blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            log_e("malloc error, blufi_ap_list is NULL");
            return;
        }
        for (int i = 0; i < apCount; ++i) {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }

        if (_ble_is_connected == true) {
            esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        } else {
            log_i("BLUFI BLE is not connected after scan done.");
        }
        free(blufi_ap_list);
    }
}

void BluFi::handleBLEEvent(esp_gap_ble_cb_event_t event,
                           esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        break;
    default:
        break;
    }
}

void BluFi::eventHandler(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    /**
     * D (209028) nvs: nvs_set_blob sta.apinfo 700
     * E (213968) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
     * E (213968) task_wdt:  - IDLE (CPU 0)
     * E (213968) task_wdt: Tasks currently running:
     * E (213968) task_wdt: CPU 0: btController
     * E (213968) task_wdt: CPU 1: IDLE
     * E (213968) task_wdt: Print CPU 0 (current core) backtrace
     *
     * Some function is slow, so reset task watchdog here.
     */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH: {
        log_i("BLUFI init finish");
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(_blufi_service_uuid);
        //pAdvertising->setAppearance(HID_CARD_READER);    // <-- optional
        pAdvertising->setScanResponse(false);
        pAdvertising->setMinPreferred(0x0006);
        pAdvertising->setMaxPreferred(0x0010);
        pAdvertising->setMinInterval(0x100);
        pAdvertising->setMaxInterval(0x100);

        _auth_user_or_pin.clear();
        _auth_curr_user.clear();
        BLEDevice::startAdvertising();
        break;
    }
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        log_i("BLUFI deinit finish");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        log_d("BLUFI ble connect");
        _ble_is_connected = true;
        _server_if = param->connect.server_if;
        _conn_id = param->connect.conn_id;
        _auth_user_or_pin.clear();
        _auth_curr_user.clear();
        BLEDevice::stopAdvertising();
        BluFi::securityInit();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        log_d("BLUFI ble disconnect");
        _ble_is_connected = false;
        //remove_all_bonded_devices();
        BluFi::securityDeinit();
        BLEDevice::startAdvertising();
        break;
#if ONLY_USE_BLUETOOTH
#else
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        if (isAuthPassed()) {
            log_i("BLUFI Set WIFI opmode %d", param->wifi_mode.op_mode);
            if(_custom_wifi_mode_chg_cb != NULL)
            {
                _custom_wifi_mode_chg_cb();
            }
            WiFi->mode(WIFI_MODE_NULL);
            delay(500);
            WiFi->mode(param->wifi_mode.op_mode);
        }
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        if (isAuthPassed()) {
            log_i("BLUFI requset wifi connect to AP");
            /*
                * there is no wifi callback when the device has already connected to this WiFi
                * so disconnect wifi before connection.
               */
            delay(100);
            WiFi->reconnect();
        }
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        if (isAuthPassed()) {
            log_i("BLUFI requset wifi disconnect from AP");
            WiFi->disconnect(true);
        }
        break;
#endif
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        log_e("BLUFI report error, error code %d", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
#if ONLY_USE_BLUETOOTH
#else
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);
        if (_gl_sta_connected) {
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, _gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = _gl_sta_ssid;
            info.sta_ssid_len = _gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        log_i("BLUFI get wifi status from AP");
        break;
    }
#endif
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        log_i("blufi close a gatt connection");
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
#if ONLY_USE_BLUETOOTH
#else
    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        if (isAuthPassed()) {
            memcpy(_sta_config.sta.bssid, param->sta_bssid.bssid, 6);
            _sta_config.sta.bssid_set = 1;
            esp_wifi_set_config(WIFI_IF_STA, &_sta_config);
            log_i("Recv STA BSSID %s", _sta_config.sta.ssid);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (isAuthPassed()) {
            strncpy((char *)_sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
            _sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
            esp_wifi_set_config(WIFI_IF_STA, &_sta_config);
            log_i("Recv STA SSID %s", _sta_config.sta.ssid);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (isAuthPassed()) {
            strncpy((char *)_sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
            _sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
            esp_wifi_set_config(WIFI_IF_STA, &_sta_config);
            log_i("Recv STA PASSWORD %s", _sta_config.sta.password);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        if (isAuthPassed()) {
            strncpy((char *)_ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
            _ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
            _ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
            esp_wifi_set_config(WIFI_IF_AP, &_ap_config);
            log_i("Recv SOFTAP SSID %s, ssid len %d", _ap_config.ap.ssid, _ap_config.ap.ssid_len);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        if (isAuthPassed()) {
            strncpy((char *)_ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
            _ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
            esp_wifi_set_config(WIFI_IF_AP, &_ap_config);
            log_i("Recv SOFTAP PASSWORD %s len = %d", _ap_config.ap.password, param->softap_passwd.passwd_len);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (isAuthPassed()) {
            if (param->softap_max_conn_num.max_conn_num > 4) {
                return;
            }
            _ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
            esp_wifi_set_config(WIFI_IF_AP, &_ap_config);
            log_i("Recv SOFTAP MAX CONN NUM %d", _ap_config.ap.max_connection);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (isAuthPassed()) {
            if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
                return;
            }
            _ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
            esp_wifi_set_config(WIFI_IF_AP, &_ap_config);
            log_i("Recv SOFTAP AUTH MODE %d", _ap_config.ap.authmode);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (isAuthPassed()) {
            if (param->softap_channel.channel > 13) {
                return;
            }
            _ap_config.ap.channel = param->softap_channel.channel;
            esp_wifi_set_config(WIFI_IF_AP, &_ap_config);
            log_i("Recv SOFTAP CHANNEL %d", _ap_config.ap.channel);
        }
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        if (isAuthPassed()) {
            WiFi->scanNetworks(true, false, false, 0, 0);
        }
        break;
    }
#endif
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        log_i("Recv Custom Data %d", param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        if (_custom_data_recv_cb != NULL)
            _custom_data_recv_cb(param->custom_data.data, param->custom_data.data_len);
        break;
    case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;
        ;
    case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}
/*
   The SEC_TYPE_xxx is for self-defined packet data type in the procedure of "BLUFI negotiate key"
   If user use other negotiation procedure to exchange(or generate) key, should redefine the type by yourself.
 */
#define SEC_TYPE_DH_PARAM_LEN 0x00
#define SEC_TYPE_DH_PARAM_DATA 0x01
#define SEC_TYPE_DH_P 0x02
#define SEC_TYPE_DH_G 0x03
#define SEC_TYPE_DH_PUBLIC 0x04

extern "C" void btc_blufi_report_error(esp_blufi_error_state_t state);
static int f_rng(void *rng_state, unsigned char *output, size_t len) {
    esp_fill_random(output, len);
    return (0);
}
void BluFi::negotiateDataHandler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free) {
    int ret;
    uint8_t type = data[0];

    if (_blufi_sec == NULL) {
        log_e("BLUFI Security is not initialized");
        btc_blufi_report_error(ESP_BLUFI_INIT_SECURITY_ERROR);
        return;
    }

    switch (type) {
    case SEC_TYPE_DH_PARAM_LEN:
        _blufi_sec->dh_param_len = ((data[1] << 8) | data[2]);
        if (_blufi_sec->dh_param) {
            free(_blufi_sec->dh_param);
            _blufi_sec->dh_param = NULL;
        }
        _blufi_sec->dh_param = (uint8_t *)malloc(_blufi_sec->dh_param_len);
        if (_blufi_sec->dh_param == NULL) {
            btc_blufi_report_error(ESP_BLUFI_DH_MALLOC_ERROR);
            log_e("%s, malloc failed", __func__);
            return;
        }
        break;
    case SEC_TYPE_DH_PARAM_DATA: {
        if (_blufi_sec->dh_param == NULL) {
            log_e("%s, blufi_sec->dh_param == NULL", __func__);
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        uint8_t *param = _blufi_sec->dh_param;
        memcpy(_blufi_sec->dh_param, &data[1], _blufi_sec->dh_param_len);
        ret = mbedtls_dhm_read_params(&_blufi_sec->dhm, &param, &param[_blufi_sec->dh_param_len]);
        if (ret) {
            log_e("%s read param failed %d", __func__, ret);
            btc_blufi_report_error(ESP_BLUFI_READ_PARAM_ERROR);
            return;
        }
        free(_blufi_sec->dh_param);
        _blufi_sec->dh_param = NULL;
        ret = mbedtls_dhm_make_public(&_blufi_sec->dhm, (int) mbedtls_mpi_size(&_blufi_sec->dhm.P),
                                      _blufi_sec->self_public_key, _blufi_sec->dhm.len, f_rng, NULL);
        if (ret) {
            log_e("%s make public failed %d", __func__, ret);
            btc_blufi_report_error(ESP_BLUFI_MAKE_PUBLIC_ERROR);
            return;
        }

        mbedtls_dhm_calc_secret(&_blufi_sec->dhm,
                                _blufi_sec->share_key,
                                SHARE_KEY_BIT_LEN,
                                &_blufi_sec->share_len,
                                NULL, NULL);

        mbedtls_md5(_blufi_sec->share_key, _blufi_sec->share_len, _blufi_sec->psk);

        mbedtls_aes_setkey_enc(&_blufi_sec->aes, _blufi_sec->psk, 128);

        /* alloc output data */
        *output_data = &_blufi_sec->self_public_key[0];
        *output_len = _blufi_sec->dhm.len;
        *need_free = false;

    } break;
    case SEC_TYPE_DH_P:
        break;
    case SEC_TYPE_DH_G:
        break;
    case SEC_TYPE_DH_PUBLIC:
        break;
    }
}

int BluFi::encryptFunc(uint8_t iv8, uint8_t *crypt_data, int crypt_len) {
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];

    memcpy(iv0, _blufi_sec->iv, sizeof(_blufi_sec->iv));
    iv0[0] = iv8; /* set iv8 as the iv0[0] */

    ret = mbedtls_aes_crypt_cfb128(&_blufi_sec->aes, MBEDTLS_AES_ENCRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) {
        return -1;
    }

    return crypt_len;
}

int BluFi::decryptFunc(uint8_t iv8, uint8_t *crypt_data, int crypt_len) {
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];

    memcpy(iv0, _blufi_sec->iv, sizeof(_blufi_sec->iv));
    iv0[0] = iv8; /* set iv8 as the iv0[0] */

    ret = mbedtls_aes_crypt_cfb128(&_blufi_sec->aes, MBEDTLS_AES_DECRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) {
        return -1;
    }

    return crypt_len;
}

uint16_t BluFi::checksumFunc(uint8_t iv8, uint8_t *data, int len) {
    /* This iv8 ignore, not used */
    return esp_crc16_be(0, data, len);
}

} /* namespace FEmbed */

#endif
