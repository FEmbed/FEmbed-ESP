idf_build_get_property(components_to_build BUILD_COMPONENTS)

set(src)
set(include)
set(priv_requires)

set(srcs    "src/WifiManager.cpp"
            )
set(include "src" 
            )

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ${include}
                       REQUIRES "FEmbed-Arduino FEmbed-OS nvs_flash"
                       PRIV_REQUIRES ${priv_requires})