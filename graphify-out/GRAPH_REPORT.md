# Graph Report - uni-usb-dongle  (2026-11-26)

## Corpus Check
- 107 files · ~107,761 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 16 file(s) not represented in the graph (top: (none) 8, .bin 3, .ota 3)

## Summary
- 1405 nodes · 3342 edges · 81 communities (59 shown, 22 thin omitted)
- Extraction: 86% EXTRACTED · 14% INFERRED · 0% AMBIGUOUS · INFERRED: 464 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `21e39a98`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- a2dp_source.c
- protocol.mjs
- find_peer
- windows.ps1
- bluetooth.c
- usb_hid_transport.c
- struct
- usb_gamepad_check.py
- usb_audio.c
- main
- usb_hid_check.py
- usb_gamepad.c
- usb_audio_check.py
- with
- run_dualsense
- output_profile_test.c
- with
- settings_store.c
- usb_gamepad_backend_test.c
- mic_pipeline.c
- app.mjs
- usb_profiles.c
- deploy-pages.yml
- bl616_pt_check.py
- HidPort
- dongle_usb_gamepad_attach
- steps
- UsbAudioCheckTests
- dongle_usb_ota_decode
- status_led.c
- hfp_ag.c
- build_web_pages.mjs
- publish-nightly
- deepseek_runner.py
- force
- with
- outputs
- steps
- hid-protocol.mjs
- env
- firmware-build
- host-tests
- run_sink
- refreshAll
- dualsense.c
- SerialProtocol
- dongle_bluetooth_start
- dongle_catalog_init
- dongle_settings_decode
- publish-nightly
- radio_task
- suspend_other_tasks_for_ota
- usb_ota_service.c
- uart_monitor.py
- argparse
- dongle_bluetooth_start
- dongle_usb_gamepad_event
- pathlib
- gamepad_router_test.c
- usbd_hid_set_report
- make_handlers
- FakeDevice
- Bl616PtCheckTests

## God Nodes (most connected - your core abstractions)
1. `usb_ota_task()` - 41 edges
2. `radio_task()` - 35 edges
3. `_request()` - 25 edges
4. `xTaskGetTickCount()` - 23 edges
5. `dongle_a2dp_source_t` - 21 edges
6. `main()` - 20 edges
7. `refreshAll()` - 19 edges
8. `wireEvents()` - 19 edges
9. `main()` - 19 edges
10. `HidPort` - 18 edges

## Surprising Connections (you probably didn't know these)
- `a2dp_open_media_cb()` --calls--> `dongle_a2dp_source_media_connected()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `a2dp_sig_recv()` --calls--> `dongle_a2dp_source_recv_signal()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `dongle_usb_gamepad_attach()` --calls--> `usbd_hid_init_intf()`  [INFERRED]
  firmware/src/usb_gamepad.c → tests/usb_gamepad_backend_test.c
- `radio_task()` --calls--> `dongle_a2dp_source_connect()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `dongle_bluetooth_start()` --calls--> `dongle_a2dp_source_init()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c

## Import Cycles
- None detected.

## Communities (81 total, 22 thin omitted)

### Community 0 - "a2dp_source.c"
Cohesion: 0.12
Nodes (39): a2dp_on_state_cb(), a2dp_sig_connected(), dongle_a2dp_remote_sep_t, dongle_a2dp_source_callbacks_t, dongle_a2dp_source_t, dongle_a2dp_stream_state_t, build_codec_config(), classify_codec() (+31 more)

### Community 1 - "protocol.mjs"
Cohesion: 0.13
Nodes (28): capabilityNames(), handleApplyMicSource(), refreshMetricsAndDiag(), renderAudioAndMic(), renderOverviewAndProfile(), requestOptional(), AUDIO_CODEC, CAPABILITY (+20 more)

### Community 2 - "find_peer"
Cohesion: 0.16
Nodes (13): a2dp_media_connected(), a2dp_media_disconnected(), a2dp_on_error_cb(), a2dp_sig_disconnected(), accept_control(), accept_input(), audio_failure(), avdtp_l2cap_accept() (+5 more)

### Community 3 - "windows.ps1"
Cohesion: 0.14
Nodes (41): ConvertFrom-JUnitXml(), ConvertTo-NormalizedPathForCompare(), Find-BouffaloSdkBase(), Find-CMakeTools(), Find-CommandExe(), Find-FirmwareTools(), Find-FirstExistingFile(), Find-HostCompilerInfo() (+33 more)

