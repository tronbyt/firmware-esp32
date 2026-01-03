# Makefile for Tronbyt Firmware

IDF_PATH ?= $(shell echo $$IDF_PATH)
PROJECT_NAME := firmware

.PHONY: all clean fullclean flash monitor menuconfig help submodules tidbyt-gen1 tidbyt-gen1_swap tidbyt-gen2 tronbyt-s3 tronbyt-s3-wide pixoticker matrixportal-s3 matrixportal-s3-waveshare

help:
	@echo "Tronbyt Firmware Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build for the currently configured board"
	@echo "  clean            Clean the build directory"
	@echo "  fullclean        Delete the build directory"
	@echo "  flash            Flash the firmware"
	@echo "  monitor          Monitor the serial output"
	@echo "  menuconfig       Open the configuration menu"
	@echo ""
	@echo "Device Specific Builds (Builds, but does not flash):"
	@echo "  tidbyt-gen1              Build for Tidbyt Gen 1"
	@echo "  tidbyt-gen1_swap         Build for Tidbyt Gen 1 (Swap Colors)"
	@echo "  tidbyt-gen2              Build for Tidbyt Gen 2"
	@echo "  tronbyt-s3               Build for Tronbyt S3"
	@echo "  tronbyt-s3-wide          Build for Tronbyt S3 Wide"
	@echo "  pixoticker               Build for Pixoticker"
	@echo "  matrixportal-s3          Build for MatrixPortal S3"
	@echo "  matrixportal-s3-waveshare Build for MatrixPortal S3 (Waveshare)"

submodules:
	@if [ ! -f external/libwebp/CMakeLists.txt ] || [ ! -f components/ESP32-HUB75-MatrixPanel-DMA/CMakeLists.txt ]; then \
		echo "Initializing submodules..."; \
		git submodule update --init --recursive; \
	fi

all: submodules
	idf.py build

clean:
	idf.py clean

fullclean:
	idf.py fullclean
	rm -f sdkconfig

flash: submodules
	idf.py flash

monitor: submodules
	idf.py monitor

menuconfig: submodules
	idf.py menuconfig

# Device Specific Targets
tidbyt-gen1: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tidbyt-gen1" build

tidbyt-gen1_swap: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tidbyt-gen1_swap" build

tidbyt-gen2: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tidbyt-gen2" build

tronbyt-s3: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tronbyt-s3" build

tronbyt-s3-wide: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tronbyt-s3-wide" build

pixoticker: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.pixoticker" build

matrixportal-s3: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.matrixportal-s3" build

matrixportal-s3-waveshare: submodules
	idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.matrixportal-s3-waveshare" build
