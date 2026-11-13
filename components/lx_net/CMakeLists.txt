idf_component_register(SRCS "lx_net.c" "lx_c6.c" INCLUDE_DIRS "include"
                       PRIV_REQUIRES bsp esp_wifi esp_netif esp_event nvs_flash lwip esp_timer espressif__esp_hosted)

# The esp_hosted version this board's launcher speaks (each board pins its own): a C6 image must match it
idf_component_get_property(hosted_dir espressif__esp_hosted COMPONENT_DIR)
if(CONFIG_LX_NET_DISABLED)
    # esp_hosted 2.12.13 starts SDIO/RPC tasks in a global constructor, even
    # when no Wi-Fi API is called. An offline board must not initialize that
    # transport (or its reset pin) before the BSP has configured its rails.
    # Drop only its automatic init/deinit unit; retain the API implementation
    # needed by the launcher's compiled, but disabled, networking services.
    idf_component_get_property(hosted_lib espressif__esp_hosted COMPONENT_LIB)
    get_target_property(hosted_sources ${hosted_lib} SOURCES)
    list(FILTER hosted_sources EXCLUDE REGEX "port_esp_hosted_host_init[.]c$")
    set_property(TARGET ${hosted_lib} PROPERTY SOURCES "${hosted_sources}")
endif()
file(STRINGS "${hosted_dir}/idf_component.yml" hosted_line REGEX "^version:")
string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" hosted_ver "${hosted_line}")
string(REGEX MATCHALL "[0-9]+" hosted_parts "${hosted_ver}")
list(GET hosted_parts 0 hosted_major)
list(GET hosted_parts 1 hosted_minor)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LX_HOSTED_VERSION="${hosted_ver}"
                           LX_HOSTED_MAJOR=${hosted_major} LX_HOSTED_MINOR=${hosted_minor})
