# Graph Report - uni-usb-dongle  (2026-11-25)

## Corpus Check
- 105 files · ~101,320 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 16 file(s) not represented in the graph (top: (none) 8, .bin 3, .ota 3)

## Summary
- 1298 nodes · 3085 edges · 71 communities (54 shown, 17 thin omitted)
- Extraction: 87% EXTRACTED · 13% INFERRED · 0% AMBIGUOUS · INFERRED: 389 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `b81de52a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- a2dp_source.c
- app.mjs
- dongle_catalog_init
- windows.ps1
- bluetooth.c
- usb_hid_transport.c
- struct
- usb_gamepad_check.py
- a2dp_encoder.c
- disconnected
- usb_hid_check.py
- usbd_hid_get_report
- usb_audio_check.py
- with
- dongle_usb_ota_decode
- output_profile_test.c
- with
- dongle_usb_ota_service_start
- usb_gamepad_backend_test.c
- string
- test_repo_config.py
- dongle_usb_profile_config
- deploy-pages.yml
- bl616_pt_check.py
- HidPort
- find_peer
- steps
- UsbAudioCheckTests
- gamepad_router_test.c
- dongle_usb_gamepad_attach
- usb_ota_service.c
- settings_store.c
- build_web_pages.mjs
- publish-nightly
- deepseek_runner.py
- main
- force
- with
- outputs
- steps
- env
- firmware-build
- host-tests
- usb_gamepad.c
- dongle_settings_decode
- hfp_ag.c
- usb_audio.c
- FakeDevice
- build_request_payload
- hfp_ag_sync_mic_state
- bluetooth_storage.c
- publish-nightly
- radio_task
- _FakeHttpResponse
- main
- test_m62_flash_plan.py
- argparse
- dongle_bluetooth_start
- string
- build_arg_parser

## God Nodes (most connected - your core abstractions)
1. `usb_ota_task()` - 41 edges
2. `radio_task()` - 33 edges
3. `_request()` - 25 edges
4. `xTaskGetTickCount()` - 22 edges
5. `dongle_a2dp_source_t` - 21 edges
6. `main()` - 20 edges
7. `main()` - 19 edges
8. `refreshAll()` - 19 edges
9. `wireEvents()` - 19 edges
10. `HidPort` - 18 edges

## Surprising Connections (you probably didn't know these)
- `a2dp_open_media_cb()` --calls--> `dongle_a2dp_source_media_connected()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `a2dp_sig_recv()` --calls--> `dongle_a2dp_source_recv_signal()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `a2dp_media_connected()` --calls--> `dongle_a2dp_source_media_connected()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/a2dp_source.c
- `ds_now_ticks()` --calls--> `xTaskGetTickCount()`  [INFERRED]
  firmware/src/bluetooth.c → tests/usb_gamepad_backend_test.c
