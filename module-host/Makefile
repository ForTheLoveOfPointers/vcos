CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter
INCLUDE ?= ../include
RUNTIME ?= ../runtime/src
CPPFLAGS += -I$(INCLUDE) -I$(RUNTIME)
LDFLAGS ?=
LDLIBS ?=
LDLIBS += -ldl
X11 ?= $(shell pkg-config --exists x11 2>/dev/null && echo 1 || echo 0)
DRM ?= $(shell pkg-config --exists libdrm 2>/dev/null && echo 1 || echo 0)
BUILD ?= ../build/module-host
OUT ?= $(BUILD)/vcos-module-host

SRCS := $(wildcard src/*.c) \
	$(RUNTIME)/audio.c \
	$(RUNTIME)/input.c \
	$(RUNTIME)/log.c \
	$(wildcard $(RUNTIME)/backends/*.c)
HOST_OBJS := $(patsubst src/%.c,$(BUILD)/obj/host/%.o,$(wildcard src/*.c))
CORE_OBJS := $(patsubst $(RUNTIME)/%.c,$(BUILD)/obj/runtime/%.o,$(filter $(RUNTIME)/%,$(SRCS)))
OBJS := $(HOST_OBJS) $(CORE_OBJS)

ifeq ($(X11),1)
override CFLAGS += -DVCOS_WITH_X11
LDLIBS += -lX11
endif

ifeq ($(DRM),1)
override CFLAGS += -DVCOS_WITH_DRM
ifneq ($(DRM_CFLAGS),)
override CFLAGS += $(DRM_CFLAGS)
LDLIBS += $(DRM_LIBS)
else
override CFLAGS += $(shell pkg-config --cflags libdrm 2>/dev/null)
LDLIBS += $(shell pkg-config --libs libdrm 2>/dev/null)
endif
endif

.PHONY: all clean

all: $(OUT)

$(BUILD)/obj/host/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/runtime/%.o: $(RUNTIME)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILD)
