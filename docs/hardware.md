# Hardware and platform findings

## Confirmed

- The Ai-Thinker [M62 product page](https://docs.ai-thinker.com/en/ai_m62/)
  identifies the Ai-M62 series as BL616-based and lists BLE 5.3 and USB 2.0
  among the chip/module capabilities.
- The [Zephyr board definition](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/aithinker/ai_m62_12f_kit)
  identifies the kit as `bl616c50q2i`. Its board documentation says UART0 is
  connected to a USB serial converter and is the default console/programming
  port. Its device tree assigns UART0 TX/RX to GPIO21/GPIO22 and describes a
  4 MiB GigaDevice GD25LQ32D flash (`JEDEC ID C8 60 16`).
- A read-only UART ROM-loader session on `/dev/ttyUSB0` reported BL616
  revision A0, flash JEDEC ID `C8 60 16`, and capacity `0x00400000`. It read
  `0xE000–0xFFFF`; both partition-table copies have valid SDK header and entry
  CRCs, are byte-identical, and match the SDK BL616DK 4 MiB layout. FW slot 0
  was active at the time of this initial readback. An earlier OTA was described
  as switching to slot 1; latest post-XZ-OTA startup logs again show slot 0
  active. Inspect boot evidence rather than inferring the running slot from
  the OTA staging destination. The ROM-loader handshake succeeded, but the exact physical button
  sequence still needs to be recorded for repeatable recovery.
- A separate read of `0x0000–0xDFFF` matches the first 52,576 bytes of the
  SDK's `boot2_bl616_isp_release_v8.1.8.bin` exactly; the remaining 4,768
  bytes are erased (`0xFF`) padding. The installed Boot2 therefore matches
  that BL616 release image, but this does not validate BL616DK application
  startup on the M62.
- The Zephyr device tree's static partition map uses a boot header at `0x0000`,
  image 0 at `0x2000`, and storage at `0x200000`. This differs from the
  Bouffalo SDK BL616DK reference table, which places table copies at `0xE000`
  and `0xF000`, with A/B firmware slots and `PSM`. These are two software
  layouts; the UART readback above shows the SDK layout on the inspected unit.
- The BL616DK reference startup also uses UART0 on GPIO21/GPIO22, matching the
  Zephyr M62 pinctrl assignment. This pin match does not make the rest of the
  BL616DK board profile safe for the M62.
- The [M62-12F-Kit schematic](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Public%20Document%20Center/WiFi/ai_m62/SchematicDiagram/Ai-M62-12F-Kit_V1.0%E5%8E%9F%E7%90%86%E5%9B%BE20230210.pdf)
   has been reviewed: the board's USB connector D+/D- terminate at a CH340C
   USB-to-UART bridge, while the SoC's native USB_DP/USB_DM are separate nets
   on J3 pins 10/9, respectively. Native USB data therefore requires an
   external connection to J3; the existing connector is not a native USB
   device port.
- The [Bouffalo SDK](https://github.com/bouffalolab/bouffalo_sdk) lists USB
  and Bluetooth support for BL616C. This establishes SoC-level peripherals,
  not that the kit routes native USB to its connector or that its supplied
  Bluetooth stack supports the profiles needed here.
- The inspected Bouffalo SDK 2.3.32 tree includes CherryUSB HID, CDC-ACM, and
  UAC device classes, plus a BL616 A2DP-source example. These establish
  available software components, not working M62-12F integration or
  simultaneous Bluetooth link capacity.
- The firmware scaffold now includes an application-level OTA receiver over
  the BL616's native USB 2.0 CDC-ACM device interface. It uses the SDK's A/B
  OTA core and SHA-256 image verification, and builds with USB 2.0 high-speed
  support and speed-specific CDC descriptors. The M62 profile enables the
  service with the SDK A/B writer. Native USB CDC enumeration, OTA transfer,
  reboot, device-state response, and a subsequent no-write probe have been
  verified on the M62. The WSL USB/IP host reports the CDC device at 480 Mbps;
  two HID gamepad interfaces now pass synthetic input tests alongside CDC;
  composite audio operation remains unverified. FW slot 1 aliases `mfg`;
  OTA wrote the new app into that shared range. The owner accepted the loss,
  and the previous contents were not separately read or backed up. Boot2 is
  outside the OTA writer's range. The
  kit's CH340 USB-to-UART connector is not used by this OTA path. The Zephyr
  board definition maps the kit's active-high white, red, green, and blue LEDs
  to GPIO29, GPIO1, GPIO30, and GPIO0. The M62 profile uses white as the
  running indicator. Red/green indicate controller slots 0/1, and blue indicates
  an authenticated headphone link, each at 50% hardware PWM. A red-green-blue
  chase overrides connection status while OTA is active. LED appearance still
  needs physical validation.
- The Bouffalo SDK provides FreeRTOS, LittleFS/MTD, and UART flashing
   workflows for BL616. Its reference 4 MiB partition table includes A/B
   firmware slots and a 32 KiB `PSM` partition at `0x3E9000`. The SDK OTA
   example documents image hash validation and Boot2 rollback for this
   reference layout.
- The connected `/dev/ttyUSB0` enumerates as a QinHeng CH340 (`1a86:7523`)
  using the `ch341` driver. This verifies the USB-UART bridge only; the flash
  identity and partition table were established separately by the ROM-loader
  readback above.
- The UART read used the SDK tool's `--flash --read` mode and did not request
  erase or write. The tool identified the flash and read the table
  successfully, falling back to its default flash configuration because no
  matching config was found. It used the BL616 ROM downloader; it did not
  identify or run the installed Boot2 image.

## M62 profile and app-only flash boundary

The `firmware/boards/m62_12f` profile reuses the SDK BL616 startup, linker
script, firmware header, and matching 4 MiB partition metadata. It omits the
BL616DK board GPIO and RF calibration sources, rejects builds with PSRAM or RF
enabled, and defines `CONFIG_BOARD_FLASH_LOW_SPEED` so startup does not apply
the BL616DK flash timing calibration. UART0 GPIO21/22 are the only board pins
configured by the reused startup, and those pins match the M62 metadata.
The M62 build configures the UART0 console to 115200 baud for readable logs
through the kit's CH340 bridge. Basic startup has been verified after OTA by
successful CDC status and probe responses; LED behavior and recovery procedure
remain unvalidated.

`make -C firmware BOARD=m62_12f flash-app-m62` is the only enabled programming
target. Its config has a single payload entry: the built application at
`0x10000` in FW slot 0. `erase = 1` requests erase of only
the programmed section, and the make target rejects an image larger than
`0x200000` bytes. No Boot2, partition-table, or data-partition image is
present, so this target does not write Boot2 (`0x00000–0x0DFFF`), either
partition table (`0x0E000–0x0FFFF`), FW slot 1/`mfg` (`0x210000–0x377FFF`),
or media/PSM/KEY/DATA/factory. Generic SDK `flash` remains blocked. The
target does not read the live partition table before writing; its preservation
guarantee depends on the installed table and active slot remaining as verified.
Latest post-XZ-OTA UART startup logs show slot 0 active, superseding the older
slot-1 assumption. This target does not inspect or switch boot metadata; inspect
the live table before deliberate ROM programming and use CDC OTA for updates.

The inspected SDK table aliases FW slot 1 and `mfg` at `0x210000`. M62 USB OTA
is enabled with the SDK A/B writer: it preserves Boot2 (`0x00000–0x0DFFF`),
but writes the inactive FW slot at the aliased `mfg` address
(`0x210000–0x377FFF`). The owner confirmed the manufacturing image is
disposable, and OTA has now completed; treat the former contents as lost.
They were not independently examined or backed up. The OTA writer also updates
partition-table metadata; it does not write either Boot2 or unrelated data
partitions.

## Still blocking firmware choices

1. **USB and update path:** the schematic confirms native USB is broken out
    and the M62 CDC-ACM OTA path has been exercised successfully. WSL USB/IP
    reports 480 Mbps; two HID gamepads enumerate alongside CDC and pass real
    synthetic input tests. Composite audio enumeration remains unverified. The
    inspected kit has the expected SDK A/B/PSM table; OTA wrote into the
    overlapping `mfg` region with the user's approval. The app-only UART
    target preserves Boot2, both tables, and other partitions but is not
    rollback-safe. Record a repeatable PROG/RST recovery sequence. The
    readback confirms flash JEDEC ID `C8 60 16` and 4 MiB capacity.
2. **Bluetooth profiles and concurrency:** the manufacturer describes BLE,
   while DualSense HID and common headphone A2DP audio typically require
   Bluetooth Classic profiles. The inspected SDK has an A2DP-source example,
   and the installed SDK remote is already `sqlCRT/bouffalo_sdk`, with HID
   TX/RX pools registered. A HID host could be built on its BR/EDR L2CAP APIs,
   but that adapter, controller parsing, pairing, and M62 RF setup remain
   unimplemented/unverified. The SDK's `BLUETOOTH-LIBS.md` documents two Classic
   ACL connections for `libbtblecontroller_bl616_ble1m2s1bredr1`; two Classic
   controllers plus a separate Classic headset would require three. Decide
   whether separate operating modes are acceptable or a different controller
   stack/hardware is required before promising simultaneous operation.
3. **Resource budget:** vendor and board metadata use different RAM figures;
   usable heap after the selected stack, USB buffers, and codecs has not been
   measured. Do not promise concurrent controller/audio support until it is.

## References

- [Ai-Thinker M62 documentation](https://docs.ai-thinker.com/en/ai_m62/)
- [Zephyr Ai-Thinker M62-12F-Kit board definition](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/aithinker/ai_m62_12f_kit)
- [Bouffalo SDK](https://github.com/bouffalolab/bouffalo_sdk)
- [Bouffalo SDK BL616 4 MiB partition table](https://github.com/bouffalolab/bouffalo_sdk/blob/master/bsp/board/bl616dk/config/partition_cfg_4M.toml)
- [Bouffalo SDK BL616 OTA example and partition notes](https://github.com/bouffalolab/bouffalo_sdk/tree/master/examples/wifi/sta/wifi_ota_by_http)
- [DS5Dongle BL618 README](https://github.com/sqlCRT/ds5dongle-bl618-opensource)
