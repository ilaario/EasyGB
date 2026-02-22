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
TEST_TIMEOUT ?= 20

.PHONY: all clean run_pk run_test run_pk_dbg run_test_suite run_all_tests run_cpu_instrs \
        run_cpu_instrs_sing_01 run_cpu_instrs_sing_02 run_cpu_instrs_sing_03 \
        run_cpu_instrs_sing_04 run_cpu_instrs_sing_05 run_cpu_instrs_sing_06 \
        run_cpu_instrs_sing_07 run_cpu_instrs_sing_08 run_cpu_instrs_sing_09 \
        run_cpu_instrs_sing_10 run_cpu_instrs_sing_11 run_cpu_instrs_timing \
        run_cpu_halt_bug

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

run: $(BIN_SDL)
	$(BIN_SDL)

# Run Pokémon with SDL renderer window
run_pk: $(BIN_SDL)
	$(BIN_SDL) input/Pokemon_Red.gb

run_test: $(BIN_SDL)
	$(BIN_SDL) input/dmg_test_prog_ver1.gb

run_pk_dbg: $(BIN_SDL_DBG)
	$(BIN_SDL_DBG) input/Pokemon_Red.gb

run_test_suite: $(BIN)
	python3 scripts/run_test_suite.py --bin $(BIN) --timeout $(TEST_TIMEOUT)

# Auto-generated test ROM targets
TEST_TARGETS :=
TEST_TARGETS += run_test_cgb_sound_cgb_sound
TEST_TARGETS += run_test_cgb_sound_rom_singles_01_registers
TEST_TARGETS += run_test_cgb_sound_rom_singles_02_len_ctr
TEST_TARGETS += run_test_cgb_sound_rom_singles_03_trigger
TEST_TARGETS += run_test_cgb_sound_rom_singles_04_sweep
TEST_TARGETS += run_test_cgb_sound_rom_singles_05_sweep_details
TEST_TARGETS += run_test_cgb_sound_rom_singles_06_overflow_on_trigger
TEST_TARGETS += run_test_cgb_sound_rom_singles_07_len_sweep_period_sync
TEST_TARGETS += run_test_cgb_sound_rom_singles_08_len_ctr_during_power
TEST_TARGETS += run_test_cgb_sound_rom_singles_09_wave_read_while_on
TEST_TARGETS += run_test_cgb_sound_rom_singles_10_wave_trigger_while_on
TEST_TARGETS += run_test_cgb_sound_rom_singles_11_regs_after_power
TEST_TARGETS += run_test_cgb_sound_rom_singles_12_wave
TEST_TARGETS += run_test_cpu_instrs_cpu_instrs
TEST_TARGETS += run_test_cpu_instrs_individual_01_special
TEST_TARGETS += run_test_cpu_instrs_individual_02_interrupts
TEST_TARGETS += run_test_cpu_instrs_individual_03_op_sp_hl
TEST_TARGETS += run_test_cpu_instrs_individual_04_op_r_imm
TEST_TARGETS += run_test_cpu_instrs_individual_05_op_rp
TEST_TARGETS += run_test_cpu_instrs_individual_06_ld_r_r
TEST_TARGETS += run_test_cpu_instrs_individual_07_jr_jp_call_ret_rst
TEST_TARGETS += run_test_cpu_instrs_individual_08_misc_instrs
TEST_TARGETS += run_test_cpu_instrs_individual_09_op_r_r
TEST_TARGETS += run_test_cpu_instrs_individual_10_bit_ops
TEST_TARGETS += run_test_cpu_instrs_individual_11_op_a_hl_
TEST_TARGETS += run_test_dmg_sound_dmg_sound
TEST_TARGETS += run_test_dmg_sound_rom_singles_01_registers
TEST_TARGETS += run_test_dmg_sound_rom_singles_02_len_ctr
TEST_TARGETS += run_test_dmg_sound_rom_singles_03_trigger
TEST_TARGETS += run_test_dmg_sound_rom_singles_04_sweep
TEST_TARGETS += run_test_dmg_sound_rom_singles_05_sweep_details
TEST_TARGETS += run_test_dmg_sound_rom_singles_06_overflow_on_trigger
TEST_TARGETS += run_test_dmg_sound_rom_singles_07_len_sweep_period_sync
TEST_TARGETS += run_test_dmg_sound_rom_singles_08_len_ctr_during_power
TEST_TARGETS += run_test_dmg_sound_rom_singles_09_wave_read_while_on
TEST_TARGETS += run_test_dmg_sound_rom_singles_10_wave_trigger_while_on
TEST_TARGETS += run_test_dmg_sound_rom_singles_11_regs_after_power
TEST_TARGETS += run_test_dmg_sound_rom_singles_12_wave_write_while_on
TEST_TARGETS += run_test_halt_bug
TEST_TARGETS += run_test_instr_timing_instr_timing
TEST_TARGETS += run_test_interrupt_time_interrupt_time
TEST_TARGETS += run_test_mem_timing_2_mem_timing
TEST_TARGETS += run_test_mem_timing_2_rom_singles_01_read_timing
TEST_TARGETS += run_test_mem_timing_2_rom_singles_02_write_timing
TEST_TARGETS += run_test_mem_timing_2_rom_singles_03_modify_timing
TEST_TARGETS += run_test_mem_timing_individual_01_read_timing
TEST_TARGETS += run_test_mem_timing_individual_02_write_timing
TEST_TARGETS += run_test_mem_timing_individual_03_modify_timing
TEST_TARGETS += run_test_mem_timing_mem_timing
TEST_TARGETS += run_test_oam_bug_oam_bug
TEST_TARGETS += run_test_oam_bug_rom_singles_1_lcd_sync
TEST_TARGETS += run_test_oam_bug_rom_singles_2_causes
TEST_TARGETS += run_test_oam_bug_rom_singles_3_non_causes
TEST_TARGETS += run_test_oam_bug_rom_singles_4_scanline_timing
TEST_TARGETS += run_test_oam_bug_rom_singles_5_timing_bug
TEST_TARGETS += run_test_oam_bug_rom_singles_6_timing_no_bug
TEST_TARGETS += run_test_oam_bug_rom_singles_7_timing_effect
TEST_TARGETS += run_test_oam_bug_rom_singles_8_instr_effect

