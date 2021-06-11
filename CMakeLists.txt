idf_build_get_property(components_to_build BUILD_COMPONENTS)

set(src)
set(include)
set(priv_requires)

set(srcs    "src/BluFi.cpp"
            "src/ArduinoNvs.cpp"
            "src/HttpsOTAUpdate.cpp"
            "src/Update.cpp"
            )
set(include "src" 
            )

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ${include}
                       REQUIRES "FEmbed-Arduino FEmbed-OS FEmbed-WiFi FEmbed-BLE nvs_flash wifi_provisioning esp_http_client esp_https_ota app_update"
                       PRIV_REQUIRES ${priv_requires})