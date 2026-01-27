CC ?= cc
CMAKE ?= /opt/local/bin/cmake
OPT ?= -O2
CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude
ifeq ($(strip $(OPT)),)
  CFLAGS +=
else
  CFLAGS += $(OPT)
endif
LDLIBS ?=
PS_ENABLE_MODULE_DISPLAY ?= 1
PS_ENABLE_PERF ?= 0
PS_DISABLE_PEEPHOLE ?= 0
PS_DISABLE_CF_FUSIONS ?= 0
export PATH := /opt/local/bin:$(PATH)
PS_ENABLE_MODULE_IMG ?= $(strip $(shell awk '$$2=="PS_ENABLE_MODULE_IMG" {print $$3; found=1} END{if(!found) print 0}' include/ps_config.h))

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

CFLAGS += -DPS_ENABLE_MODULE_DISPLAY=$(PS_ENABLE_MODULE_DISPLAY)
CFLAGS += -DPS_ENABLE_MODULE_IMG=$(PS_ENABLE_MODULE_IMG)
CFLAGS += -DPS_ENABLE_PERF=$(PS_ENABLE_PERF)
CFLAGS += -DPS_DISABLE_PEEPHOLE=$(PS_DISABLE_PEEPHOLE)
CFLAGS += -DPS_DISABLE_CF_FUSIONS=$(PS_DISABLE_CF_FUSIONS)
ifeq ($(PS_ENABLE_MODULE_DISPLAY),1)
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

LIBPNG_BUNDLED_DIR := third_party/libpng/build
LIBPNG_BUNDLED_LIB := $(firstword $(wildcard $(LIBPNG_BUNDLED_DIR)/libpng*.dylib) \
                                $(wildcard $(LIBPNG_BUNDLED_DIR)/libpng*.so) \
                                $(wildcard $(LIBPNG_BUNDLED_DIR)/libpng*.a))
LIBJPEG_BUNDLED_DIR := third_party/libjpeg/build
LIBJPEG_BUNDLED_LIB := $(firstword $(wildcard $(LIBJPEG_BUNDLED_DIR)/libjpeg*.dylib) \
                                 $(wildcard $(LIBJPEG_BUNDLED_DIR)/libjpeg*.so) \
                                 $(wildcard $(LIBJPEG_BUNDLED_DIR)/libjpeg*.a))

ifeq ($(PS_ENABLE_MODULE_IMG),1)
  ifneq ($(LIBPNG_BUNDLED_LIB),)
    CFLAGS += -Ithird_party/libpng -I$(LIBPNG_BUNDLED_DIR)
    LDLIBS += $(LIBPNG_BUNDLED_LIB)
  else
    LDLIBS += -lpng
  endif
  ifneq ($(LIBJPEG_BUNDLED_LIB),)
    CFLAGS += -Ithird_party/libjpeg -Ithird_party/libjpeg/src -I$(LIBJPEG_BUNDLED_DIR)
  LDLIBS += $(LIBJPEG_BUNDLED_LIB)
  else
    CFLAGS += -Ithird_party/libjpeg/src
    LDLIBS += -ljpeg
  endif
  LDLIBS += -lz
endif
ROOT := $(shell pwd)
WEB_DIR := $(ROOT)/web
EMCC ?= emcc

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := protoscript
WEB_SRCS := $(SRC)
WEB_OUT := $(WEB_DIR)/protoscript.js
WEB_FLAGS := -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=ProtoScript -s EXIT_RUNTIME=0 -s FORCE_FILESYSTEM=1 -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 -s EXPORTED_RUNTIME_METHODS="['FS','callMain']" -Iinclude -DPS_ENABLE_MODULE_DISPLAY=0 -DPS_ENABLE_MODULE_IMG=0

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

test: $(BIN)
	./tests/run-tests.sh

test262: $(BIN)
	./test262/test262.sh

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRCS)
	$(EMCC) $(WEB_FLAGS) -o $(WEB_OUT) $(WEB_SRCS) -lm

web-clean:
	rm -f $(WEB_DIR)/protoscript.js $(WEB_DIR)/protoscript.wasm $(WEB_DIR)/protoscript.data

image-deps:
	git submodule update --init --recursive third_party/libpng third_party/libjpeg
	mkdir -p third_party/libpng/build
	$(CMAKE) -S third_party/libpng -B third_party/libpng/build -DPNG_SHARED=OFF -DPNG_TESTS=OFF
	$(CMAKE) --build third_party/libpng/build
	mkdir -p third_party/libjpeg/build
	$(CMAKE) -S third_party/libjpeg -B third_party/libjpeg/build -DENABLE_SHARED=OFF
	$(CMAKE) --build third_party/libjpeg/build

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all test test262 clean web web-clean image-deps