### Community 4 - "bluetooth.c"
Cohesion: 0.05
Nodes (18): a2dp_open_media_cb(), a2dp_send_l2cap(), a2dp_sig_recv(), a2dp_sig_send_cb(), configure_packet_type(), configure_qos(), configure_supervision_timeout(), dongle_bluetooth_disconnect_address() (+10 more)

### Community 5 - "usb_hid_transport.c"
Cohesion: 0.25
Nodes (15): dongle_usb_hid_get_feature(), dongle_usb_hid_reply(), dongle_usb_hid_reply_seen(), dongle_usb_hid_reset(), dongle_usb_hid_set_feature(), dongle_usb_hid_take(), lock(), unlock() (+7 more)

### Community 6 - "struct"
Cohesion: 0.15
Nodes (13): FakeDevice, make_test_image(), UsbOtaTests, decode_feature(), decode_frame(), encode_frame(), main(), probe() (+5 more)

### Community 7 - "usb_gamepad_check.py"
Cohesion: 0.12
Nodes (14): UsbGamepadCheckTests, check_gamepads(), wait_for(), check_gamepads_windows(), arm_read(), wait_for(), check_tick_rate(), find_windows_gamepads() (+6 more)

### Community 8 - "usb_audio.c"
Cohesion: 0.05
Nodes (47): dongle_a2dp_encoder_close(), dongle_a2dp_encoder_encode(), dongle_a2dp_encoder_frame_samples(), dongle_a2dp_encoder_on_tx_congestion(), dongle_a2dp_encoder_open(), dongle_a2dp_encoder_set_controller_active(), dongle_bluetooth_audio_metrics(), dongle_bluetooth_set_usb_sample_rate() (+39 more)

### Community 9 - "main"
Cohesion: 0.24
Nodes (14): a2dp_on_controller_coex_changed(), channel_disconnected(), channel_peer(), connected_controllers_locked(), control_connected(), disconnected(), dongle_bluetooth_connected(), dongle_bluetooth_connected_devices() (+6 more)

### Community 10 - "usb_hid_check.py"
Cohesion: 0.12
Nodes (26): HidCheckTests, _request(), block_device(), decode_blocklist(), decode_dualsense_metrics(), decode_mic_state(), decode_profile_state(), feature_fragments() (+18 more)

### Community 11 - "usb_gamepad.c"
Cohesion: 0.11
Nodes (22): dongle_bluetooth_local_address(), dongle_mic_pipeline_set_source(), active_gamepad_count(), dongle_usb_gamepad_dualsense_raw(), dongle_usb_gamepad_submit_dualsense(), dongle_usb_gamepad_test(), ds_poll_min_gap_ticks(), ds_sensor_ts_step() (+14 more)

### Community 12 - "usb_audio_check.py"
Cohesion: 0.17
Nodes (13): analyze_pcm16_mono(), analyze_sbc_rtp_packet(), _dongle_vid_pid_tags(), _find_windows_wave_device(), find_windows_wavein_device(), find_windows_waveout_device(), main(), pcm_device() (+5 more)

### Community 13 - "with"
Cohesion: 0.06
Nodes (37): concurrency, cancel-in-progress, group, name, runs-on, steps, name, runs-on (+29 more)

### Community 14 - "run_dualsense"
Cohesion: 0.11
Nodes (17): build_feature_report(), build_input_report_31(), ds_crc32(), DualSenseTelemetry, main(), make_hid_sdp_records(), parse_args(), run_dualsense() (+9 more)

### Community 15 - "output_profile_test.c"
Cohesion: 0.23
Nodes (17): axis12(), dongle_output_encode(), dongle_switch_reply(), le16(), stick(), stick12(), main(), read_le16_signed() (+9 more)

### Community 16 - "with"
Cohesion: 0.06
Nodes (37): GH_TOKEN, name, runs-on, steps, name, runs-on, steps, jobs (+29 more)

### Community 17 - "settings_store.c"
Cohesion: 0.21
Nodes (8): dongle_app_task(), dongle_settings_store_close(), dongle_settings_store_init(), dongle_settings_store_load(), dongle_settings_store_save(), partition_state(), store_give(), store_take()

