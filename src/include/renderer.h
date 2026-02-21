#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GBRenderer* gb_renderer;

gb_renderer renderer_init(int scale);
void renderer_destroy(gb_renderer r);
bool renderer_poll(gb_renderer r);
uint8_t renderer_get_joypad_state(gb_renderer r);
int renderer_get_speed_multiplier(gb_renderer r);
void renderer_present(gb_renderer r, uint8_t framebuffer[144][160]);

#endif
