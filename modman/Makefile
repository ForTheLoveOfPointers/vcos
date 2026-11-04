CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra
BUILD ?= ../build/modman
OUT ?= $(BUILD)/vcos-modman
SRCS := src/main.c src/vmod.c src/json.c src/sha256.c src/signature.c src/download.c

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRCS) src/vmod.h src/json.h src/sha256.h src/signature.h src/download.h src/vcos_pubkey.inc
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) $(LDLIBS) -lcrypto

clean:
	rm -rf $(BUILD)
