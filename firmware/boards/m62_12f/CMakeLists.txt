set(M62_SDK_BOARD_DIR "${BL_SDK_BASE}/bsp/board/bl616dk")

if(NOT EXISTS "${M62_SDK_BOARD_DIR}/board.c")
    message(FATAL_ERROR "The Bouffalo SDK BL616 board startup sources are unavailable")
endif()

if(CONFIG_PSRAM)
    message(FATAL_ERROR "The M62-12F profile does not support the BL616DK PSRAM setup")
endif()

if(CONFIG_RF)
    # RF parameters are loaded by rfparam_init from factory/eFuse data. The
    # SDK board adapter supplies the shared BL616 radio control hooks.
    target_sources(app PRIVATE
        "${M62_SDK_BOARD_DIR}/board_rf.c"
        "${M62_SDK_BOARD_DIR}/wireless_regdb.c"
        "${M62_SDK_BOARD_DIR}/wireless_spur.c")
endif()

sdk_add_include_directories("${M62_SDK_BOARD_DIR}")
sdk_add_compile_definitions(-DCONFIG_BOARD_FLASH_LOW_SPEED)
sdk_add_compile_definitions(-DCONFIG_M62_STATUS_LEDS=1)
message(WARNING
    "M62 USB OTA uses the SDK A/B writer: FW slot 1 aliases mfg and may erase manufacturing data; Boot2 is not written")

target_sources(app PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src/status_led.c"
    "${M62_SDK_BOARD_DIR}/board_flash_psram.c"
    "${M62_SDK_BOARD_DIR}/board.c"
    "${M62_SDK_BOARD_DIR}/fw_header.c"
)

if(CONFIG_GCC_COMPILE_LTO)
    set_source_files_properties(
        "${M62_SDK_BOARD_DIR}/board_flash_psram.c"
        "${M62_SDK_BOARD_DIR}/board.c"
        TARGET_DIRECTORY app
        PROPERTIES COMPILE_FLAGS "-fno-lto"
    )
endif()

sdk_set_linker_script_macro("${M62_SDK_BOARD_DIR}/bl616_common.ld.in")
sdk_add_link_options(-ufw_header)

set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR "${M62_SDK_BOARD_DIR}")
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
