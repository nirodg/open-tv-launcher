# Open TV sources — host dev build (no QEMU needed).
# Same sources the guest package builds; the gateway swaps its DRM/evdev
# backends for the hostdev ones (null display + scripted input).
#
#   make -C os/src run          interactive window-less loop (keys via stdin? no —
#                               use OTV_KEYSCRIPT + OTV_SHOT_OUT for shots)
#   make -C os/src check        compile + link only
#
# See os/README.md for the full dev loop.
SHELL := /bin/bash
SRC := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD := $(SRC)/../build/host

CC ?= gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -I$(SRC)/ui -I$(SRC)/launcher -I$(SRC)/gateway/include
LDLIBS := -lm

UI_SRC := $(wildcard $(SRC)/ui/*.c)
LAUNCHER_SRC := $(wildcard $(SRC)/launcher/*.c)
GW_SRC := $(SRC)/gateway/time.c \
	  $(SRC)/gateway/device.c \
	  $(wildcard $(SRC)/gateway/hostdev/*.c)
OBJ := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(UI_SRC) $(LAUNCHER_SRC) $(GW_SRC))
BIN := $(BUILD)/otv-launcher

.PHONY: all check run clean
all: $(BIN)
check: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD)
