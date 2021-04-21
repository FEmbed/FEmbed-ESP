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

#ifdef  LOG_TAG
    #undef  LOG_TAG
#endif
#define LOG_TAG                             "BluFi"

#include <BLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>

#include "esp_bt_defs.h"
#include "esp_crc.h"

namespace FEmbed {

struct blufi_security *BluFi::_blufi_sec = NULL;
static uint8_t _blufi_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};
esp_ble_adv_data_t BluFi::_adv_data =  {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
        .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data =  NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 16,
        .p_service_uuid = _blufi_service_uuid128,
        .flag = 0x6,
    };

esp_ble_adv_params_t BluFi::_adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
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
BluFi::BluFi()
{
    // TODO Auto-generated constructor stub
    
}

BluFi::~BluFi()
{
    // TODO Auto-generated destructor stub
}

static esp_blufi_callbacks_t blufi_callbacks = {
    .event_cb = BluFi::eventHandler,
    .negotiate_data_handler = BluFi::negotiateDataHandler,
    .encrypt_func = BluFi::encryptFunc,
    .decrypt_func = BluFi::decryptFunc,
    .checksum_func = BluFi::checksumFunc,
};
void BluFi::init(std::string deviceName)
{
    esp_err_t ret;

    BLEDevice::init(deviceName);
    _device_name = deviceName;

    // Update uuid for BluFi
    // _blufi_service_uuid128 TODO
    securityInit();
    ret = esp_blufi_register_callbacks(&blufi_callbacks);
    if(ret){
        log_e("%s blufi register failed, error code = %x\n", __func__, ret);
        return;
    }
    esp_blufi_profile_init();
}

void BluFi::deinit()
{
    BLEDevice::deinit(true);
    securityDeinit();
}

int BluFi::securityInit(void)
{
    _blufi_sec = (struct blufi_security *)malloc(sizeof(struct blufi_security));
    if (_blufi_sec == NULL) {
        return ESP_FAIL;
    }

    memset(_blufi_sec, 0x0, sizeof(struct blufi_security));

    mbedtls_dhm_init(&_blufi_sec->dhm);
    mbedtls_aes_init(&_blufi_sec->aes);

    memset(_blufi_sec->iv, 0x0, 16);
    return 0;
}

void BluFi::securityDeinit(void)
{
    if (_blufi_sec == NULL) {
        return;
    }
    if (_blufi_sec->dh_param){
        free(_blufi_sec->dh_param);
        _blufi_sec->dh_param = NULL;
    }
    mbedtls_dhm_free(&_blufi_sec->dhm);
    mbedtls_aes_free(&_blufi_sec->aes);

    memset(_blufi_sec, 0x0, sizeof(struct blufi_security));

    free(_blufi_sec);
    _blufi_sec =  NULL;
}

