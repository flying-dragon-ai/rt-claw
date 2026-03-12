set(RT_CLAW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

set(CLAW_SRC_DIRS
    "${RT_CLAW_ROOT}/osal/freertos"
    "${RT_CLAW_ROOT}/src"
    "${RT_CLAW_ROOT}/src/core"
    "${RT_CLAW_ROOT}/src/services/net"
    "${RT_CLAW_ROOT}/src/services/ai"
    "${RT_CLAW_ROOT}/src/services/im"
    "${RT_CLAW_ROOT}/src/tools"
)

set(CLAW_INC_DIRS
    "${RT_CLAW_ROOT}/osal/include"
    "${RT_CLAW_ROOT}/src"
    "${RT_CLAW_ROOT}/src/core"
    "${RT_CLAW_ROOT}/src/services/swarm"
    "${RT_CLAW_ROOT}/src/services/net"
    "${RT_CLAW_ROOT}/src/services/ai"
    "${RT_CLAW_ROOT}/src/services/im"
    "${RT_CLAW_ROOT}/src/tools"
)

set(CLAW_REQUIRES
    freertos
    esp_eth
    esp_netif
    esp_event
    esp_http_client
    esp_websocket_client
    esp-tls
    json
    esp_driver_gpio
    esp_timer
    lwip
    nvs_flash
)

if(CONFIG_CLAW_SWARM_ENABLE)
    list(APPEND CLAW_SRC_DIRS "${RT_CLAW_ROOT}/src/services/swarm")
endif()

if(CONFIG_CLAW_LCD_ENABLE)
    list(APPEND CLAW_REQUIRES esp_lcd)
endif()

idf_component_register(
    SRC_DIRS        ${CLAW_SRC_DIRS}
    INCLUDE_DIRS    ${CLAW_INC_DIRS}
    REQUIRES        ${CLAW_REQUIRES}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC CLAW_PLATFORM_ESP_IDF)
