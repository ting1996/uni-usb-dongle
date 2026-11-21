cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(. ../include include)
sdk_add_compile_definitions(CONFIG_FAST_OTA=0)
sdk_add_compile_definitions(CONFIG_DONGLE_USB_AUDIO=1)
sdk_add_compile_definitions(CONFIG_BT_RX_BUF_COUNT=8)
sdk_add_compile_definitions(CONFIG_BT_L2CAP_TX_BUF_COUNT=8)
option(DONGLE_BT_SIGNAL_TRACE "Trace bounded headphone L2CAP signaling during negotiation" OFF)
sdk_add_link_options("-Wl,--wrap=bt_l2cap_chan_send")
sdk_add_link_options("-Wl,--wrap=bt_avdtp_open")
sdk_add_link_options("-Wl,--wrap=bt_hci_cmd_send_sync")
sdk_add_link_options("-Wl,--wrap=SBC_Encoder_Init")
if(DONGLE_BT_SIGNAL_TRACE)
    target_compile_definitions(app PRIVATE DONGLE_BT_SIGNAL_TRACE=1)
    sdk_add_link_options("-Wl,--wrap=bt_l2cap_recv")
endif()
foreach(function easyflash_init ef_get_env_blob_offset ef_get_env_blob ef_set_env_blob ef_del_env)
    sdk_add_link_options("-Wl,--wrap=${function}")
endforeach()
sdk_add_include_directories(${BL_SDK_BASE}/components/wireless/bluetooth/blestack/src/include)
target_sources(app PRIVATE
    ../src/core/audio_stream.c
    ../src/core/device_catalog.c
    ../src/core/dualsense.c
    ../src/core/gamepad_router.c
    ../src/core/hid_transport.c
    ../src/core/output_profile.c
    ../src/core/settings_codec.c
    ../src/core/usb_ota_protocol.c
    src/settings_store.c
    src/bluetooth_storage.c
    src/bluetooth.c
    src/usb_audio.c
    src/usb_gamepad.c
    src/usb_hid_transport.c
    src/usb_ota_service.c
    src/usb_profiles.c
)
sdk_set_main_file(main.c)

project(uni_usb_dongle)