void BluFi::eventHandler(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
        * now, as a example, we do it more simply */
       switch (event) {
       case ESP_BLUFI_EVENT_INIT_FINISH:
           log_i("BLUFI init finish\n");
           esp_ble_gap_set_device_name(_device_name.c_str());
           esp_ble_gap_config_adv_data(&_adv_data);
           break;
       case ESP_BLUFI_EVENT_DEINIT_FINISH:
           log_i("BLUFI deinit finish\n");
           break;
       case ESP_BLUFI_EVENT_BLE_CONNECT:
           log_i("BLUFI ble connect\n");
           _ble_is_connected = true;
           server_if = param->connect.server_if;
           conn_id = param->connect.conn_id;
           esp_ble_gap_stop_advertising();
           securityInit();
           break;
       case ESP_BLUFI_EVENT_BLE_DISCONNECT:
           log_i("BLUFI ble disconnect\n");
           _ble_is_connected = false;
           securityDeinit();
           esp_ble_gap_start_advertising(&_adv_params);
           break;
       case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
           log_i("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
           ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
           break;
       case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
           log_i("BLUFI requset wifi connect to AP\n");
           /* there is no wifi callback when the device has already connected to this wifi
           so disconnect wifi before connection.
           */
           esp_wifi_disconnect();
           esp_wifi_connect();
           break;
       case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
           log_i("BLUFI requset wifi disconnect from AP\n");
           esp_wifi_disconnect();
           break;
       case ESP_BLUFI_EVENT_REPORT_ERROR:
           BLUFI_ERROR("BLUFI report error, error code %d\n", param->report_error.state);
           esp_blufi_send_error_info(param->report_error.state);
           break;
       case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
           wifi_mode_t mode;
           esp_blufi_extra_info_t info;

           esp_wifi_get_mode(&mode);

           if (gl_sta_connected) {
               memset(&info, 0, sizeof(esp_blufi_extra_info_t));
               memcpy(info.sta_bssid, gl_sta_bssid, 6);
               info.sta_bssid_set = true;
               info.sta_ssid = gl_sta_ssid;
               info.sta_ssid_len = gl_sta_ssid_len;
               esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
           } else {
               esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
           }
           log_i("BLUFI get wifi status from AP\n");

           break;
       }
       case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
           log_i("blufi close a gatt connection");
           esp_blufi_close(server_if, conn_id);
           break;
       case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
           /* TODO */
           break;
       case ESP_BLUFI_EVENT_RECV_STA_BSSID:
           memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
           sta_config.sta.bssid_set = 1;
           esp_wifi_set_config(WIFI_IF_STA, &sta_config);
           log_i("Recv STA BSSID %s\n", sta_config.sta.ssid);
           break;
       case ESP_BLUFI_EVENT_RECV_STA_SSID:
           strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
           sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
           esp_wifi_set_config(WIFI_IF_STA, &sta_config);
           log_i("Recv STA SSID %s\n", sta_config.sta.ssid);
           break;
       case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
           strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
           sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
           esp_wifi_set_config(WIFI_IF_STA, &sta_config);
           log_i("Recv STA PASSWORD %s\n", sta_config.sta.password);
           break;
       case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
           strncpy((char *)ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
           ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
           ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
           esp_wifi_set_config(WIFI_IF_AP, &ap_config);
           log_i("Recv SOFTAP SSID %s, ssid len %d\n", ap_config.ap.ssid, ap_config.ap.ssid_len);
           break;
       case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
           strncpy((char *)ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
           ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
           esp_wifi_set_config(WIFI_IF_AP, &ap_config);
           log_i("Recv SOFTAP PASSWORD %s len = %d\n", ap_config.ap.password, param->softap_passwd.passwd_len);
           break;
       case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
           if (param->softap_max_conn_num.max_conn_num > 4) {
               return;
           }
           ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
           esp_wifi_set_config(WIFI_IF_AP, &ap_config);
           log_i("Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
           break;
       case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
           if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
               return;
           }
           ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
           esp_wifi_set_config(WIFI_IF_AP, &ap_config);
           log_i("Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
           break;
       case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
           if (param->softap_channel.channel > 13) {
               return;
           }
           ap_config.ap.channel = param->softap_channel.channel;
           esp_wifi_set_config(WIFI_IF_AP, &ap_config);
           log_i("Recv SOFTAP CHANNEL %d\n", ap_config.ap.channel);
           break;
       case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
           wifi_scan_config_t scanConf = {
               .ssid = NULL,
               .bssid = NULL,
               .channel = 0,
               .show_hidden = false
           };
           esp_wifi_scan_start(&scanConf, true);
           break;
       }
       case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
           log_i("Recv Custom Data %d\n", param->custom_data.data_len);
           esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
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
           break;;
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
#define SEC_TYPE_DH_PARAM_LEN   0x00
#define SEC_TYPE_DH_PARAM_DATA  0x01
#define SEC_TYPE_DH_P           0x02
#define SEC_TYPE_DH_G           0x03
#define SEC_TYPE_DH_PUBLIC      0x04

void BluFi::negotiateDataHandler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free)
{
    int ret;
    uint8_t type = data[0];

    if (_blufi_sec == NULL) {
        BLUFI_ERROR("BLUFI Security is not initialized");
        btc_blufi_report_error(ESP_BLUFI_INIT_SECURITY_ERROR);
        return;
    }

    switch (type) {
    case SEC_TYPE_DH_PARAM_LEN:
        _blufi_sec->dh_param_len = ((data[1]<<8)|data[2]);
        if (_blufi_sec->dh_param) {
            free(_blufi_sec->dh_param);
            _blufi_sec->dh_param = NULL;
        }
        _blufi_sec->dh_param = (uint8_t *)malloc(_blufi_sec->dh_param_len);
        if (_blufi_sec->dh_param == NULL) {
            btc_blufi_report_error(ESP_BLUFI_DH_MALLOC_ERROR);
            BLUFI_ERROR("%s, malloc failed\n", __func__);
            return;
        }
        break;
    case SEC_TYPE_DH_PARAM_DATA:{
        if (_blufi_sec->dh_param == NULL) {
            BLUFI_ERROR("%s, blufi_sec->dh_param == NULL\n", __func__);
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        uint8_t *param = _blufi_sec->dh_param;
        memcpy(_blufi_sec->dh_param, &data[1], _blufi_sec->dh_param_len);
        ret = mbedtls_dhm_read_params(&_blufi_sec->dhm, &param, &param[_blufi_sec->dh_param_len]);
        if (ret) {
            BLUFI_ERROR("%s read param failed %d\n", __func__, ret);
            btc_blufi_report_error(ESP_BLUFI_READ_PARAM_ERROR);
            return;
        }
        free(_blufi_sec->dh_param);
        _blufi_sec->dh_param = NULL;
        ret = mbedtls_dhm_make_public(&_blufi_sec->dhm, (int) mbedtls_mpi_size( &_blufi_sec->dhm.P ), _blufi_sec->self_public_key, _blufi_sec->dhm.len, myrand, NULL);
        if (ret) {
            BLUFI_ERROR("%s make public failed %d\n", __func__, ret);
            btc_blufi_report_error(ESP_BLUFI_MAKE_PUBLIC_ERROR);
            return;
        }

        mbedtls_dhm_calc_secret( &_blufi_sec->dhm,
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

    }
        break;
    case SEC_TYPE_DH_P:
        break;
    case SEC_TYPE_DH_G:
        break;
    case SEC_TYPE_DH_PUBLIC:
        break;
    }
}

int BluFi::encryptFunc(uint8_t iv8, uint8_t *crypt_data, int cyprt_len)
{
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];

    memcpy(iv0, _blufi_sec->iv, sizeof(_blufi_sec->iv));
    iv0[0] = iv8;   /* set iv8 as the iv0[0] */

    ret = mbedtls_aes_crypt_cfb128(&_blufi_sec->aes, MBEDTLS_AES_ENCRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) {
        return -1;
    }

    return crypt_len;
}

int BluFi::decryptFunc(uint8_t iv8, uint8_t *crypt_data, int crypt_len)
{
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];

    memcpy(iv0, _blufi_sec->iv, sizeof(_blufi_sec->iv));
    iv0[0] = iv8;   /* set iv8 as the iv0[0] */

    ret = mbedtls_aes_crypt_cfb128(&_blufi_sec->aes, MBEDTLS_AES_DECRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) {
        return -1;
    }

    return crypt_len;
}

uint16_t BluFi::checksumFunc(uint8_t iv8, uint8_t *data, int len)
{
    /* This iv8 ignore, not used */
    return esp_crc16_be(0, data, len);
}

} /* namespace FEmbed */

#endif
