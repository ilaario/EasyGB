CC = gcc

# Common flags
CFLAGS = -D_GNU_SOURCE -std=c11 -I./headers \
         -Wall -Wextra -Werror -Wvla

# Debug build flags
DBG_FLAGS = -g -O0 -DDEBUGLOG
REL_FLAGS = -O2

SRC = src/cart.c src/bus.c src/mmu.c src/ppu.c src/apu.c src/cpu.c src/opcodes.c src/debug.c src/renderer.c src/main.c
BIN = bin/easygb
BIN_SDL = bin/easygb_sdl
BIN_SDL_DBG = bin/easygb_sdl_dbg

# SDL detection/config for windowed build
SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS = $(shell sdl2-config --libs 2>/dev/null)
SDL_LIBDIR = $(patsubst -L%,%,$(firstword $(filter -L%,$(SDL_LIBS))))
SDL_DYLIB = $(SDL_LIBDIR)/libSDL2.dylib
SDL_FILE_INFO = $(shell file "$(SDL_DYLIB)" 2>/dev/null)
HOST_ARCH = $(shell uname -m)

SDL_ARCH_FLAGS =
ifeq ($(HOST_ARCH),arm64)
ifneq (,$(findstring x86_64,$(SDL_FILE_INFO)))
ifneq (,$(findstring arm64,$(SDL_FILE_INFO)))
else
SDL_ARCH_FLAGS = -arch x86_64
endif
endif
endif

HEADLESS_FLAGS = $(CFLAGS) $(DBG_FLAGS)
SDL_BUILD_FLAGS = $(CFLAGS) $(SDL_CFLAGS) -DEASYGB_USE_SDL=1 $(SDL_ARCH_FLAGS) $(REL_FLAGS)
SDL_BUILD_FLAGS_DBG = $(CFLAGS) $(SDL_CFLAGS) -DEASYGB_USE_SDL=1 $(SDL_ARCH_FLAGS) $(DBG_FLAGS)

.PHONY: all clean run_pk run_pk_dbg run_cpu_instrs run_cpu_instrs_sing_01 run_cpu_instrs_sing_02 \
        run_cpu_instrs_sing_03 run_cpu_instrs_sing_04 run_cpu_instrs_sing_05 \
        run_cpu_instrs_sing_06 run_cpu_instrs_sing_07 run_cpu_instrs_sing_08 \
        run_cpu_instrs_sing_09 run_cpu_instrs_sing_10 run_cpu_instrs_sing_11 \
        run_cpu_instrs_timing run_cpu_halt_bug

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	$(CC) $(HEADLESS_FLAGS) -o $(BIN) $(SRC) $(LIBS)

$(BIN_SDL): $(SRC)
	@if [ -z "$(SDL_LIBS)" ]; then \
		echo "SDL2 not found. Install it or keep using headless targets."; \
		exit 1; \
	fi
	@mkdir -p bin
	$(CC) $(SDL_BUILD_FLAGS) -o $(BIN_SDL) $(SRC) $(SDL_LIBS) $(LIBS)

$(BIN_SDL_DBG): $(SRC)
	@if [ -z "$(SDL_LIBS)" ]; then \
		echo "SDL2 not found. Install it or keep using headless targets."; \
		exit 1; \
	fi
	@mkdir -p bin
	$(CC) $(SDL_BUILD_FLAGS_DBG) -o $(BIN_SDL_DBG) $(SRC) $(SDL_LIBS) $(LIBS)

# Run Pokémon with SDL renderer window
run_pk: $(BIN_SDL)
	$(BIN_SDL) input/Pokemon_Red.gb

run_pk_dbg: $(BIN_SDL_DBG)
	$(BIN_SDL_DBG) input/Pokemon_Red.gb

# Headless CPU test targets
run_cpu_instrs: $(BIN)
	$(BIN) input/test_roms/cpu_instrs/cpu_instrs.gb

run_cpu_instrs_sing_01: $(BIN)
	$(BIN) input/test_roms/cpu_instrs/individual/01-special.gb

run_cpu_instrs_sing_02: $(BIN)
	$(BIN) input/test_roms/cpu_instrs/individual/02-interrupts.gb

run_cpu_instrs_sing_03: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/03-op sp,hl.gb"

run_cpu_instrs_sing_04: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/04-op r,imm.gb"

run_cpu_instrs_sing_05: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/05-op rp.gb"

run_cpu_instrs_sing_06: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/06-ld r,r.gb"

run_cpu_instrs_sing_07: $(BIN)
	$(BIN) input/test_roms/cpu_instrs/individual/07-jr,jp,call,ret,rst.gb

run_cpu_instrs_sing_08: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/08-misc instrs.gb"

run_cpu_instrs_sing_09: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/09-op r,r.gb"

run_cpu_instrs_sing_10: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/10-bit ops.gb"

run_cpu_instrs_sing_11: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/11-op a,(hl).gb"

run_cpu_instrs_timing: $(BIN)
	$(BIN) "input/test_roms/instr_timing/instr_timing.gb"

run_cpu_halt_bug: $(BIN)
	$(BIN) "input/test_roms/halt_bug.gb"

clean:
	rm -rf bin
