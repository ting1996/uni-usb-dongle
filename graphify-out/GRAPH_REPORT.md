# Graph Report - uni-usb-dongle  (2026-11-24)

## Corpus Check
- 96 files · ~93,745 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 16 file(s) not represented in the graph (top: (none) 8, .bin 3, .ota 3)

## Summary
- 1206 nodes · 2833 edges · 54 communities (43 shown, 11 thin omitted)
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 348 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `4bde21a6`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- main
- app.mjs
- string
- windows.ps1
- bluetooth.c
- usb_hid_transport.c
- struct
- usb_gamepad_check.py
- usb_audio.c
- disconnected
- usb_hid_check.py
- usb_gamepad.c
- usb_audio_check.py
- with
- output_profile_test.c
- with
- main
- usb_gamepad_backend_test.c
- string
- deepseek_runner.py
- dongle_usb_profile_config
- deploy-pages.yml
- bl616_pt_check.py
- HidPort
- steps
- UsbAudioCheckTests
- dongle_usb_gamepad_attach
- usb_ota_service.c
- Bl616PtCheckTests
- build_web_pages.mjs
- publish-nightly
- force
- with
- outputs
- steps
- env
- firmware-build
- host-tests
- settings_store.c
- sys
- mic_pipeline.c
- FakeDevice
- hfp_ag_sync_mic_state
- publish-nightly
- radio_task

## God Nodes (most connected - your core abstractions)
1. `usb_ota_task()` - 41 edges
2. `radio_task()` - 28 edges
3. `_request()` - 25 edges
4. `xTaskGetTickCount()` - 22 edges
5. `main()` - 20 edges
6. `refreshAll()` - 19 edges
7. `main()` - 19 edges
8. `HidPort` - 18 edges
9. `wireEvents()` - 18 edges
10. `main()` - 17 edges

## Surprising Connections (you probably didn't know these)
- `ds_now_ticks()` --calls--> `xTaskGetTickCount()`  [INFERRED]
  firmware/src/bluetooth.c → tests/usb_gamepad_backend_test.c
