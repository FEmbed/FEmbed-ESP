//
// Created by GeneKong on 2022/7/29.
//

#include "mDNS.hpp"
#include "WiFi.h"
#include <functional>
#include "esp_wifi.h"

#ifdef  LOG_TAG
#undef  LOG_TAG
#endif
#define LOG_TAG                             "mDNS"

namespace FEmbed {

// Add quotes around defined value
#ifdef __IN_ECLIPSE__
#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)
#else
#define STR(tok) tok
#endif

// static void _on_sys_event(arduino_event_t *event){
//     mdns_handle_system_event(nullptr, event);
// }

mDNS::mDNS() : results(nullptr) {}
mDNS::~mDNS() {
    end();
}

bool mDNS::begin(const char *hostName) {
    if (mdns_init()) {
        log_e("Failed starting MDNS");
        return false;
    }
    //WiFi.onEvent(_on_sys_event);
    _hostname = hostName;
    _hostname.toLowerCase();
    if (mdns_hostname_set(hostName)) {
        log_e("Failed setting MDNS hostname");
        return false;
    }
    return true;
}

void mDNS::end() {
    mdns_free();
}

void mDNS::setInstanceName(String name) {
    if (name.length() > 63) return;
    if (mdns_instance_name_set(name.c_str())) {
        log_e("Failed setting MDNS instance");
        return;
    }
}

void mDNS::enableArduino(uint16_t port, bool auth) {
    mdns_txt_item_t arduTxtData[4] = {
        {(char *) "board", (char *) STR(ARDUINO_VARIANT)},
        {(char *) "tcp_check", (char *) "no"},
        {(char *) "ssh_upload", (char *) "no"},
        {(char *) "auth_upload", (char *) "no"}
    };

    if (mdns_service_add(nullptr, "_arduino", "_tcp", port, arduTxtData, 4)) {
        log_e("Failed adding Arduino service");
    }

    if (auth && mdns_service_txt_item_set("_arduino", "_tcp", "auth_upload", "yes")) {
        log_e("Failed setting Arduino txt item");
    }
}

void mDNS::disableArduino() {
    if (mdns_service_remove("_arduino", "_tcp")) {
        log_w("Failed removing Arduino service");
    }
}

void mDNS::enableWorkstation(esp_interface_t interface) {
    char winstance[21 + _hostname.length()];
    uint8_t mac[6];
    esp_wifi_get_mac((wifi_interface_t) interface, mac);
    sprintf(winstance,
            "%s [%02x:%02x:%02x:%02x:%02x:%02x]",
            _hostname.c_str(),
            mac[0],
            mac[1],
            mac[2],
            mac[3],
            mac[4],
            mac[5]);

    if (mdns_service_add(nullptr, "_workstation", "_tcp", 9, nullptr, 0)) {
        log_e("Failed adding Workstation service");
    } else if (mdns_service_instance_name_set("_workstation", "_tcp", winstance)) {
        log_e("Failed setting Workstation service instance name");
    }
}

void mDNS::disableWorkstation() {
    if (mdns_service_remove("_workstation", "_tcp")) {
        log_w("Failed removing Workstation service");
    }
}

bool mDNS::addService(char *name, char *proto, uint16_t port) {
    char _name[strlen(name) + 2];
    char _proto[strlen(proto) + 2];
    if (name[0] == '_') {
        sprintf(_name, "%s", name);
    } else {
        sprintf(_name, "_%s", name);
    }
    if (proto[0] == '_') {
        sprintf(_proto, "%s", proto);
    } else {
        sprintf(_proto, "_%s", proto);
    }

    if (esp_err_t err = mdns_service_add(nullptr, _name, _proto, port, nullptr, 0)) {
        log_e("Failed(%d) adding service %s.%s.", err, name, proto);
        return false;
    }
    return true;
}

bool mDNS::addServiceTxt(char *name, char *proto, char *key, char *value) {
    char _name[strlen(name) + 2];
    char _proto[strlen(proto) + 2];
    if (name[0] == '_') {
        sprintf(_name, "%s", name);
    } else {
        sprintf(_name, "_%s", name);
    }
    if (proto[0] == '_') {
        sprintf(_proto, "%s", proto);
    } else {
        sprintf(_proto, "_%s", proto);
    }

    if (mdns_service_txt_item_set(_name, _proto, key, value)) {
        log_e("Failed setting service TXT");
        return false;
    }
    return true;
}

IPAddress mDNS::queryHost(char *host, uint32_t timeout) {
    esp_ip4_addr_t addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(host, timeout, &addr);
    if (err) {
        if (err == ESP_ERR_NOT_FOUND) {
            log_w("Host was not found!");
            return {};
        }
        log_e("Query Failed");
        return {};
    }
    return IPAddress(addr.addr);
}

int mDNS::queryService(char *service, char *proto) {
    if (!service || !service[0] || !proto || !proto[0]) {
        log_e("Bad Parameters");
        return 0;
    }

    if (results) {
        mdns_query_results_free(results);
        results = nullptr;
    }

    char srv[strlen(service) + 2];
    char prt[strlen(proto) + 2];
    if (service[0] == '_') {
        sprintf(srv, "%s", service);
    } else {
        sprintf(srv, "_%s", service);
    }
    if (proto[0] == '_') {
        sprintf(prt, "%s", proto);
    } else {
        sprintf(prt, "_%s", proto);
    }

    esp_err_t err = mdns_query_ptr(srv, prt, 3000, 20, &results);
    if (err) {
        log_e("Query Failed");
        return 0;
    }
    if (!results) {
        log_w("No results found!");
        return 0;
    }

    mdns_result_t *r = results;
    int i = 0;
    while (r) {
        i++;
        r = r->next;
    }
    return i;
}

mdns_result_t *mDNS::_getResult(int idx) {
    mdns_result_t *result = results;
    int i = 0;
    while (result) {
        if (i == idx) {
            break;
        }
        i++;
        result = result->next;
    }
    return result;
}

mdns_txt_item_t *mDNS::_getResultTxt(int idx, int txtIdx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return nullptr;
    }
    if (txtIdx >= result->txt_count) return nullptr;
    return &result->txt[txtIdx];
}

