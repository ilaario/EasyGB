#ifndef APU_H
#define APU_H

#include <stdint.h>

#include "bus.h"

typedef struct APU* apu;

apu  apu_init(bus b);
void apu_destroy(apu a);
void apu_step(apu a, int cpu_cycles);

#endif
