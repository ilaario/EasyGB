CC = gcc

# Flag comuni
CFLAGS = -D_GNU_SOURCE -std=c11 -I./headers \
         -Wall -Wextra -Werror -Wvla
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

# Flag per build di debug
GFLAGS_DEBUG = $(CFLAGS) $(SDL_CFLAGS) -g -O0 -DDEBUGLOG

SRC_DIR   = src
BUILD_DIR = build

SRC = src/cart.c src/bus.c src/mmu.c src/ppu.c src/cpu.c src/opcodes.c src/debug.c src/renderer.c src/main.c
BIN = bin/easygb

.PHONY: all clean run_pk run_cpu_instrs

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	$(CC) $(GFLAGS_DEBUG) -o $(BIN) $(SRC) $(SDL_LIBS) $(LIBS)

# Run Pokémon (quando un giorno sarà giocabile… non oggi)
run_pk: $(BIN)
	$(BIN) input/Pokemon_Red.gb

# Target comodo per una ROM di test CPU
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