- `is_host_addr_blocked()` --calls--> `dongle_blocklist_contains()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/device_blocklist.c
- `hfp_dlc_recv_cb()` --calls--> `dongle_hfp_ag_feed()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/hfp_ag.c
- `receive_control()` --calls--> `dongle_dualsense_bt_feature_to_usb()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/dualsense.c
- `receive_input()` --calls--> `dongle_dualsense_bt_to_usb_input()`  [INFERRED]
  firmware/src/bluetooth.c → src/core/dualsense.c

## Import Cycles
- None detected.

## Communities (54 total, 11 thin omitted)

### Community 0 - "main"
Cohesion: 0.25
Nodes (8): dongle_usb_audio_event(), dongle_usb_gamepad_dualsense_raw(), dongle_usb_gamepad_event(), dongle_usb_gamepad_test(), submit(), notify_ota_task(), usb_ota_event_handler(), main()

### Community 1 - "app.mjs"
Cohesion: 0.05
Nodes (93): Device, feature(), activateTab(), appendLog(), capabilityNames(), connectInteractive(), disconnectClient(), discoveryEntries (+85 more)

### Community 2 - "string"
Cohesion: 0.07
Nodes (36): mutate_device_catalog(), dongle_catalog_init(), dongle_catalog_pair_controller(), dongle_catalog_remove_controller(), dongle_catalog_select_controller(), dongle_gamepad_router_forward(), dongle_hfp_ag_feed(), dongle_hfp_ag_request_codec() (+28 more)

### Community 3 - "windows.ps1"
Cohesion: 0.14
Nodes (41): ConvertFrom-JUnitXml(), ConvertTo-NormalizedPathForCompare(), Find-BouffaloSdkBase(), Find-CMakeTools(), Find-CommandExe(), Find-FirmwareTools(), Find-FirstExistingFile(), Find-HostCompilerInfo() (+33 more)

### Community 4 - "bluetooth.c"
Cohesion: 0.06
Nodes (20): accept_control(), accept_input(), audio_chain(), audio_discovered(), audio_started(), audio_stream(), audio_suspended(), dongle_bluetooth_disconnect_address() (+12 more)

### Community 5 - "usb_hid_transport.c"
Cohesion: 0.12
Nodes (27): dongle_usb_hid_get_feature(), dongle_usb_hid_reply(), dongle_usb_hid_reply_seen(), dongle_usb_hid_reset(), dongle_usb_hid_set_feature(), dongle_usb_hid_take(), lock(), unlock() (+19 more)

### Community 6 - "struct"
Cohesion: 0.15
Nodes (13): FakeDevice, make_test_image(), UsbOtaTests, decode_feature(), decode_frame(), encode_frame(), main(), probe() (+5 more)

### Community 7 - "usb_gamepad_check.py"
Cohesion: 0.09
Nodes (15): M62FlashPlanTests, UsbGamepadCheckTests, check_gamepads(), wait_for(), check_gamepads_windows(), arm_read(), wait_for(), check_tick_rate() (+7 more)

### Community 8 - "usb_audio.c"
Cohesion: 0.07
Nodes (32): avrcp_chain(), dongle_bluetooth_audio_metrics(), dongle_bluetooth_command(), audio_received(), audio_task(), db_to_q15_gain(), dongle_usb_audio_diagnostics(), dongle_usb_audio_is_streaming() (+24 more)

### Community 9 - "disconnected"
Cohesion: 0.33
Nodes (10): channel_disconnected(), channel_peer(), control_connected(), disconnected(), dongle_bluetooth_connected(), input_connected(), receive_input(), dongle_mic_pipeline_set_ds_connected() (+2 more)

### Community 10 - "usb_hid_check.py"
Cohesion: 0.12
Nodes (26): HidCheckTests, _request(), block_device(), decode_blocklist(), decode_dualsense_metrics(), decode_mic_state(), decode_profile_state(), find_windows_hid_devices() (+18 more)

### Community 11 - "usb_gamepad.c"
Cohesion: 0.19
Nodes (15): dongle_bluetooth_local_address(), active_gamepad_count(), encode_gamepad_report(), fifo_is_empty(), gamepad_task(), out_fifo_bytes(), put_le16(), put_le32() (+7 more)

### Community 12 - "usb_audio_check.py"
Cohesion: 0.14
Nodes (17): address(), format_address(), main(), radio_state(), analyze_pcm16_mono(), analyze_sbc_rtp_packet(), _dongle_vid_pid_tags(), _find_windows_wave_device() (+9 more)

### Community 13 - "with"
Cohesion: 0.06
Nodes (37): concurrency, cancel-in-progress, group, name, runs-on, steps, name, runs-on (+29 more)

### Community 15 - "output_profile_test.c"
Cohesion: 0.23
Nodes (17): axis12(), dongle_output_encode(), dongle_switch_reply(), le16(), stick(), stick12(), main(), read_le16_signed() (+9 more)

### Community 16 - "with"
Cohesion: 0.06
Nodes (37): GH_TOKEN, name, runs-on, steps, name, runs-on, steps, jobs (+29 more)

### Community 17 - "main"
Cohesion: 0.26
Nodes (11): dongle_bluetooth_dualsense_feature(), dongle_bluetooth_dualsense_metrics(), dongle_bluetooth_send_dualsense_feature(), dongle_bluetooth_send_dualsense_output(), dongle_bluetooth_send_dualsense_output_ex(), ds_lock(), ds_now_ticks(), ds_unlock() (+3 more)

### Community 18 - "usb_gamepad_backend_test.c"
Cohesion: 0.13
Nodes (9): gamepad_complete(), assert_fifo_exclusivity(), complete_both(), deliver_out(), dongle_usb_profile(), dongle_usb_profile_gamepad_count(), dongle_usb_profile_gamepad_out_ep(), dongle_usb_profile_mgmt_interface() (+1 more)

### Community 20 - "string"
Cohesion: 0.17
Nodes (27): gamepad_out_complete(), crc_byte(), dongle_dualsense_bt_feature_to_usb(), dongle_dualsense_bt_to_usb_input(), dongle_dualsense_bt_unlock_leds(), dongle_dualsense_crc32(), dongle_dualsense_feature_size(), dongle_dualsense_from_ps4_output() (+19 more)

### Community 21 - "deepseek_runner.py"
Cohesion: 0.05
Nodes (21): DeepSeekRunnerTests, _fake_urlopen(), _FakeHttpResponse, _parse_jobs(), _read(), RepoConfigTests, build_arg_parser(), build_request_payload() (+13 more)

### Community 22 - "dongle_usb_profile_config"
Cohesion: 0.19
Nodes (14): dongle_usb_gamepad_generic_descriptor(), usb_ota_config_descriptor_callback(), usb_ota_device_descriptor_callback(), usb_ota_other_speed_descriptor_callback(), apply_device_identity(), dongle_usb_profile_activate(), dongle_usb_profile_complete(), dongle_usb_profile_config() (+6 more)

### Community 23 - "deploy-pages.yml"
Cohesion: 0.07
Nodes (31): runs-on, steps, concurrency, cancel-in-progress, group, environment, needs, runs-on (+23 more)

### Community 24 - "bl616_pt_check.py"
Cohesion: 0.23
Nodes (8): _decode_name(), _extract_active_fw(), format_report(), _infer_base_offset(), inspect_dump(), main(), _parse_int_auto(), parse_table()

### Community 27 - "steps"
Cohesion: 0.08
Nodes (23): default, description, required, description, inputs, board, out-dir, name (+15 more)

### Community 32 - "dongle_usb_gamepad_attach"
Cohesion: 0.16
Nodes (13): main(), dongle_usb_audio_attach(), dongle_usb_audio_register(), dongle_usb_gamepad_attach(), dongle_usb_gamepad_register(), attach_usb_stack(), usb_ota_string_descriptor_callback(), dongle_usb_profile() (+5 more)

### Community 33 - "usb_ota_service.c"
Cohesion: 0.05
Nodes (52): dongle_bluetooth_connected_devices(), dongle_bluetooth_ready(), dongle_status_led_set_ota(), dongle_status_led_start(), set_rgb_mask(), status_led_task(), dongle_usb_gamepad_diagnostics(), abort_session() (+44 more)

### Community 34 - "Bl616PtCheckTests"
Cohesion: 0.43
Nodes (3): Bl616PtCheckTests, _build_bfpt_table(), _build_dual_8k_dump()

### Community 35 - "build_web_pages.mjs"
Cohesion: 0.16
Nodes (11): buildWebPages(), DEFAULT_OUT_DIR, DEFAULT_SOURCE_DIR, isInsideDir(), isSamePath(), REPO_ROOT, rewriteHtmlRefs(), rewriteJsRefs() (+3 more)

### Community 36 - "publish-nightly"
Cohesion: 0.25
Nodes (7): concurrency, cancel-in-progress, group, jobs, name, permissions, contents

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

### Community 48 - "settings_store.c"
Cohesion: 0.09
Nodes (16): dongle_app_task(), dongle_bluetooth_bind_blocklist(), dongle_bluetooth_storage_bind(), path_for(), __wrap_easyflash_init(), __wrap_ef_del_env(), __wrap_ef_get_env_blob_offset(), __wrap_ef_set_env_blob() (+8 more)

### Community 50 - "sys"
Cohesion: 0.31
Nodes (3): UartMonitorTest, main(), open_uart()

### Community 51 - "mic_pipeline.c"
Cohesion: 0.14
Nodes (18): dongle_mic_pipeline_desired_ds_slot(), dongle_mic_pipeline_desired_hfp_sco(), dongle_mic_pipeline_feed_dualsense_opus(), dongle_mic_pipeline_init(), dongle_mic_pipeline_metrics(), dongle_mic_pipeline_set_source(), drain_opus_ring(), drain_sco_ring() (+10 more)

### Community 52 - "FakeDevice"
Cohesion: 0.18
Nodes (8): FakeDevice, make_state(), UsbDeviceCheckTests, check_device(), ControllerState, DeviceState, main(), read_device_state()

### Community 54 - "hfp_ag_sync_mic_state"
Cohesion: 0.47
Nodes (6): hfp_ag_reset_state(), hfp_ag_sync_mic_state(), hfp_dlc_connected_cb(), hfp_dlc_disconnected_cb(), dongle_mic_pipeline_set_hfp_state(), dongle_hfp_ag_init()

### Community 56 - "publish-nightly"
Cohesion: 0.40
Nodes (5): publish-nightly, if, name, needs, runs-on

### Community 57 - "radio_task"
Cohesion: 0.11
Nodes (22): address_out(), audio_failure(), configure_qos(), connected(), dongle_bluetooth_remove_audio(), dongle_bluetooth_start(), ds_send_l2cap_report(), inquiry_complete() (+14 more)

## Knowledge Gaps
- **11 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `usb_ota_task()` connect `usb_ota_service.c` to `main`, `dongle_usb_gamepad_attach`, `string`, `bluetooth.c`, `usb_hid_transport.c`, `usb_audio.c`, `disconnected`, `settings_store.c`, `main`, `mic_pipeline.c`, `dongle_usb_profile_config`, `radio_task`?**
  _High betweenness centrality (0.012) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `usb_ota_task()` (e.g. with `dongle_bluetooth_audio_metrics()` and `dongle_bluetooth_command()`) actually correct?**
  _`usb_ota_task()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **Should `app.mjs` be split into smaller, more focused modules?**
  _Cohesion score 0.052166224580017684 - nodes in this community are weakly interconnected._
- **Why does `dongle_usb_ota_service_start()` connect `settings_store.c` to `dongle_usb_gamepad_attach`, `usb_ota_service.c`, `mic_pipeline.c`, `dongle_usb_profile_config`, `radio_task`?**
  _High betweenness centrality (0.007) - this node is a cross-community bridge._
- **Are the 13 inferred relationships involving `radio_task()` (e.g. with `dongle_mic_pipeline_desired_ds_slot()` and `dongle_mic_pipeline_desired_hfp_sco()`) actually correct?**
  _`radio_task()` has 13 INFERRED edges - model-reasoned connections that need verification._
- **Should `string` be split into smaller, more focused modules?**
  _Cohesion score 0.07459207459207459 - nodes in this community are weakly interconnected._
- **Why does `dongle_usb_gamepad_register()` connect `dongle_usb_gamepad_attach` to `main`, `usb_gamepad.c`?**
  _High betweenness centrality (0.005) - this node is a cross-community bridge._