#ifndef OPC_H
#define OPC_H

#include "cpu.h"
#include "bus.h"

typedef void (*opcode_handler)(cpu, uint8_t opcode);

typedef struct {
    const char *name;
    opcode_handler handler;
    int cycles;
} Opcode;

extern Opcode opcodes[256];

void opcode_init();
void execute_cb(cpu c, uint8_t opcode);

#endif
