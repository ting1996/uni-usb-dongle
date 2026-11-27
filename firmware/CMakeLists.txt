cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(. ../include include)
sdk_add_compile_definitions(CONFIG_FAST_OTA=0)
sdk_add_compile_definitions(CONFIG_DONGLE_USB_AUDIO=1)
sdk_add_compile_definitions(CONFIG_BT_RX_BUF_COUNT=8)
sdk_add_compile_definitions(CONFIG_BT_L2CAP_TX_BUF_COUNT=12)
sdk_add_compile_definitions(CONFIG_BT_L2CAP_TX_MTU=1008)
option(DONGLE_BT_SIGNAL_TRACE "Trace bounded headphone L2CAP signaling during negotiation" OFF)
sdk_add_link_options("-Wl,--wrap=bt_l2cap_chan_send")
sdk_add_link_options("-Wl,--wrap=bt_hci_cmd_send_sync")
if(DONGLE_BT_SIGNAL_TRACE)
    target_compile_definitions(app PRIVATE DONGLE_BT_SIGNAL_TRACE=1)
    sdk_add_link_options("-Wl,--wrap=bt_l2cap_recv")
endif()
foreach(function easyflash_init ef_get_env_blob_offset ef_get_env_blob ef_set_env_blob ef_del_env)
    sdk_add_link_options("-Wl,--wrap=${function}")
endforeach()
sdk_add_include_directories(
    ${BL_SDK_BASE}/components/wireless/bluetooth/blestack/src/include
    ${BL_SDK_BASE}/components/wireless/bluetooth/blestack/src/sbc/enc
    ${BL_SDK_BASE}/components/multimedia/opus/include
    ../third_party/libldac/inc
    ../third_party/libopenaptx
    ../third_party/vo-aacenc/common/include
    ../third_party/vo-aacenc/aacenc/inc
    ../third_party/vo-aacenc/aacenc/basic_op
)
set(VO_AACENC_SOURCES
    ../third_party/vo-aacenc/common/cmnMemory.c
    ../third_party/vo-aacenc/aacenc/basic_op/basicop2.c
    ../third_party/vo-aacenc/aacenc/basic_op/oper_32b.c
    ../third_party/vo-aacenc/aacenc/src/aac_rom.c
    ../third_party/vo-aacenc/aacenc/src/aacenc.c
    ../third_party/vo-aacenc/aacenc/src/aacenc_core.c
    ../third_party/vo-aacenc/aacenc/src/adj_thr.c
    ../third_party/vo-aacenc/aacenc/src/band_nrg.c
    ../third_party/vo-aacenc/aacenc/src/bit_cnt.c
    ../third_party/vo-aacenc/aacenc/src/bitbuffer.c
    ../third_party/vo-aacenc/aacenc/src/bitenc.c
    ../third_party/vo-aacenc/aacenc/src/block_switch.c
    ../third_party/vo-aacenc/aacenc/src/channel_map.c
    ../third_party/vo-aacenc/aacenc/src/dyn_bits.c
    ../third_party/vo-aacenc/aacenc/src/grp_data.c
    ../third_party/vo-aacenc/aacenc/src/interface.c
    ../third_party/vo-aacenc/aacenc/src/line_pe.c
    ../third_party/vo-aacenc/aacenc/src/memalign.c
    ../third_party/vo-aacenc/aacenc/src/ms_stereo.c
    ../third_party/vo-aacenc/aacenc/src/pre_echo_control.c
    ../third_party/vo-aacenc/aacenc/src/psy_configuration.c
    ../third_party/vo-aacenc/aacenc/src/psy_main.c
    ../third_party/vo-aacenc/aacenc/src/qc_main.c
    ../third_party/vo-aacenc/aacenc/src/quantize.c
    ../third_party/vo-aacenc/aacenc/src/sf_estim.c
    ../third_party/vo-aacenc/aacenc/src/spreading.c
    ../third_party/vo-aacenc/aacenc/src/stat_bits.c
    ../third_party/vo-aacenc/aacenc/src/tns.c
    ../third_party/vo-aacenc/aacenc/src/transform.c
)
set(LDAC_SOURCES
    ../third_party/libldac/src/ldaclib.c
    ../third_party/libldac/src/ldacBT.c
)
set(THIRD_PARTY_CODEC_SOURCES
    ${LDAC_SOURCES}
    ../third_party/libopenaptx/openaptx.c
    ${VO_AACENC_SOURCES}
)
set_source_files_properties(${THIRD_PARTY_CODEC_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "__unused=;_32BIT_FIXED_POINT=1"
    COMPILE_OPTIONS "-O2;-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-implicit-fallthrough;-Wno-maybe-uninitialized"
)
set_source_files_properties(${LDAC_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "__unused=;_32BIT_FIXED_POINT=1;LDAC_TCM_CODE=1"
    COMPILE_OPTIONS "-O3;-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-implicit-fallthrough;-Wno-maybe-uninitialized"
)
target_sources(app PRIVATE
    ../src/core/a2dp_packetizer.c
    ../src/core/a2dp_source.c
    ../src/core/audio_stream.c
    ../src/core/device_blocklist.c
    ../src/core/device_catalog.c
    ../src/core/dualsense.c
    ../src/core/dualsense_audio.c
    ../src/core/dualsense_haptics.c
    ../src/core/dualsense_mic.c
    ../src/core/gamepad_router.c
    ../src/core/hfp_ag.c
    ../src/core/hid_transport.c
    ../src/core/output_profile.c
    ../src/core/prefs_codec.c
    ../src/core/settings_codec.c
    ../src/core/usb_ota_protocol.c
    src/settings_store.c
    src/a2dp_encoder.c
    src/bluetooth_storage.c
    src/bluetooth.c
    ${THIRD_PARTY_CODEC_SOURCES}
    src/mic_pipeline.c
    src/usb_audio.c
    src/usb_gamepad.c
    src/usb_hid_transport.c
    src/usb_ota_service.c
    src/usb_profiles.c
)
target_link_libraries(app ${BL_SDK_BASE}/components/multimedia/opus/libopus.a m)
sdk_set_main_file(main.c)

project(uni_usb_dongle)
