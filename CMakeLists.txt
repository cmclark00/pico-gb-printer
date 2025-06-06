# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(pico_gb_printer)
#set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

# LWIP
set(LWIP_DIR ${PICO_SDK_PATH}/lib/lwip)
set (LWIP_INCLUDE_DIRS
    "include"
    "${LWIP_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
include(${LWIP_DIR}/src/Filelists.cmake)

# Extra stuff from TinyUSB, that is not part of tinyusb_device library
set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
set(TINYUSB_LIBNETWORKING_SOURCES
    ${PICO_TINYUSB_PATH}/lib/networking/dhserver.c
    ${PICO_TINYUSB_PATH}/lib/networking/dnserver.c
    ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
)

add_executable(${PROJECT_NAME} src/pico_pokemon_storage.c src/linkcable.c src/pokemon_data.c src/pokemon_trading.c src/datablocks.c src/tusb_lwip_glue.c src/usb_descriptors.c src/websocket_server.c src/char_encode.c ${TINYUSB_LIBNETWORKING_SOURCES})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/linkcable.pio)

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
target_include_directories(${PROJECT_NAME} PRIVATE ${LWIP_INCLUDE_DIRS} ${PICO_TINYUSB_PATH}/src ${PICO_TINYUSB_PATH}/lib/networking)
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_pio pico_unique_id tinyusb_device lwipallapps lwipcore hardware_clocks)
pico_add_extra_outputs(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)