run_all_tests: $(TEST_TARGETS)

run_test_cgb_sound_cgb_sound: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/cgb_sound.gb"

run_test_cgb_sound_rom_singles_01_registers: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/01-registers.gb"

run_test_cgb_sound_rom_singles_02_len_ctr: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/02-len ctr.gb"

run_test_cgb_sound_rom_singles_03_trigger: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/03-trigger.gb"

run_test_cgb_sound_rom_singles_04_sweep: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/04-sweep.gb"

run_test_cgb_sound_rom_singles_05_sweep_details: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/05-sweep details.gb"

run_test_cgb_sound_rom_singles_06_overflow_on_trigger: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/06-overflow on trigger.gb"

run_test_cgb_sound_rom_singles_07_len_sweep_period_sync: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/07-len sweep period sync.gb"

run_test_cgb_sound_rom_singles_08_len_ctr_during_power: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/08-len ctr during power.gb"

run_test_cgb_sound_rom_singles_09_wave_read_while_on: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/09-wave read while on.gb"

run_test_cgb_sound_rom_singles_10_wave_trigger_while_on: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/10-wave trigger while on.gb"

run_test_cgb_sound_rom_singles_11_regs_after_power: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/11-regs after power.gb"

run_test_cgb_sound_rom_singles_12_wave: $(BIN)
	$(BIN) "input/test_roms/cgb_sound/rom_singles/12-wave.gb"

run_test_cpu_instrs_cpu_instrs: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/cpu_instrs.gb"

run_test_cpu_instrs_individual_01_special: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/01-special.gb"

run_test_cpu_instrs_individual_02_interrupts: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/02-interrupts.gb"

run_test_cpu_instrs_individual_03_op_sp_hl: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/03-op sp,hl.gb"

run_test_cpu_instrs_individual_04_op_r_imm: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/04-op r,imm.gb"

run_test_cpu_instrs_individual_05_op_rp: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/05-op rp.gb"

run_test_cpu_instrs_individual_06_ld_r_r: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/06-ld r,r.gb"

run_test_cpu_instrs_individual_07_jr_jp_call_ret_rst: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/07-jr,jp,call,ret,rst.gb"

run_test_cpu_instrs_individual_08_misc_instrs: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/08-misc instrs.gb"

run_test_cpu_instrs_individual_09_op_r_r: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/09-op r,r.gb"

run_test_cpu_instrs_individual_10_bit_ops: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/10-bit ops.gb"

run_test_cpu_instrs_individual_11_op_a_hl_: $(BIN)
	$(BIN) "input/test_roms/cpu_instrs/individual/11-op a,(hl).gb"

run_test_dmg_sound_dmg_sound: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/dmg_sound.gb"

run_test_dmg_sound_rom_singles_01_registers: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/01-registers.gb"

run_test_dmg_sound_rom_singles_02_len_ctr: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/02-len ctr.gb"

