CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter
override CFLAGS += -fPIC -fvisibility=hidden -I$(INCLUDE) -I$(RUNTIME)
LDFLAGS ?=
BUILD ?= ../build/modules

INCLUDE ?= ../include
RUNTIME ?= ../runtime/src
NES_SRCS := \
	nes/nes_module.c \
	nes/services_stub.c \
	$(RUNTIME)/cpu6502.c \
	$(RUNTIME)/bus.c \
	$(RUNTIME)/cartridge.c \
	$(RUNTIME)/ppu.c \
	$(RUNTIME)/apu.c \
	$(RUNTIME)/input.c \
	$(RUNTIME)/log.c

.PHONY: all clean

all: $(BUILD)/null.so $(BUILD)/nes.so

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/null.so: null/null_module.c $(INCLUDE)/vcos_module.h | $(BUILD)
	$(CC) $(CFLAGS) -shared -o $@ null/null_module.c $(LDFLAGS)

$(BUILD)/nes.so: $(NES_SRCS) $(INCLUDE)/vcos_module.h | $(BUILD)
	$(CC) $(CFLAGS) -shared -Wl,-Bsymbolic -o $@ $(NES_SRCS) $(LDFLAGS)

clean:
	rm -rf $(BUILD)