### Community 18 - "usb_gamepad_backend_test.c"
Cohesion: 0.10
Nodes (13): gamepad_complete(), assert_fifo_exclusivity(), complete_both(), deliver_out(), dongle_usb_profile(), dongle_usb_profile_dualsense_poll_rate(), dongle_usb_profile_gamepad_count(), dongle_usb_profile_gamepad_out_ep() (+5 more)

### Community 20 - "mic_pipeline.c"
Cohesion: 0.13
Nodes (19): dongle_mic_pipeline_desired_ds_slot(), dongle_mic_pipeline_desired_hfp_sco(), dongle_mic_pipeline_feed_dualsense_opus(), dongle_mic_pipeline_init(), dongle_mic_pipeline_metrics(), drain_opus_ring(), drain_sco_ring(), feed_sco_msbc_chunk() (+11 more)

### Community 21 - "app.mjs"
Cohesion: 0.13
Nodes (23): activateTab(), applyTheme(), connectInteractive(), discoveryEntries, elements, handleApplyProfile(), initTabs(), initTheme() (+15 more)

### Community 22 - "usb_profiles.c"
Cohesion: 0.13
Nodes (21): dongle_bluetooth_active_audio_codec(), dongle_bluetooth_set_audio_codec(), dongle_usb_gamepad_generic_descriptor(), usb_ota_config_descriptor_callback(), usb_ota_device_descriptor_callback(), usb_ota_other_speed_descriptor_callback(), apply_device_identity(), dongle_usb_profile_activate() (+13 more)

### Community 23 - "deploy-pages.yml"
Cohesion: 0.07
Nodes (31): runs-on, steps, concurrency, cancel-in-progress, group, environment, needs, runs-on (+23 more)

### Community 24 - "bl616_pt_check.py"
Cohesion: 0.23
Nodes (8): _decode_name(), _extract_active_fw(), format_report(), _infer_base_offset(), inspect_dump(), main(), _parse_int_auto(), parse_table()

### Community 26 - "dongle_usb_gamepad_attach"
Cohesion: 0.23
Nodes (11): main(), dongle_usb_audio_attach(), dongle_usb_audio_register(), dongle_usb_gamepad_attach(), dongle_usb_gamepad_register(), attach_usb_stack(), usb_ota_string_descriptor_callback(), dongle_usb_profile() (+3 more)

### Community 27 - "steps"
Cohesion: 0.08
Nodes (23): default, description, required, description, inputs, board, out-dir, name (+15 more)

### Community 31 - "dongle_usb_ota_decode"
Cohesion: 0.26
Nodes (12): crc32_update(), dongle_usb_ota_decode(), dongle_usb_ota_encode(), frame_checksum(), read_u16_le(), read_u32_le(), write_u16_le(), write_u32_le() (+4 more)

### Community 33 - "status_led.c"
Cohesion: 0.27
Nodes (4): dongle_status_led_set_ota(), dongle_status_led_start(), set_rgb_channel_enabled(), set_rgb_mask()

### Community 34 - "hfp_ag.c"
Cohesion: 0.30
Nodes (9): dongle_hfp_ag_feed(), dongle_hfp_ag_request_codec(), emit_str(), handle_line(), parse_codec_list(), parse_uint(), starts_with_ci(), main() (+1 more)

### Community 35 - "build_web_pages.mjs"
Cohesion: 0.16
Nodes (11): buildWebPages(), DEFAULT_OUT_DIR, DEFAULT_SOURCE_DIR, isInsideDir(), isSamePath(), REPO_ROOT, rewriteHtmlRefs(), rewriteJsRefs() (+3 more)

### Community 36 - "publish-nightly"
Cohesion: 0.25
Nodes (7): concurrency, cancel-in-progress, group, jobs, name, permissions, contents

### Community 37 - "deepseek_runner.py"
Cohesion: 0.05
Nodes (21): DeepSeekRunnerTests, _fake_urlopen(), _FakeHttpResponse, _parse_jobs(), _read(), RepoConfigTests, build_arg_parser(), build_request_payload() (+13 more)

### Community 39 - "force"
Cohesion: 0.20
Nodes (10): default, description, required, type, force, on, schedule, workflow_dispatch (+2 more)

### Community 40 - "with"
Cohesion: 0.20
Nodes (10): with, board, fetch-depth, name, node-version, out-dir, path, python-version (+2 more)

