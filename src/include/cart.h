#ifndef CART_H
#define CART_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KIB(x) ((x) * 1024)

struct header{
    uint8_t   entry_point[8];
    uint8_t   logo[48];
    char      title[16];
    uint8_t   cgb_flag;
    uint8_t   cart_type;
    uint8_t   rom_size;
    uint8_t   ram_size;
    uint8_t   checksum;
};

typedef struct header * header;

#endif