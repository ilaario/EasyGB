#ifndef BUS_H
#define BUS_H

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"

#ifndef KIB
#define KIB(x) ((x) * 1024)
#endif

typedef struct Bus_internal* memory;

struct Bus {
    memory mem;

    uint8_t (*read8)(struct Bus*, uint16_t);
    void    (*write8)(struct Bus*, uint16_t, uint8_t);
    uint8_t (*read16)(struct Bus*, uint16_t);
    void    (*write16)(struct Bus*, uint16_t, uint16_t);    
};

typedef struct Bus* bus;

uint8_t bus_read8(bus b, uint16_t addr);
void    bus_write8(bus b, uint16_t addr, uint8_t val);
uint16_t bus_read16(bus b, uint16_t addr);
void    bus_write16(bus b, uint16_t addr, uint16_t val);    
void    bus_tick(bus b, int cycles);
void    bus_set_ly(bus b, uint8_t ly);

bus bus_init(cartridge cart);
void snapshot_bus(bus b);

#endif
