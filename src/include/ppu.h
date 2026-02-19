#ifndef PPU_H
#define PPU_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"

#ifndef KIB
#define KIB(x) ((x) * 1024)
#endif

struct PPU {
    bus      mbus;

    int      mode;
    int      dot_counter;
    uint8_t  ly;
    bool     frame_ready;
    bool     lyc_equal_last;

    uint8_t  framebuffer[144][160];
};

typedef struct PPU* ppu;

ppu  ppu_init(bus b);
void ppu_step(ppu p, int cycles);

#endif
