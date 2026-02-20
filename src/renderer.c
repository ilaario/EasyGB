#include "include/renderer.h"
#include "include/bus.h"

#ifdef EASYGB_USE_SDL
#include <SDL2/SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>

enum {
    LCD_WIDTH = 160,
    LCD_HEIGHT = 144
};

struct GBRenderer {
#ifdef EASYGB_USE_SDL
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t pixels[LCD_WIDTH * LCD_HEIGHT];
    uint8_t joypad_state;
#else
    uint8_t joypad_state;
#endif
};

#ifdef EASYGB_USE_SDL

static inline uint32_t shade_to_rgba(uint8_t shade) {
    switch (shade & 0x03u) {
    case 0: return 0xE0F8D0FFu;
    case 1: return 0x88C070FFu;
    case 2: return 0x346856FFu;
    default: return 0x081820FFu;
    }
}

gb_renderer renderer_init(int scale) {
    if (scale <= 0) {
        scale = 4;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return NULL;
    }

    gb_renderer r = (gb_renderer)calloc(1, sizeof(struct GBRenderer));
    if (r == NULL) {
        fprintf(stderr, "[ERROR] Renderer allocation failed\n");
        SDL_Quit();
        return NULL;
    }

    r->window = SDL_CreateWindow(
        "EasyGB",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LCD_WIDTH * scale, LCD_HEIGHT * scale,
        SDL_WINDOW_SHOWN
    );
    if (r->window == NULL) {
        fprintf(stderr, "[ERROR] SDL_CreateWindow failed: %s\n", SDL_GetError());
        renderer_destroy(r);
        return NULL;
    }

    r->renderer = SDL_CreateRenderer(
        r->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (r->renderer == NULL) {
        fprintf(stderr, "[ERROR] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        renderer_destroy(r);
        return NULL;
    }

    if (SDL_RenderSetLogicalSize(r->renderer, LCD_WIDTH, LCD_HEIGHT) != 0) {
        fprintf(stderr, "[ERROR] SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        renderer_destroy(r);
        return NULL;
    }

    r->texture = SDL_CreateTexture(
        r->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, LCD_HEIGHT
    );
    if (r->texture == NULL) {
        fprintf(stderr, "[ERROR] SDL_CreateTexture failed: %s\n", SDL_GetError());
        renderer_destroy(r);
        return NULL;
    }

    return r;
}

void renderer_destroy(gb_renderer r) {
    if (r != NULL) {
        if (r->texture != NULL) {
            SDL_DestroyTexture(r->texture);
        }
        if (r->renderer != NULL) {
            SDL_DestroyRenderer(r->renderer);
        }
        if (r->window != NULL) {
            SDL_DestroyWindow(r->window);
        }
        free(r);
    }

    SDL_Quit();
}

bool renderer_poll(gb_renderer r) {
    if (r == NULL) {
        return false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            return false;
        }
    }

    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t state = 0;

    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) state |= JOY_RIGHT;
    if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) state |= JOY_LEFT;
    if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) state |= JOY_UP;
    if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) state |= JOY_DOWN;

    if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_K]) state |= JOY_A;
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_J]) state |= JOY_B;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) state |= JOY_START;
    if (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_RSHIFT]) state |= JOY_SELECT;

    r->joypad_state = state;

    return true;
}

uint8_t renderer_get_joypad_state(gb_renderer r) {
    if (r == NULL) {
        return 0;
    }
    return r->joypad_state;
}

void renderer_present(gb_renderer r, uint8_t framebuffer[144][160]) {
    for (int y = 0; y < LCD_HEIGHT; y++) {
        uint32_t *dst = &r->pixels[y * LCD_WIDTH];
        for (int x = 0; x < LCD_WIDTH; x++) {
            dst[x] = shade_to_rgba(framebuffer[y][x]);
        }
    }

    SDL_UpdateTexture(r->texture, NULL, r->pixels, LCD_WIDTH * (int)sizeof(uint32_t));
    SDL_RenderClear(r->renderer);
    SDL_RenderCopy(r->renderer, r->texture, NULL, NULL);
    SDL_RenderPresent(r->renderer);
}

#else

gb_renderer renderer_init(int scale) {
    (void)scale;
    gb_renderer r = (gb_renderer)calloc(1, sizeof(struct GBRenderer));
    if (r == NULL) {
        fprintf(stderr, "[ERROR] Renderer allocation failed\n");
        return NULL;
    }
    return r;
}

void renderer_destroy(gb_renderer r) {
    free(r);
}

bool renderer_poll(gb_renderer r) {
    (void)r;
    return true;
}

uint8_t renderer_get_joypad_state(gb_renderer r) {
    (void)r;
    return 0;
}

void renderer_present(gb_renderer r, uint8_t framebuffer[144][160]) {
    (void)r;
    (void)framebuffer;
}

#endif