- `is_host_addr_blocked()` --calls--> `dongle_blocklist_contains()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/device_blocklist.c

## Import Cycles
- None detected.

## Communities (71 total, 17 thin omitted)

### Community 0 - "a2dp_source.c"
Cohesion: 0.12
Nodes (39): a2dp_on_state_cb(), a2dp_sig_connected(), dongle_a2dp_remote_sep_t, dongle_a2dp_source_callbacks_t, dongle_a2dp_source_t, dongle_a2dp_stream_state_t, build_codec_config(), classify_codec() (+31 more)

### Community 1 - "app.mjs"
Cohesion: 0.05
Nodes (96): Device, feature(), activateTab(), appendLog(), applyTheme(), capabilityNames(), connectInteractive(), disconnectClient() (+88 more)

### Community 2 - "dongle_catalog_init"
Cohesion: 0.40
Nodes (10): dongle_catalog_init(), dongle_catalog_pair_controller(), dongle_catalog_remove_controller(), dongle_catalog_select_controller(), main(), test_duplicate_pairing_and_full_catalog_do_not_mutate_slots(), test_invalid_arguments_and_addresses_are_rejected(), test_remove_and_select_validate_slots() (+2 more)

### Community 3 - "windows.ps1"
Cohesion: 0.14
Nodes (41): ConvertFrom-JUnitXml(), ConvertTo-NormalizedPathForCompare(), Find-BouffaloSdkBase(), Find-CMakeTools(), Find-CommandExe(), Find-FirmwareTools(), Find-FirstExistingFile(), Find-HostCompilerInfo() (+33 more)

### Community 4 - "bluetooth.c"
Cohesion: 0.05
Nodes (15): a2dp_media_connected(), a2dp_open_media_cb(), a2dp_send_l2cap(), a2dp_sig_recv(), a2dp_sig_send_cb(), connected_controllers_locked(), dongle_bluetooth_connected_devices(), dongle_bluetooth_disconnect_address() (+7 more)

### Community 5 - "usb_hid_transport.c"
Cohesion: 0.21
Nodes (15): dongle_usb_hid_get_feature(), dongle_usb_hid_reply(), dongle_usb_hid_reply_seen(), dongle_usb_hid_reset(), dongle_usb_hid_set_feature(), dongle_usb_hid_take(), lock(), unlock() (+7 more)

### Community 6 - "struct"
Cohesion: 0.15
Nodes (13): FakeDevice, make_test_image(), UsbOtaTests, decode_feature(), decode_frame(), encode_frame(), main(), probe() (+5 more)

### Community 7 - "usb_gamepad_check.py"
Cohesion: 0.08
Nodes (17): Bl616PtCheckTests, _build_bfpt_table(), _build_dual_8k_dump(), UsbGamepadCheckTests, check_gamepads(), wait_for(), check_gamepads_windows(), arm_read() (+9 more)

### Community 8 - "a2dp_encoder.c"
Cohesion: 0.13
Nodes (19): dongle_a2dp_encoder_close(), dongle_a2dp_encoder_encode(), dongle_a2dp_encoder_frame_samples(), dongle_a2dp_encoder_open(), audio_task(), bw_write_bits(), bw_write_bytes(), dongle_a2dp_packetize_aac_latm() (+11 more)

### Community 9 - "disconnected"
Cohesion: 0.22
Nodes (14): dongle_bluetooth_dualsense_feature(), dongle_bluetooth_dualsense_metrics(), dongle_bluetooth_send_dualsense_feature(), dongle_bluetooth_send_dualsense_output(), dongle_bluetooth_send_dualsense_output_ex(), ds_lock(), ds_now_ticks(), ds_unlock() (+6 more)

### Community 10 - "usb_hid_check.py"
Cohesion: 0.12
Nodes (26): HidCheckTests, _request(), block_device(), decode_blocklist(), decode_dualsense_metrics(), decode_mic_state(), decode_profile_state(), find_windows_hid_devices() (+18 more)

### Community 11 - "usbd_hid_get_report"
Cohesion: 0.20
Nodes (15): dongle_bluetooth_local_address(), active_gamepad_count(), gamepad_out_complete(), put_le16(), slot_address(), usbd_hid_get_idle(), usbd_hid_get_report(), usbd_hid_set_idle() (+7 more)

### Community 12 - "usb_audio_check.py"
Cohesion: 0.17
Nodes (13): analyze_pcm16_mono(), analyze_sbc_rtp_packet(), _dongle_vid_pid_tags(), _find_windows_wave_device(), find_windows_wavein_device(), find_windows_waveout_device(), main(), pcm_device() (+5 more)

### Community 13 - "with"
Cohesion: 0.06
Nodes (37): concurrency, cancel-in-progress, group, name, runs-on, steps, name, runs-on (+29 more)

### Community 14 - "dongle_usb_ota_decode"
Cohesion: 0.26
Nodes (12): crc32_update(), dongle_usb_ota_decode(), dongle_usb_ota_encode(), frame_checksum(), read_u16_le(), read_u32_le(), write_u16_le(), write_u32_le() (+4 more)

### Community 15 - "output_profile_test.c"
Cohesion: 0.23
Nodes (17): axis12(), dongle_output_encode(), dongle_switch_reply(), le16(), stick(), stick12(), main(), read_le16_signed() (+9 more)

### Community 16 - "with"
Cohesion: 0.06
Nodes (37): GH_TOKEN, name, runs-on, steps, name, runs-on, steps, jobs (+29 more)

### Community 17 - "dongle_usb_ota_service_start"
Cohesion: 0.15
Nodes (21): dongle_bluetooth_bind_blocklist(), dongle_usb_ota_service_start(), load_blocklist(), load_prefs(), dongle_codec_crc32(), dongle_codec_read_le32(), dongle_codec_write_le32(), blocklist_is_valid() (+13 more)

### Community 18 - "usb_gamepad_backend_test.c"
Cohesion: 0.14
Nodes (8): assert_fifo_exclusivity(), complete_both(), deliver_out(), dongle_usb_profile(), dongle_usb_profile_gamepad_count(), dongle_usb_profile_gamepad_out_ep(), dongle_usb_profile_mgmt_interface(), poll_at()

### Community 20 - "string"
Cohesion: 0.16
Nodes (24): crc_byte(), dongle_dualsense_bt_to_usb_input(), dongle_dualsense_bt_unlock_leds(), dongle_dualsense_crc32(), dongle_dualsense_from_switch_rumble(), dongle_dualsense_from_xinput_output(), dongle_dualsense_input(), dongle_dualsense_merge_usb_output() (+16 more)

### Community 21 - "test_repo_config.py"
Cohesion: 0.22
Nodes (3): _parse_jobs(), _read(), RepoConfigTests

### Community 22 - "dongle_usb_profile_config"
Cohesion: 0.29
Nodes (8): dongle_usb_gamepad_generic_descriptor(), usb_ota_config_descriptor_callback(), usb_ota_other_speed_descriptor_callback(), dongle_usb_profile_config(), dongle_usb_profile_gamepad_count(), dongle_usb_profile_gamepad_out_ep(), dongle_usb_profile_report_descriptor(), dongle_usb_profile_report_descriptor()

### Community 23 - "deploy-pages.yml"
Cohesion: 0.07
Nodes (31): runs-on, steps, concurrency, cancel-in-progress, group, environment, needs, runs-on (+23 more)

### Community 24 - "bl616_pt_check.py"
Cohesion: 0.24
Nodes (8): _decode_name(), _extract_active_fw(), format_report(), _infer_base_offset(), inspect_dump(), main(), _parse_int_auto(), parse_table()

### Community 26 - "find_peer"
Cohesion: 0.18
Nodes (12): a2dp_media_disconnected(), a2dp_on_error_cb(), a2dp_sig_disconnected(), accept_control(), accept_input(), audio_failure(), avdtp_l2cap_accept(), find_peer() (+4 more)

### Community 27 - "steps"
Cohesion: 0.08
Nodes (23): default, description, required, description, inputs, board, out-dir, name (+15 more)

### Community 31 - "gamepad_router_test.c"
Cohesion: 0.24
Nodes (6): dongle_gamepad_router_forward(), main(), pair_two_controllers(), test_invalid_arguments_and_rejected_output_are_reported(), test_reports_route_to_stable_indexes_independent_of_selection(), test_unpaired_or_misconfigured_slots_are_not_forwarded()

### Community 32 - "dongle_usb_gamepad_attach"
Cohesion: 0.20
Nodes (11): main(), dongle_usb_audio_attach(), dongle_usb_audio_register(), dongle_usb_gamepad_attach(), dongle_usb_gamepad_register(), attach_usb_stack(), dongle_usb_profile_msos(), usbd_add_endpoint() (+3 more)

### Community 33 - "usb_ota_service.c"
Cohesion: 0.05
Nodes (46): dongle_bluetooth_active_audio_codec(), dongle_bluetooth_command(), dongle_bluetooth_connected(), dongle_bluetooth_set_audio_codec(), dongle_status_led_set_ota(), dongle_status_led_start(), set_rgb_channel_enabled(), set_rgb_mask() (+38 more)

### Community 34 - "settings_store.c"
Cohesion: 0.21
Nodes (8): dongle_app_task(), dongle_settings_store_close(), dongle_settings_store_init(), dongle_settings_store_load(), dongle_settings_store_save(), partition_state(), store_give(), store_take()

### Community 35 - "build_web_pages.mjs"
Cohesion: 0.16
Nodes (11): buildWebPages(), DEFAULT_OUT_DIR, DEFAULT_SOURCE_DIR, isInsideDir(), isSamePath(), REPO_ROOT, rewriteHtmlRefs(), rewriteJsRefs() (+3 more)

### Community 36 - "publish-nightly"
Cohesion: 0.25
Nodes (7): concurrency, cancel-in-progress, group, jobs, name, permissions, contents

### Community 37 - "deepseek_runner.py"
Cohesion: 0.22
Nodes (5): capture_git_diff(), _forbidden_input_reason(), _format_numbered_file(), _read_text_input(), _resolve_git_executable()

### Community 38 - "main"
Cohesion: 0.21
Nodes (5): DeepSeekRunnerTests, build_user_prompt(), load_dotenv_values(), main(), parse_review_verdict()

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

### Community 44 - "env"
Cohesion: 0.40
Nodes (5): GH_TOKEN, HEAD_SHA, PREV_SHA, SHORT_SHA, env

### Community 45 - "firmware-build"
Cohesion: 0.40
Nodes (5): if, name, needs, runs-on, firmware-build

### Community 46 - "host-tests"
Cohesion: 0.40
Nodes (5): if, name, needs, runs-on, host-tests

### Community 47 - "usb_gamepad.c"
Cohesion: 0.22
Nodes (6): encode_gamepad_report(), fifo_is_empty(), gamepad_complete(), gamepad_task(), out_fifo_bytes(), put_le32()

### Community 48 - "dongle_settings_decode"
Cohesion: 0.33
Nodes (9): catalog_is_valid(), dongle_settings_decode(), dongle_settings_encode(), read_crc32(), settings_crc32(), write_crc32(), main(), test_corrupt_data_is_rejected_without_changing_output() (+1 more)

### Community 50 - "hfp_ag.c"
Cohesion: 0.41
Nodes (7): dongle_hfp_ag_feed(), dongle_hfp_ag_request_codec(), emit_str(), handle_line(), parse_codec_list(), parse_uint(), starts_with_ci()

### Community 51 - "usb_audio.c"
Cohesion: 0.05
Nodes (46): dongle_bluetooth_audio_metrics(), dongle_bluetooth_set_usb_sample_rate(), dongle_mic_pipeline_desired_ds_slot(), dongle_mic_pipeline_desired_hfp_sco(), dongle_mic_pipeline_feed_dualsense_opus(), dongle_mic_pipeline_init(), dongle_mic_pipeline_metrics(), dongle_mic_pipeline_set_source() (+38 more)

### Community 52 - "FakeDevice"
Cohesion: 0.31
Nodes (3): FakeDevice, make_state(), UsbDeviceCheckTests

### Community 53 - "build_request_payload"
Cohesion: 0.17
Nodes (5): build_request_payload(), invoke_deepseek_api(), normalize_reasoning_effort(), _redact_secret(), role_system_prompt()

### Community 54 - "hfp_ag_sync_mic_state"
Cohesion: 0.32
Nodes (8): hfp_ag_reset_state(), hfp_ag_sync_mic_state(), hfp_dlc_connected_cb(), hfp_dlc_disconnected_cb(), hfp_dlc_recv_cb(), hfp_rfcomm_tx(), dongle_mic_pipeline_set_hfp_state(), dongle_hfp_ag_init()

### Community 55 - "bluetooth_storage.c"
Cohesion: 0.27
Nodes (6): dongle_bluetooth_storage_bind(), path_for(), __wrap_easyflash_init(), __wrap_ef_del_env(), __wrap_ef_get_env_blob_offset(), __wrap_ef_set_env_blob()

### Community 56 - "publish-nightly"
Cohesion: 0.40
Nodes (5): publish-nightly, if, name, needs, runs-on

### Community 57 - "radio_task"
Cohesion: 0.12
Nodes (18): avrcp_chain(), channel_peer(), configure_qos(), control_connected(), ds_send_l2cap_report(), input_connected(), keep_central(), manage_role() (+10 more)

### Community 61 - "main"
Cohesion: 0.25
Nodes (11): channel_disconnected(), disconnected(), dongle_mic_pipeline_set_ds_connected(), dongle_usb_gamepad_diagnostics(), dongle_usb_gamepad_dualsense_clear(), dongle_usb_gamepad_dualsense_raw(), dongle_usb_gamepad_event(), dongle_usb_gamepad_sink() (+3 more)

### Community 63 - "argparse"
Cohesion: 0.16
Nodes (12): UartMonitorTest, address(), format_address(), main(), radio_state(), main(), open_uart(), check_device() (+4 more)

### Community 64 - "dongle_bluetooth_start"
Cohesion: 0.24
Nodes (7): address_out(), connected(), dongle_bluetooth_remove_audio(), dongle_bluetooth_start(), inquiry_complete(), is_host_addr_blocked(), pairing_confirm()

## Knowledge Gaps
- **17 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `usb_ota_task()` connect `usb_ota_service.c` to `dongle_bluetooth_start`, `dongle_usb_gamepad_attach`, `settings_store.c`, `dongle_catalog_init`, `bluetooth.c`, `usb_hid_transport.c`, `disconnected`, `usbd_hid_get_report`, `dongle_usb_ota_decode`, `dongle_usb_ota_service_start`, `usb_audio.c`, `main`?**
  _High betweenness centrality (0.017) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `usb_ota_task()` (e.g. with `dongle_bluetooth_audio_metrics()` and `dongle_bluetooth_command()`) actually correct?**
  _`usb_ota_task()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **Should `a2dp_source.c` be split into smaller, more focused modules?**
  _Cohesion score 0.12173913043478261 - nodes in this community are weakly interconnected._
- **Why does `radio_task()` connect `radio_task` to `dongle_bluetooth_start`, `a2dp_source.c`, `usb_ota_service.c`, `bluetooth.c`, `disconnected`, `hfp_ag.c`, `usb_audio.c`, `string`, `hfp_ag_sync_mic_state`, `find_peer`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Are the 18 inferred relationships involving `radio_task()` (e.g. with `dongle_mic_pipeline_desired_ds_slot()` and `dongle_mic_pipeline_desired_hfp_sco()`) actually correct?**
  _`radio_task()` has 18 INFERRED edges - model-reasoned connections that need verification._
- **Should `app.mjs` be split into smaller, more focused modules?**
  _Cohesion score 0.05042016806722689 - nodes in this community are weakly interconnected._
- **Why does `encode_gamepad_report()` connect `usb_gamepad.c` to `usbd_hid_get_report`, `output_profile_test.c`?**
  _High betweenness centrality (0.007) - this node is a cross-community bridge._