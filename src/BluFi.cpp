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

namespace FEmbed {
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

} /* namespace FEmbed */