run_test_dmg_sound_rom_singles_03_trigger: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/03-trigger.gb"

run_test_dmg_sound_rom_singles_04_sweep: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/04-sweep.gb"

run_test_dmg_sound_rom_singles_05_sweep_details: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/05-sweep details.gb"

run_test_dmg_sound_rom_singles_06_overflow_on_trigger: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/06-overflow on trigger.gb"

run_test_dmg_sound_rom_singles_07_len_sweep_period_sync: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/07-len sweep period sync.gb"

run_test_dmg_sound_rom_singles_08_len_ctr_during_power: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/08-len ctr during power.gb"

run_test_dmg_sound_rom_singles_09_wave_read_while_on: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/09-wave read while on.gb"

run_test_dmg_sound_rom_singles_10_wave_trigger_while_on: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/10-wave trigger while on.gb"

run_test_dmg_sound_rom_singles_11_regs_after_power: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/11-regs after power.gb"

run_test_dmg_sound_rom_singles_12_wave_write_while_on: $(BIN)
	$(BIN) "input/test_roms/dmg_sound/rom_singles/12-wave write while on.gb"

run_test_halt_bug: $(BIN)
	$(BIN) "input/test_roms/halt_bug.gb"

run_test_instr_timing_instr_timing: $(BIN)
	$(BIN) "input/test_roms/instr_timing/instr_timing.gb"

run_test_interrupt_time_interrupt_time: $(BIN)
	$(BIN) "input/test_roms/interrupt_time/interrupt_time.gb"

run_test_mem_timing_2_mem_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing-2/mem_timing.gb"

run_test_mem_timing_2_rom_singles_01_read_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing-2/rom_singles/01-read_timing.gb"

run_test_mem_timing_2_rom_singles_02_write_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing-2/rom_singles/02-write_timing.gb"

run_test_mem_timing_2_rom_singles_03_modify_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing-2/rom_singles/03-modify_timing.gb"

run_test_mem_timing_individual_01_read_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing/individual/01-read_timing.gb"

run_test_mem_timing_individual_02_write_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing/individual/02-write_timing.gb"

run_test_mem_timing_individual_03_modify_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing/individual/03-modify_timing.gb"

run_test_mem_timing_mem_timing: $(BIN)
	$(BIN) "input/test_roms/mem_timing/mem_timing.gb"

run_test_oam_bug_oam_bug: $(BIN)
	$(BIN) "input/test_roms/oam_bug/oam_bug.gb"

run_test_oam_bug_rom_singles_1_lcd_sync: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/1-lcd_sync.gb"

run_test_oam_bug_rom_singles_2_causes: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/2-causes.gb"

run_test_oam_bug_rom_singles_3_non_causes: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/3-non_causes.gb"

run_test_oam_bug_rom_singles_4_scanline_timing: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/4-scanline_timing.gb"

run_test_oam_bug_rom_singles_5_timing_bug: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/5-timing_bug.gb"

run_test_oam_bug_rom_singles_6_timing_no_bug: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/6-timing_no_bug.gb"

run_test_oam_bug_rom_singles_7_timing_effect: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/7-timing_effect.gb"

run_test_oam_bug_rom_singles_8_instr_effect: $(BIN)
	$(BIN) "input/test_roms/oam_bug/rom_singles/8-instr_effect.gb"

# Backward-compatible aliases
run_cpu_instrs: run_test_cpu_instrs_cpu_instrs

run_cpu_instrs_sing_01: run_test_cpu_instrs_individual_01_special

run_cpu_instrs_sing_02: run_test_cpu_instrs_individual_02_interrupts

run_cpu_instrs_sing_03: run_test_cpu_instrs_individual_03_op_sp_hl

run_cpu_instrs_sing_04: run_test_cpu_instrs_individual_04_op_r_imm

run_cpu_instrs_sing_05: run_test_cpu_instrs_individual_05_op_rp

run_cpu_instrs_sing_06: run_test_cpu_instrs_individual_06_ld_r_r

run_cpu_instrs_sing_07: run_test_cpu_instrs_individual_07_jr_jp_call_ret_rst

run_cpu_instrs_sing_08: run_test_cpu_instrs_individual_08_misc_instrs

run_cpu_instrs_sing_09: run_test_cpu_instrs_individual_09_op_r_r

run_cpu_instrs_sing_10: run_test_cpu_instrs_individual_10_bit_ops

run_cpu_instrs_sing_11: run_test_cpu_instrs_individual_11_op_a_hl_

run_cpu_instrs_timing: run_test_instr_timing_instr_timing

run_cpu_halt_bug: run_test_halt_bug

clean:
	rm -rf bin
