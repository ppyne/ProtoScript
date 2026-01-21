CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude
ROOT := $(shell pwd)
WEB_DIR := $(ROOT)/web
EMCC ?= emcc

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := protoscript
WEB_SRCS := $(SRC)
WEB_OUT := $(WEB_DIR)/protoscript.js
WEB_FLAGS := -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=ProtoScript -s EXIT_RUNTIME=0 -s FORCE_FILESYSTEM=1 -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 -s EXPORTED_RUNTIME_METHODS="['FS','callMain']" -Iinclude

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

test: $(BIN)
	./tests/run-tests.sh

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRCS)
	$(EMCC) $(WEB_FLAGS) -o $(WEB_OUT) $(WEB_SRCS) -lm

web-clean:
	rm -f $(WEB_DIR)/protoscript.js $(WEB_DIR)/protoscript.wasm $(WEB_DIR)/protoscript.data

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all test clean web web-clean
