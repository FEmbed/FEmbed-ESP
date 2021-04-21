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
#include <OSTask.h>

#include "esp_blufi_api.h"
#include "esp_gap_ble_api.h"

#include "mbedtls/aes.h"
#include "mbedtls/dhm.h"
#include "mbedtls/md5.h"

namespace FEmbed {

class BluFi {
public:
    BluFi();
    virtual ~BluFi();

    static void init(std::string deviceName);
    static void deinit();

    // BluFi callbacks
    static void eventHandler(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);
    static void negotiateDataHandler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free);
    static int encryptFunc(uint8_t iv8, uint8_t *crypt_data, int cyprt_len);
    static int decryptFunc(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
    static uint16_t checksumFunc(uint8_t iv8, uint8_t *data, int len);
private:
    struct blufi_security {
    #define DH_SELF_PUB_KEY_LEN     128
    #define DH_SELF_PUB_KEY_BIT_LEN (DH_SELF_PUB_KEY_LEN * 8)
        uint8_t  self_public_key[DH_SELF_PUB_KEY_LEN];
    #define SHARE_KEY_LEN           128
    #define SHARE_KEY_BIT_LEN       (SHARE_KEY_LEN * 8)
        uint8_t  share_key[SHARE_KEY_LEN];
        size_t   share_len;
    #define PSK_LEN                 16
        uint8_t  psk[PSK_LEN];
        uint8_t  *dh_param;
        int      dh_param_len;
        uint8_t  iv[16];
        mbedtls_dhm_context dhm;
        mbedtls_aes_context aes;
    };

    int securityInit(void);
    void securityDeinit(void);

    static struct blufi_security *_blufi_sec;
    static esp_ble_adv_data_t _adv_data;
    static esp_ble_adv_params_t _adv_params;
    static bool _ble_is_connected;
    static std::string _device_name;
};

#endif
} /* namespace FEmbed */

#endif /* LIB_FEMBED_ESP_SRC_BLUFI_H_ */
