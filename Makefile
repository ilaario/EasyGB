CC = gcc
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE -std=c11

SRC_DIR = src
BUILD_DIR = build
GFLAGS_DEBUG = $(CFLAGS) -g -O0 -Wall -D DEBUGLOG
DEBUG = gdb
AR = ar

CFLAGS  := -D_GNU_SOURCE -std=c11 -I./headers

SRC := src/cart.c src/mmu.c src/ppu.c src/cpu.c src/main.c
BIN := bin/easygb

.PHONY: all

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	gcc $(GFLAGS_DEBUG) -o $(BIN) $(SRC) $(LIBS)

run_pk: $(BIN)
	$(BIN) input/Pokemon_Red.gb

clean:
	rm -rf bin
