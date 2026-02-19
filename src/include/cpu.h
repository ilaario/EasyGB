#ifndef CPU_H
#define CPU_H

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"

#ifndef KIB
#define KIB(x) ((x) * 1024)
#endif

enum reg16 {
    REG_BC,
    REG_DE,
    REG_HL,
    REG_AF,
    REG_SP,
    REG_PC
};

enum flag{
    FLAG_Z = 0x80,
    FLAG_N = 0x40,
    FLAG_H = 0x20,
    FLAG_C = 0x10
};

struct CPU {
    // 8-bit registers
    uint8_t A, F;
    uint8_t B, C;
    uint8_t D, E;
    uint8_t H, L;


    uint16_t PC, SP;

    // flags
    bool halted;
    bool halt_bug;
    bool ime;
    uint8_t ime_pending;
    int  cycles;

    bus mbus;
};

typedef struct CPU * cpu;

cpu cpu_init(bus b);

uint16_t read_reg16(cpu c, enum reg16 reg);
void write_reg16(cpu c, enum reg16 reg, uint16_t val);
void set_flag(cpu c, enum flag f, bool val);
bool get_flag(cpu c, enum flag f);

int cpu_step(cpu c);
uint8_t cpu_fetch8(cpu c);
uint16_t cpu_fetch16(cpu c);

#endif
