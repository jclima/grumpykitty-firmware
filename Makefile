# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
# GrumpyKitty firmware developer tooling. See DEVELOPMENT.md.
ENV ?= esp32s3box

# Locate pio: PATH first, then the PlatformIO default install.
PIO ?= $(shell command -v pio 2>/dev/null || ([ -x $(HOME)/.platformio/penv/bin/pio ] && echo $(HOME)/.platformio/penv/bin/pio))

# Serial port: override with PORT=/dev/cu.usbmodemXXXX, otherwise auto-detect.
PORT ?= $(firstword $(wildcard /dev/cu.usbmodem* /dev/ttyACM* /dev/cu.usbserial* /dev/ttyUSB*))
PORT_UPLOAD  = $(if $(PORT),--upload-port $(PORT),)
PORT_MONITOR = $(if $(PORT),--port $(PORT),)

.PHONY: build upload monitor flash-monitor clean size devices bootstrap-check dist

bootstrap-check:
	@if [ -z "$(PIO)" ]; then \
	  echo "pio not found on PATH or at ~/.platformio/penv/bin/pio"; \
	  echo "Install PlatformIO: https://docs.platformio.org/en/latest/core/installation/"; exit 1; \
	fi
	@echo "pio: $(PIO)"; $(PIO) --version

build: bootstrap-check
	$(PIO) run -e $(ENV)

upload: bootstrap-check
	@echo "Port: $(or $(PORT),<pio auto-detect>)  (board must be in Download Mode, see DEVELOPMENT.md)"
	$(PIO) run -e $(ENV) -t upload $(PORT_UPLOAD)

monitor: bootstrap-check
	$(PIO) device monitor -e $(ENV) $(PORT_MONITOR)

flash-monitor: upload
	$(PIO) device monitor -e $(ENV) $(PORT_MONITOR)

clean: bootstrap-check
	$(PIO) run -e $(ENV) -t clean

size: bootstrap-check
	$(PIO) run -e $(ENV) -t size

devices: bootstrap-check
	$(PIO) device list

# Builds the firmware (runs `make build`), then writes dist/grumpykitty-<VERSION>-merged.bin,
# dist/grumpykitty-<VERSION>-firmware.bin and dist/manifest.json. See docs/RELEASING.md.
dist: build
	python3 tools/release/merge_bin.py --build-dir .pio/build/$(ENV)
