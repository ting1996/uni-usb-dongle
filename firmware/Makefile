BL_SDK_BASE ?= $(abspath ../../bouffalo_sdk)
SDK_DEMO_PATH := $(abspath .)
CHIP ?= bl616
BUILD_DIR ?= build

ifeq ($(strip $(BOARD)),)
$(error Set BOARD=bl616dk for an SDK smoke build or BOARD=m62_12f for the M62 profile)
endif
ifeq ($(BOARD),bl616dk)
else ifeq ($(BOARD),m62_12f)
ifneq ($(CHIP),bl616)
$(error BOARD=m62_12f requires CHIP=bl616)
endif
CONFIG_CONSOLE_UART_BAUDRATE ?= 115200
BOARD_DIR := $(SDK_DEMO_PATH)/boards
ifeq ($(OS),Windows_NT)
COMX ?= COM5
else
COMX ?= /dev/ttyUSB0
endif
BAUDRATE ?= 115200
else
$(error Unsupported BOARD=$(BOARD); supported boards are bl616dk and m62_12f)
endif
ifneq ($(filter flash,$(MAKECMDGOALS)),)
$(error Generic flash is blocked; use BOARD=m62_12f flash-app-m62 for the app-only plan)
endif
ifneq ($(filter flash-app-m62,$(MAKECMDGOALS)),)
ifneq ($(BOARD),m62_12f)
$(error flash-app-m62 requires BOARD=m62_12f)
endif
ifneq ($(BUILD_DIR),build)
$(error flash-app-m62 requires BUILD_DIR=build because its programming config uses that output path)
endif
ifneq ($(strip $(CPU_ID)),)
$(error flash-app-m62 requires an unset CPU_ID because its config targets the single-core app image)
endif
endif
ifneq ($(or $(filter /% \% %:%,$(subst \,/,$(BUILD_DIR))),$(findstring :,$(BUILD_DIR))),)
$(error BUILD_DIR must be relative to firmware/ for the SDK linker script)
endif

export BL_SDK_BASE
export SDK_DEMO_PATH

include $(BL_SDK_BASE)/project.build

M62_APP_IMAGE := $(BUILD_DIR)/build_out/uni_usb_dongle_bl616.bin
M62_FLASH_CONFIG := flash_prog_cfg_m62_app.ini

.PHONY: flash-app-m62
flash-app-m62: $(BUILD_DIR)
ifeq ($(OS),Windows_NT)
	@powershell -NoProfile -Command "if (-not (Test-Path -LiteralPath '$(M62_APP_IMAGE)')) { [Console]::Error.WriteLine('Missing M62 app image: $(M62_APP_IMAGE)'); exit 1 }; if ((Get-Item -LiteralPath '$(M62_APP_IMAGE)').Length -le 0) { [Console]::Error.WriteLine('Empty M62 app image: $(M62_APP_IMAGE)'); exit 1 }; if ((Get-Item -LiteralPath '$(M62_APP_IMAGE)').Length -gt 2097152) { [Console]::Error.WriteLine('M62 app image exceeds slot 0 limit of 2097152 bytes'); exit 1 }"
else
	@test -s "$(M62_APP_IMAGE)" || { echo "Missing or empty M62 app image: $(M62_APP_IMAGE)" >&2; exit 1; }
	@size=$$(wc -c < "$(M62_APP_IMAGE)" | tr -d '[:space:]'); \
	if [ "$$size" -gt 2097152 ]; then \
		echo "M62 app image is $$size bytes; slot 0 limit is 2097152 bytes" >&2; \
		exit 1; \
	fi
endif
	$(BL_FLASH_PROGRAM) --interface=uart --baudrate=$(BAUDRATE) \
		--port=$(COMX) --chipname=$(CHIP) --config=$(M62_FLASH_CONFIG) --reset