String mDNS::hostname(int idx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return String();
    }
    return String(result->hostname);
}

IPAddress mDNS::IP(int idx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return IPAddress();
    }
    mdns_ip_addr_t *addr = result->addr;
    while (addr) {
        if (addr->addr.type == MDNS_IP_PROTOCOL_V4) {
            return IPAddress(addr->addr.u_addr.ip4.addr);
        }
        addr = addr->next;
    }
    return IPAddress();
}

IPv6Address mDNS::IPv6(int idx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return IPv6Address();
    }
    mdns_ip_addr_t *addr = result->addr;
    while (addr) {
        if (addr->addr.type == MDNS_IP_PROTOCOL_V6) {
            return IPv6Address(addr->addr.u_addr.ip6.addr);
        }
        addr = addr->next;
    }
    return IPv6Address();
}

uint16_t mDNS::port(int idx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return 0;
    }
    return result->port;
}

int mDNS::numTxt(int idx) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return 0;
    }
    return result->txt_count;
}

bool mDNS::hasTxt(int idx, const char *key) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return false;
    }
    int i = 0;
    while (i < result->txt_count) {
        if (strcmp(result->txt[i].key, key) == 0) return true;
        i++;
    }
    return false;
}

String mDNS::txt(int idx, const char *key) {
    mdns_result_t *result = _getResult(idx);
    if (!result) {
        log_e("Result %d not found", idx);
        return "";
    }
    int i = 0;
    while (i < result->txt_count) {
        if (strcmp(result->txt[i].key, key) == 0) return result->txt[i].value;
        i++;
    }
    return "";
}

String mDNS::txt(int idx, int txtIdx) {
    mdns_txt_item_t *resultTxt = _getResultTxt(idx, txtIdx);
    return !resultTxt
           ? ""
           : resultTxt->value;
}

String mDNS::txtKey(int idx, int txtIdx) {
    mdns_txt_item_t *resultTxt = _getResultTxt(idx, txtIdx);
    return !resultTxt
           ? ""
           : resultTxt->key;
}

} // FEmbed