### Community 41 - "outputs"
Cohesion: 0.25
Nodes (8): name, outputs, runs-on, check-changes, head_sha, prev_sha, short_sha, should_build

### Community 42 - "steps"
Cohesion: 0.43
Nodes (8): steps, steps, steps, steps, id, name, run, uses

### Community 43 - "hid-protocol.mjs"
Cohesion: 0.11
Nodes (17): Device, feature(), decodeHidReply(), delay(), encodeHidFragments(), HID_FEATURE_SIZE, HID_FILTERS, HID_FRAGMENT_SIZE (+9 more)

### Community 44 - "env"
Cohesion: 0.40
Nodes (5): GH_TOKEN, HEAD_SHA, PREV_SHA, SHORT_SHA, env

### Community 45 - "firmware-build"
Cohesion: 0.40
Nodes (5): if, name, needs, runs-on, firmware-build

### Community 46 - "host-tests"
Cohesion: 0.40
Nodes (5): if, name, needs, runs-on, host-tests

### Community 48 - "run_sink"
Cohesion: 0.19
Nodes (6): build_codec_capabilities(), main(), parse_args(), run_sink(), on_connection(), StreamTelemetry

### Community 49 - "refreshAll"
Cohesion: 0.23
Nodes (28): appendLog(), disconnectClient(), handleAbortOta(), handleBlockAddress(), handleDisconnectAddress(), handleDiscover(), handlePairDevice(), handleRemoveAudio() (+20 more)

### Community 51 - "dualsense.c"
Cohesion: 0.16
Nodes (28): receive_input(), gamepad_out_complete(), crc_byte(), dongle_dualsense_bt_feature_to_usb(), dongle_dualsense_bt_to_usb_input(), dongle_dualsense_bt_unlock_leds(), dongle_dualsense_crc32(), dongle_dualsense_feature_size() (+20 more)

### Community 53 - "dongle_bluetooth_start"
Cohesion: 0.10
Nodes (28): dongle_bluetooth_bind_blocklist(), dongle_bluetooth_storage_bind(), path_for(), __wrap_easyflash_init(), __wrap_ef_del_env(), __wrap_ef_get_env_blob_offset(), __wrap_ef_set_env_blob(), dongle_usb_ota_service_start() (+20 more)

### Community 54 - "dongle_catalog_init"
Cohesion: 0.37
Nodes (10): mutate_device_catalog(), dongle_catalog_init(), dongle_catalog_pair_controller(), dongle_catalog_remove_controller(), dongle_catalog_select_controller(), main(), test_duplicate_pairing_and_full_catalog_do_not_mutate_slots(), test_invalid_arguments_and_addresses_are_rejected() (+2 more)

### Community 55 - "dongle_settings_decode"
Cohesion: 0.33
Nodes (10): catalog_is_valid(), dongle_settings_decode(), dongle_settings_encode(), read_crc32(), settings_crc32(), write_crc32(), main(), test_corrupt_data_is_rejected_without_changing_output() (+2 more)

### Community 56 - "publish-nightly"
Cohesion: 0.40
Nodes (5): publish-nightly, if, name, needs, runs-on

### Community 57 - "radio_task"
Cohesion: 0.18
Nodes (13): ds_send_l2cap_report(), effective_a2dp_codec(), hfp_ag_reset_state(), hfp_ag_sync_mic_state(), hfp_dlc_connected_cb(), hfp_dlc_disconnected_cb(), radio_task(), release_sco_active_conn() (+5 more)

### Community 59 - "suspend_other_tasks_for_ota"
Cohesion: 0.31
Nodes (14): dongle_bluetooth_ota_is_suspended(), dongle_bluetooth_set_ota_suspended(), dongle_mic_pipeline_ota_is_suspended(), dongle_mic_pipeline_set_ota_suspended(), dongle_usb_audio_ota_is_suspended(), dongle_usb_audio_set_ota_suspended(), dongle_usb_gamepad_ota_is_suspended(), dongle_usb_gamepad_set_ota_suspended() (+6 more)

### Community 61 - "usb_ota_service.c"
Cohesion: 0.14
Nodes (24): dongle_bluetooth_ready(), dongle_usb_gamepad_diagnostics(), abort_session(), begin_update(), finish_update(), handle_device_command(), handle_device_state(), package_header_is_valid() (+16 more)

### Community 62 - "uart_monitor.py"
Cohesion: 0.27
Nodes (3): UartMonitorTest, main(), open_uart()

