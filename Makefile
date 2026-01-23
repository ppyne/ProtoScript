CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude
LDLIBS ?=
PS_ENABLE_SDL ?= 1

SDL_BUNDLED_DIR := third_party/SDL/build
SDL_BUNDLED_LIB := $(firstword $(wildcard $(SDL_BUNDLED_DIR)/libSDL2*.dylib) \
                               $(wildcard $(SDL_BUNDLED_DIR)/libSDL2*.so) \
                               $(wildcard $(SDL_BUNDLED_DIR)/libSDL2*.a))
SDL2_CONFIG ?= sdl2-config

ifeq ($(SDL_BUNDLED_LIB),)
  SDL_CFLAGS := $(shell $(SDL2_CONFIG) --cflags 2>/dev/null)
  SDL_LIBS := $(shell $(SDL2_CONFIG) --libs 2>/dev/null)
else
  SDL_CFLAGS := -Ithird_party/SDL/include
  SDL_LIBS := $(SDL_BUNDLED_LIB)
  ifneq ($(filter %.dylib,$(SDL_BUNDLED_LIB)),)
    LDLIBS += -Wl,-rpath,@loader_path/third_party/SDL/build
  endif
endif

CFLAGS += -DPS_ENABLE_SDL=$(PS_ENABLE_SDL)
ifeq ($(PS_ENABLE_SDL),1)
  ifeq ($(strip $(SDL_CFLAGS)),)
    $(error SDL2 not found. Build submodule: \
git submodule update --init --recursive third_party/SDL \
mkdir -p third_party/SDL/build \
cd third_party/SDL/build \
cmake .. -DCMAKE_BUILD_TYPE=Release \
cmake --build .)
  endif
  CFLAGS += $(SDL_CFLAGS)
  LDLIBS += $(SDL_LIBS)
endif
ROOT := $(shell pwd)
WEB_DIR := $(ROOT)/web
EMCC ?= emcc

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := protoscript
WEB_SRCS := $(SRC)
WEB_OUT := $(WEB_DIR)/protoscript.js
WEB_FLAGS := -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=ProtoScript -s EXIT_RUNTIME=0 -s FORCE_FILESYSTEM=1 -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 -s EXPORTED_RUNTIME_METHODS="['FS','callMain']" -Iinclude -DPS_ENABLE_SDL=0

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

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
