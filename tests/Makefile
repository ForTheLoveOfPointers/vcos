CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter
BUILD ?= build

RUNTIME := ../runtime/src
COMMON_SRCS := \
	$(RUNTIME)/cpu6502.c \
	$(RUNTIME)/bus.c \
	$(RUNTIME)/cartridge.c \
	$(RUNTIME)/ppu.c \
	$(RUNTIME)/apu.c \
	$(RUNTIME)/input.c \
	$(RUNTIME)/services.c \
	$(RUNTIME)/log.c

.PHONY: all clean

all: $(BUILD)/cpu_tests $(BUILD)/cartridge_tests $(BUILD)/runtime_tests $(BUILD)/module_abi_tests

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/cpu_tests: cpu_tests.c $(COMMON_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ cpu_tests.c $(COMMON_SRCS)

$(BUILD)/cartridge_tests: cartridge_tests.c $(COMMON_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ cartridge_tests.c $(COMMON_SRCS)

$(BUILD)/runtime_tests: runtime_tests.c $(COMMON_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ runtime_tests.c $(COMMON_SRCS)

$(BUILD)/module_abi_tests: module_abi_tests.c ../include/vcos_module.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ module_abi_tests.c

clean:
	rm -rf $(BUILD)
