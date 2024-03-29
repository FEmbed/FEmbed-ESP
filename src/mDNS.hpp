//
// Created by GeneKong on 2022/7/29.
// Port from https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/src/ESPmDNS.cpp
//

#ifndef EXAMPLE_WEBSOCKETSERVER_LIB_FEMBED_ESP_SRC_MDNS_HPP_
#define EXAMPLE_WEBSOCKETSERVER_LIB_FEMBED_ESP_SRC_MDNS_HPP_

#include <Service.h>
#include "Arduino.h"
#include "IPv6Address.h"
#include "IPAddress.h"
#include "mdns.h"

//this should be defined at build time
#ifndef ARDUINO_VARIANT
#define ARDUINO_VARIANT "esp32"
#endif

namespace FEmbed {

class mDNS : public Service<mDNS> {
 public:
    mDNS();
    ~mDNS();
    bool begin(const char *hostName);
    void end();

    void setInstanceName(String name);
    void setInstanceName(const char *name) {
        setInstanceName(String(name));
    }
    void setInstanceName(char *name) {
        setInstanceName(String(name));
    }

    bool addService(char *service, char *proto, uint16_t port);
    bool addService(const char *service, const char *proto, uint16_t port) {
        return addService((char *) service, (char *) proto, port);
    }
    bool addService(String service, String proto, uint16_t port) {
        return addService(service.c_str(), proto.c_str(), port);
    }

    bool addServiceTxt(char *name, char *proto, char *key, char *value);
    void addServiceTxt(const char *name, const char *proto, const char *key, const char *value) {
        addServiceTxt((char *) name, (char *) proto, (char *) key, (char *) value);
    }
    void addServiceTxt(String name, String proto, String key, String value) {
        addServiceTxt(name.c_str(), proto.c_str(), key.c_str(), value.c_str());
    }

    void enableArduino(uint16_t port = 3232, bool auth = false);
    void disableArduino();

    void enableWorkstation(esp_interface_t interface = ESP_IF_WIFI_STA);
    void disableWorkstation();

    IPAddress queryHost(char *host, uint32_t timeout = 2000);
    IPAddress queryHost(const char *host, uint32_t timeout = 2000) {
        return queryHost((char *) host, timeout);
    }
    IPAddress queryHost(String host, uint32_t timeout = 2000) {
        return queryHost(host.c_str(), timeout);
    }

    int queryService(char *service, char *proto);
    int queryService(const char *service, const char *proto) {
        return queryService((char *) service, (char *) proto);
    }
    int queryService(String service, String proto) {
        return queryService(service.c_str(), proto.c_str());
    }

    String hostname(int idx);
    IPAddress IP(int idx);
    IPv6Address IPv6(int idx);
    uint16_t port(int idx);
    int numTxt(int idx);
    bool hasTxt(int idx, const char *key);
    String txt(int idx, const char *key);
    String txt(int idx, int txtIdx);
    String txtKey(int idx, int txtIdx);

 private:
    String _hostname;
    mdns_result_t *results;
    mdns_result_t *_getResult(int idx);
    mdns_txt_item_t *_getResultTxt(int idx, int txtIdx);
};

} // FEmbed

#endif //EXAMPLE_WEBSOCKETSERVER_LIB_FEMBED_ESP_SRC_MDNS_HPP_