### Community 63 - "argparse"
Cohesion: 0.30
Nodes (9): address(), format_address(), main(), radio_state(), check_device(), ControllerState, DeviceState, main() (+1 more)

### Community 66 - "dongle_bluetooth_start"
Cohesion: 0.11
Nodes (18): address_out(), avrcp_chain(), connected(), disable_default_link_policy(), dongle_bluetooth_command(), dongle_bluetooth_remove_audio(), dongle_bluetooth_start(), inquiry_complete() (+10 more)

### Community 67 - "dongle_usb_gamepad_event"
Cohesion: 0.50
Nodes (4): dongle_usb_audio_event(), dongle_usb_gamepad_event(), notify_ota_task(), usb_ota_event_handler()

### Community 73 - "gamepad_router_test.c"
Cohesion: 0.24
Nodes (6): dongle_gamepad_router_forward(), main(), pair_two_controllers(), test_invalid_arguments_and_rejected_output_are_reported(), test_reports_route_to_stable_indexes_independent_of_selection(), test_unpaired_or_misconfigured_slots_are_not_forwarded()

### Community 75 - "usbd_hid_set_report"
Cohesion: 0.24
Nodes (12): dongle_bluetooth_dualsense_feature(), dongle_bluetooth_dualsense_metrics(), dongle_bluetooth_send_dualsense_feature(), dongle_bluetooth_send_dualsense_output(), dongle_bluetooth_send_dualsense_output_ex(), ds_lock(), ds_now_ticks(), ds_unlock() (+4 more)

### Community 76 - "make_handlers"
Cohesion: 0.20
Nodes (6): classify_configured_codec(), _disable_scan(), on_avdtp_connection(), make_handlers(), on_config(), on_start()

### Community 79 - "FakeDevice"
Cohesion: 0.31
Nodes (3): FakeDevice, make_state(), UsbDeviceCheckTests

### Community 80 - "Bl616PtCheckTests"
Cohesion: 0.43
Nodes (3): Bl616PtCheckTests, _build_bfpt_table(), _build_dual_8k_dump()

## Knowledge Gaps
- **22 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `usb_ota_task()` connect `usb_ota_service.c` to `dongle_bluetooth_start`, `bluetooth.c`, `usb_hid_transport.c`, `usb_audio.c`, `main`, `usbd_hid_set_report`, `usb_gamepad.c`, `settings_store.c`, `mic_pipeline.c`, `dongle_bluetooth_start`, `usb_profiles.c`, `dongle_catalog_init`, `dongle_usb_gamepad_attach`, `suspend_other_tasks_for_ota`, `dongle_usb_ota_decode`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `usb_ota_task()` (e.g. with `dongle_bluetooth_audio_metrics()` and `dongle_bluetooth_command()`) actually correct?**
  _`usb_ota_task()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **Should `a2dp_source.c` be split into smaller, more focused modules?**
  _Cohesion score 0.12173913043478261 - nodes in this community are weakly interconnected._
- **Why does `radio_task()` connect `radio_task` to `a2dp_source.c`, `dongle_bluetooth_start`, `find_peer`, `bluetooth.c`, `hfp_ag.c`, `usbd_hid_set_report`, `dualsense.c`, `mic_pipeline.c`, `suspend_other_tasks_for_ota`, `usb_ota_service.c`?**
  _High betweenness centrality (0.011) - this node is a cross-community bridge._
- **Are the 19 inferred relationships involving `radio_task()` (e.g. with `dongle_mic_pipeline_desired_ds_slot()` and `dongle_mic_pipeline_desired_hfp_sco()`) actually correct?**
  _`radio_task()` has 19 INFERRED edges - model-reasoned connections that need verification._
- **Should `protocol.mjs` be split into smaller, more focused modules?**
  _Cohesion score 0.12643678160919541 - nodes in this community are weakly interconnected._
- **Why does `xTaskGetTickCount()` connect `usb_ota_service.c` to `dongle_bluetooth_start`, `find_peer`, `bluetooth.c`, `main`, `usbd_hid_set_report`, `usb_gamepad.c`, `usb_gamepad_backend_test.c`, `mic_pipeline.c`, `usb_profiles.c`, `radio_task`, `suspend_other_tasks_for_ota`?**
  _High betweenness centrality (0.010) - this node is a cross-community bridge._