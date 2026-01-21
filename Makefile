CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := protoscript

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

test: $(BIN)
	./tests/run-tests.sh

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all test clean
