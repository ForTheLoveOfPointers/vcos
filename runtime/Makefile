CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -Iinclude
LDFLAGS ?=
LDLIBS ?=
LDLIBS += -ldl
X11 ?= 0
DRM ?= $(shell pkg-config --exists libdrm 2>/dev/null && echo 1 || echo 0)
BUILD ?= build
OUT ?= $(BUILD)/vcos

SRCS := $(wildcard src/*.c) $(wildcard src/backends/*.c)
OBJS := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRCS))

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

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILD)
