cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<C_COMPILER_ID:MSVC>:$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>>"
        CACHE STRING "MSVC debug information format")
endif()
project(uni_usb_dongle VERSION 0.1.0 LANGUAGES C)

include(CTest)

add_library(dongle_core
    src/core/audio_stream.c
    src/core/device_blocklist.c
    src/core/device_catalog.c
    src/core/dualsense.c
    src/core/dualsense_mic.c
    src/core/gamepad_router.c
    src/core/hfp_ag.c
    src/core/hid_transport.c
    src/core/output_profile.c
    src/core/prefs_codec.c
    src/core/settings_codec.c
    src/core/usb_ota_protocol.c
)
target_include_directories(dongle_core PUBLIC include)
target_compile_features(dongle_core PUBLIC c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dongle_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    target_compile_options(dongle_core PUBLIC /experimental:c11atomics PRIVATE /W4)
endif()

if(BUILD_TESTING)
    add_executable(dualsense_test tests/dualsense_test.c)
    target_link_libraries(dualsense_test PRIVATE dongle_core)
    add_test(NAME dualsense COMMAND dualsense_test)
    add_executable(hid_transport_test tests/hid_transport_test.c)
    target_link_libraries(hid_transport_test PRIVATE dongle_core)
    add_test(NAME hid_transport COMMAND hid_transport_test)
    add_executable(usb_gamepad_backend_test tests/usb_gamepad_backend_test.c)
    target_include_directories(usb_gamepad_backend_test PRIVATE
        tests/usb_stubs firmware/include include)
    target_link_libraries(usb_gamepad_backend_test PRIVATE dongle_core)
    target_compile_features(usb_gamepad_backend_test PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(usb_gamepad_backend_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(usb_gamepad_backend_test PRIVATE /W4)
    endif()
    add_test(NAME usb_gamepad_backend COMMAND usb_gamepad_backend_test)

    add_executable(output_profile_test tests/output_profile_test.c)
    target_link_libraries(output_profile_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(output_profile_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(output_profile_test PRIVATE /W4)
    endif()
    add_test(NAME output_profile COMMAND output_profile_test)

    add_executable(audio_stream_test tests/audio_stream_test.c)
    target_link_libraries(audio_stream_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(audio_stream_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_stream COMMAND audio_stream_test)

    add_executable(device_catalog_test tests/device_catalog_test.c)
    target_link_libraries(device_catalog_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(device_catalog_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME device_catalog COMMAND device_catalog_test)

    add_executable(gamepad_router_test tests/gamepad_router_test.c)
    target_link_libraries(gamepad_router_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gamepad_router_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME gamepad_router COMMAND gamepad_router_test)

    add_executable(usb_ota_protocol_test tests/usb_ota_protocol_test.c)
    target_link_libraries(usb_ota_protocol_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(usb_ota_protocol_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME usb_ota_protocol COMMAND usb_ota_protocol_test)

    add_executable(settings_codec_test tests/settings_codec_test.c)
    target_link_libraries(settings_codec_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(settings_codec_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME settings_codec COMMAND settings_codec_test)

    add_executable(device_blocklist_test tests/device_blocklist_test.c)
    target_link_libraries(device_blocklist_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(device_blocklist_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(device_blocklist_test PRIVATE /W4)
    endif()
    add_test(NAME device_blocklist COMMAND device_blocklist_test)

    add_executable(prefs_codec_test tests/prefs_codec_test.c)
    target_link_libraries(prefs_codec_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(prefs_codec_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(prefs_codec_test PRIVATE /W4)
    endif()
    add_test(NAME prefs_codec COMMAND prefs_codec_test)

    add_executable(hfp_ag_test tests/hfp_ag_test.c)
    target_link_libraries(hfp_ag_test PRIVATE dongle_core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hfp_ag_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(hfp_ag_test PRIVATE /W4)
    endif()
    add_test(NAME hfp_ag COMMAND hfp_ag_test)
endif()
