// ArduinoNvs.cpp

// Copyright (c) 2018 Sinai RnD
// Copyright (c) 2016-2017 TridentTD

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "ArduinoNvs.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "ArdNVS"

ArduinoNvs::ArduinoNvs(String namespaceNvs, bool auto_reinit)
{
    FEmbed::OSMutexLocker locker(nvs_global_lock);
    _nvs_valid = false;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        log_w("Cannot init flash mem");
        if (err != ESP_ERR_NVS_NO_FREE_PAGES)
        {
            log_w("flash init failed");
            return;
        }

        if (!auto_reinit)
            return;
        // erase and reinit
        log_w("Try reinit the partition");
        const esp_partition_t *nvs_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
        if (nvs_partition == NULL)
            return;
        err = esp_partition_erase_range(nvs_partition, 0, nvs_partition->size);
        esp_err_t err = nvs_flash_init();
        if (err)
            return;
        log_w("Partition re-formatted");
    }

    err = nvs_open(namespaceNvs.c_str(), NVS_READWRITE, &_nvs_handle);
    if (err != ESP_OK)
    {
        log_w("nvs open failed %s.", namespaceNvs.c_str());
        return;
    }

    log_i("nvs %s init successful.", namespaceNvs.c_str());
    _nvs_valid = true;
}

ArduinoNvs::~ArduinoNvs()
{
    if (_nvs_valid)
    {
        nvs_global_lock.lock();
        nvs_close(_nvs_handle);
        nvs_global_lock.unlock();
    }
}

