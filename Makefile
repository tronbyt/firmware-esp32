# Makefile for Tronbyt Firmware

PROJECT_NAME := firmware

.PHONY: all clean fullclean flash monitor menuconfig help tidbyt-gen1 tidbyt-gen1_swap tidbyt-gen2 tronbyt-s3 tronbyt-s3-wide pixoticker matrixportal-s3 matrixportal-s3-waveshare

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

all:
	idf.py build

clean:
	idf.py clean

fullclean:
	idf.py fullclean
	rm -f sdkconfig

flash:
	idf.py flash

monitor:
	idf.py monitor

menuconfig:
	idf.py menuconfig

# Macro for device-specific builds
# Usage: $(call build_device,<target>,<defaults_file>)
define build_device
	rm -f sdkconfig
	IDF_TARGET=$(1) idf.py set-target $(1)
	IDF_TARGET=$(1) idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;$(2)" build
endef

# Device Specific Targets
tidbyt-gen1:
	$(call build_device,esp32,sdkconfig.defaults.tidbyt-gen1)

tidbyt-gen1_swap:
	$(call build_device,esp32,sdkconfig.defaults.tidbyt-gen1_swap)

tidbyt-gen2:
	$(call build_device,esp32,sdkconfig.defaults.tidbyt-gen2)

tronbyt-s3:
	$(call build_device,esp32s3,sdkconfig.defaults.tronbyt-s3)

tronbyt-s3-wide:
	$(call build_device,esp32s3,sdkconfig.defaults.tronbyt-s3-wide)

pixoticker:
	$(call build_device,esp32,sdkconfig.defaults.pixoticker)

matrixportal-s3:
	$(call build_device,esp32s3,sdkconfig.defaults.matrixportal-s3)

matrixportal-s3-waveshare:
	$(call build_device,esp32s3,sdkconfig.defaults.matrixportal-s3-waveshare)