BUILD := build
X11 := $(shell pkg-config --exists x11 2>/dev/null && echo 1 || echo 0)
DRM := $(shell pkg-config --exists libdrm 2>/dev/null && echo 1 || echo 0)
VMOD_MODULE ?= buildroot/output/target/usr/lib/vcos/modules/nes.so
CROSS_CC ?= buildroot/output/host/bin/x86_64-buildroot-linux-musl-gcc

.PHONY: all runtime roms games modules module-host modman modular vmod vmod-sign vmod-genesis vmod-genesis-sign test test-genesis genesis genesis-target test-image test-modman-image run run-systems run-systems-headless run-genesis run-module run-module-headless run-headless run-qemu clean distclean buildroot-clone

all: runtime roms modules module-host modman

runtime:
	$(MAKE) -C runtime BUILD=../$(BUILD)/runtime OUT=../$(BUILD)/runtime/vcos X11=$(X11) DRM=$(DRM)

roms:
	python3 guest/tools/build_roms.py --out $(BUILD)/guest

games:
	@mkdir -p $(BUILD)/lib/games
	@if ls games/*.nes >/dev/null 2>&1; then cp -f games/*.nes $(BUILD)/lib/games/; fi

modules:
	$(MAKE) -C modules BUILD=../$(BUILD)/modules

module-host:
	$(MAKE) -C module-host BUILD=../$(BUILD)/module-host OUT=../$(BUILD)/module-host/vcos-module-host X11=$(X11) DRM=$(DRM)

modman:
	$(MAKE) -C modman BUILD=../$(BUILD)/modman OUT=../$(BUILD)/modman/vcos-modman

modular: modules module-host modman

vmod:
	@test -f $(VMOD_MODULE) || { \
		echo "missing $(VMOD_MODULE); run ./scripts/build-image.sh first" >&2; \
		exit 1; }
	mkdir -p $(BUILD)/vmod
	python3 scripts/make-vmod.py --id org.vcos.nes --name "VCOS NES" \
		--version 1.0.0 --module $(VMOD_MODULE) \
		--system nes --extension .nes --license MIT \
		--description "VCOS NES emulator module" \
		--out $(BUILD)/vmod/org.vcos.nes-1.0.0.vmod

genesis:
	./scripts/build-genesis-module.sh

genesis-target:
	@test -x "$(CROSS_CC)" || { \
		echo "missing cross compiler $(CROSS_CC); run ./scripts/build-image.sh first" >&2; \
		exit 1; }
	CC="$(CURDIR)/$(CROSS_CC)" \
	GENESIS_CORE_DIR="$(CURDIR)/build/cores/genesis-plus-gx-target" \
	GENESIS_MODULE_OUT="$(CURDIR)/build/modules/genesis-target.so" \
	./scripts/build-genesis-module.sh

vmod-genesis: genesis-target
	mkdir -p $(BUILD)/vmod
	python3 scripts/make-vmod.py --id org.vcos.genesis --name "Genesis Plus GX" \
		--version 1.0.0 --module $(BUILD)/modules/genesis-target.so \
		--system genesis --extension .md --extension .gen --extension .smd \
		--extension .bin --license "Genesis Plus GX" \
		--description "Sega Genesis / Mega Drive libretro module" \
		--out $(BUILD)/vmod/org.vcos.genesis-1.0.0.vmod

vmod-genesis-sign: vmod-genesis
	VCOS_SIGNING_KEY="$${VCOS_SIGNING_KEY:-$(CURDIR)/keys/private/vcos-signing-private.pem}" \
		./scripts/sign-vmod.sh $(BUILD)/vmod/org.vcos.genesis-1.0.0.vmod

test-genesis: genesis module-host runtime roms
	./tests/genesis_module_tests.sh

run-genesis: genesis module-host
	./$(BUILD)/module-host/vcos-module-host \
		--module $(BUILD)/modules/genesis.so \
		--content tests/assets/genesis-hello-world.bin \
		--backend x11 --audio auto --no-kmsg

run-systems: runtime roms games modules module-host genesis
	@mkdir -p $(BUILD)/lib/games
	@cp -f tests/assets/genesis-hello-world.bin $(BUILD)/lib/games/
	./$(BUILD)/runtime/vcos \
		--rom $(BUILD)/guest/shell.nes \
		--shell $(BUILD)/guest/shell.nes \
		--lib $(BUILD)/guest \
		--data $(BUILD)/lib \
		--modules $(BUILD)/modules \
		--module-host $(BUILD)/module-host/vcos-module-host \
		--backend x11 --audio auto --verbose

run-systems-headless: runtime roms games modules module-host genesis
	@mkdir -p $(BUILD)/lib/games
	@cp -f tests/assets/genesis-hello-world.bin $(BUILD)/lib/games/
	./$(BUILD)/runtime/vcos \
		--rom $(BUILD)/guest/shell.nes \
		--shell $(BUILD)/guest/shell.nes \
		--lib $(BUILD)/guest \
		--data $(BUILD)/lib \
		--modules $(BUILD)/modules \
		--module-host $(BUILD)/module-host/vcos-module-host \
		--backend ppm --frames 3 --screenshot $(BUILD)/systems.ppm \
		--audio none --verbose

vmod-sign: vmod
	VCOS_SIGNING_KEY="$${VCOS_SIGNING_KEY:-$(CURDIR)/keys/private/vcos-signing-private.pem}" \
		./scripts/sign-vmod.sh $(BUILD)/vmod/org.vcos.nes-1.0.0.vmod

test: runtime roms modules module-host modman
	$(MAKE) -C tests BUILD=../$(BUILD)/tests
	./$(BUILD)/tests/cpu_tests
	./$(BUILD)/tests/cartridge_tests
	./$(BUILD)/tests/runtime_tests
	./$(BUILD)/tests/module_abi_tests
	./tests/module_host_tests.sh
	./tests/modman_tests.sh
	./tests/modman_online_tests.sh
	./tests/integration.sh

run: runtime roms games modules module-host
	./$(BUILD)/runtime/vcos --rom $(BUILD)/guest/shell.nes \
		--lib $(BUILD)/guest --data $(BUILD)/lib \
		--modules $(BUILD)/modules \
		--module-host $(BUILD)/module-host/vcos-module-host \
		--backend x11 --scale 3 --verbose

run-headless: runtime roms games modules module-host
	./$(BUILD)/runtime/vcos --rom $(BUILD)/guest/shell.nes \
		--lib $(BUILD)/guest --data $(BUILD)/lib \
		--modules $(BUILD)/modules \
		--module-host $(BUILD)/module-host/vcos-module-host \
		--backend ppm --frames 3 \
		--screenshot $(BUILD)/shell.ppm --verbose

run-module: modules module-host
	@test -f games/table_tennis.nes || { echo "missing games/table_tennis.nes" >&2; exit 1; }
	./$(BUILD)/module-host/vcos-module-host \
		--module $(BUILD)/modules/nes.so \
		--content games/table_tennis.nes \
		--backend x11 --audio auto --no-kmsg

run-module-headless: modules module-host
	@test -f games/table_tennis.nes || { echo "missing games/table_tennis.nes" >&2; exit 1; }
	./$(BUILD)/module-host/vcos-module-host \
		--module $(BUILD)/modules/nes.so \
		--content games/table_tennis.nes \
		--backend ppm --frames 3 --no-pace --audio none --no-kmsg \
		--screenshot $(BUILD)/module-nes.ppm \
		--log $(BUILD)/module-host.log

test-image:
	./tests/qemu_boot.sh

test-modman-image: vmod
	./tests/qemu_modman.sh

run-qemu:
	./scripts/run-qemu.sh

buildroot-clone:
	./scripts/get-buildroot.sh

clean:
	rm -rf $(BUILD)
	$(MAKE) -C runtime clean
	$(MAKE) -C modules clean
	$(MAKE) -C module-host clean
	$(MAKE) -C modman clean

distclean: clean
	rm -rf buildroot/buildroot