bool ArduinoNvs::eraseAll(bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_erase_all(_nvs_handle);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("eraseAll failed(%d).", err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::erase(String key, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_erase_key(_nvs_handle, key.c_str());
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("erase `%s` failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::commit()
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_commit(_nvs_handle);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("commit failed(%d).", err);
        return false;
    }
    return true;
}

bool ArduinoNvs::setInt(String key, uint8_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_u8(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setInt(String key, int16_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_i16(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setInt(String key, uint16_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_u16(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setInt(String key, int32_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_i32(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setInt(String key, uint32_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_u32(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}
bool ArduinoNvs::setInt(String key, int64_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_i64(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setInt(String key, uint64_t value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_u64(_nvs_handle, (char *)key.c_str(), value);
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setInt %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setString(String key, String value, bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_str(_nvs_handle, (char *)key.c_str(),
                                value.c_str());
    nvs_global_lock.unlock();
    if (err != ESP_OK)
    {
        log_w("setString %s failed(%d).", key.c_str(), err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setBlob(String key, uint8_t *blob, size_t length,
                         bool forceCommit)
{
    if (!_nvs_valid)
        return false;
    log_d("ArduinoNvs::setObjct(): set obj addr = [0x%X], length = [%d]\n",
          (int32_t)blob, length);
    if (length == 0)
        return false;
    nvs_global_lock.lock();
    esp_err_t err = nvs_set_blob(_nvs_handle, (char *)key.c_str(), blob,
                                 length);
    nvs_global_lock.unlock();
    if (err)
    {
        log_d("ArduinoNvs::setObjct(): err = [0x%X]\n", err);
        return false;
    }
    return forceCommit ? commit() : true;
}

bool ArduinoNvs::setBlob(String key, std::vector<uint8_t> &blob,
                         bool forceCommit)
{
    return setBlob(key, &blob[0], blob.size(), forceCommit);
}

int64_t ArduinoNvs::getInt(String key, int64_t default_value)
{
    uint8_t v_u8;
    int16_t v_i16;
    uint16_t v_u16;
    int32_t v_i32;
    uint32_t v_u32;
    int64_t v_i64;
    uint64_t v_u64;

    esp_err_t err;
    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    err = nvs_get_u8(_nvs_handle, (char *)key.c_str(), &v_u8);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_u8;

    nvs_global_lock.lock();
    err = nvs_get_i16(_nvs_handle, (char *)key.c_str(), &v_i16);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_i16;

    nvs_global_lock.lock();
    err = nvs_get_u16(_nvs_handle, (char *)key.c_str(), &v_u16);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_u16;

    nvs_global_lock.lock();
    err = nvs_get_i32(_nvs_handle, (char *)key.c_str(), &v_i32);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_i32;

    nvs_global_lock.lock();
    err = nvs_get_u32(_nvs_handle, (char *)key.c_str(), &v_u32);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_u32;

    nvs_global_lock.lock();
    err = nvs_get_i64(_nvs_handle, (char *)key.c_str(), &v_i64);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_i64;

    nvs_global_lock.lock();
    err = nvs_get_u64(_nvs_handle, (char *)key.c_str(), &v_u64);
    nvs_global_lock.unlock();
    if (err == ESP_OK)
        return (int64_t)v_u64;

    return default_value;
}

bool ArduinoNvs::getString(String key, String &res)
{
    size_t required_size;
    esp_err_t err;

    if (!_nvs_valid)
        return false;
    nvs_global_lock.lock();
    err = nvs_get_str(_nvs_handle, key.c_str(), NULL, &required_size);
    nvs_global_lock.unlock();
    if (err)
        return false;

    char value[required_size];
    nvs_global_lock.lock();
    err = nvs_get_str(_nvs_handle, key.c_str(), value, &required_size);
    nvs_global_lock.unlock();
    if (err)
        return false;
    res = value;
    return true;
}

String ArduinoNvs::getString(String key)
{
    static String res;
    bool ok = getString(key, res);
    if (!ok)
        return String();
    return res;
}

size_t ArduinoNvs::getBlobSize(String key)
{
    size_t required_size;
    if (!_nvs_valid)
        return 0;
    nvs_global_lock.lock();
    esp_err_t err = nvs_get_blob(_nvs_handle, key.c_str(), NULL,
                                 &required_size);
    nvs_global_lock.unlock();
    if (err)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND) // key_not_found is not an error, just return size 0
            log_d("ArduinoNvs::getBlobSize(): err = [0x%X]\n", err);
        return 0;
    }
    return required_size;
}

bool ArduinoNvs::getBlob(String key, uint8_t *blob, size_t length)
{
    if (length == 0)
        return false;
    if (!_nvs_valid)
        return false;

    size_t required_size = getBlobSize(key);
    if (required_size == 0)
        return false;
    if (length < required_size)
        return false;

    nvs_global_lock.lock();
    esp_err_t err = nvs_get_blob(_nvs_handle, key.c_str(), blob,
                                 &required_size);
    nvs_global_lock.unlock();
    if (err)
    {
        log_d("ArduinoNvs::getBlob(): get object err = [0x%X]\n", err);
        return false;
    }
    return true;
}

bool ArduinoNvs::getBlob(String key, std::vector<uint8_t> &blob)
{
    size_t required_size = getBlobSize(key);
    if (required_size == 0)
        return false;

    blob.resize(required_size);
    nvs_global_lock.lock();
    esp_err_t err = nvs_get_blob(_nvs_handle, key.c_str(), &blob[0],
                                 &required_size);
    nvs_global_lock.unlock();
    if (err)
    {
        log_d("ArduinoNvs::getBlob(): get object err = [0x%X]\n", err);
        return false;
    }
    return true;
}

std::vector<uint8_t> ArduinoNvs::getBlob(String key)
{
    std::vector<uint8_t> res;
    bool ok = getBlob(key, res);
    if (!ok)
        res.clear();
    return res;
}

bool ArduinoNvs::setFloat(String key, float value, bool forceCommit)
{
    return setBlob(key, (uint8_t *)&value, sizeof(float), forceCommit);
}

float ArduinoNvs::getFloat(String key, float default_value)
{
    std::vector<uint8_t> res(sizeof(float));
    if (!getBlob(key, res))
        return default_value;
    return *(float *)(&res[0]);
}

FEmbed::OSMutex ArduinoNvs::nvs_global_lock;
