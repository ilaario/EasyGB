#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"
#include "include/apu.h"
#include "include/debug.h"
#include "include/renderer.h"
#ifdef EASYGB_USE_SDL
#include <SDL2/SDL.h>
#endif

cartridge cart;
bus mbus;
cpu mcpu;
ppu mppu;
apu mapu;
gb_renderer mrender;

int main(int argc, char const *argv[]){
    dbg_init();

    if(argc != 2){
        perror("Wrong usage of arguments, needed <file.gb>");
        exit(EXIT_FAILURE);
    }

    printf("%s\n", argv[1]);
    dbg_log("Booting ROM: %s", argv[1]);

    cart = read_cart(argv[1]);
    mbus = bus_init(cart);
    mcpu = cpu_init(mbus);
    mppu = ppu_init(mbus);
    mrender = renderer_init(4);
    if (mrender == NULL) {
        return EXIT_FAILURE;
    }
    mapu = apu_init(mbus);
    
    // snapshot_bus(mbus);

    bool running = true;
#ifdef EASYGB_USE_SDL
    const int cycles_per_frame = 70224; // 154 lines * 456 dots
    const uint64_t gb_cpu_hz = 4194304u;
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    if (perf_freq == 0u) {
        perf_freq = 1u;
    }
    uint64_t next_frame_tick = SDL_GetPerformanceCounter();
#endif
    while (running) {
#ifdef EASYGB_USE_SDL
        running = renderer_poll(mrender);
        if (!running) {
            break;
        }
        bus_set_joypad_state(mbus, renderer_get_joypad_state(mrender));
        int frame_cycles = 0;
        while (running && frame_cycles < cycles_per_frame) {
            int cycles = cpu_step(mcpu);
            ppu_step(mppu, cycles);
            apu_step(mapu, cycles);
            frame_cycles += cycles;

            if (mppu->frame_ready) {
                renderer_present(mrender, mppu->framebuffer);
                mppu->frame_ready = false;
                break;
            }
        }

        int speed_multiplier = renderer_get_speed_multiplier(mrender);
        if (speed_multiplier < 1) {
            speed_multiplier = 1;
        }

        uint64_t frame_ticks =
            ((uint64_t)cycles_per_frame * perf_freq) /
            (gb_cpu_hz * (uint64_t)speed_multiplier);
        if (frame_ticks == 0u) {
            frame_ticks = 1u;
        }

        next_frame_tick += frame_ticks;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame_tick) {
            uint64_t remaining_ticks = next_frame_tick - now;
            uint32_t delay_ms = (uint32_t)((remaining_ticks * 1000u) / perf_freq);
            if (delay_ms > 1u) {
                SDL_Delay(delay_ms - 1u);
            }
            while (SDL_GetPerformanceCounter() < next_frame_tick) {
            }
        } else if ((now - next_frame_tick) > perf_freq) {
            // If we are very late (e.g. debugger pause), resync to current time.
            next_frame_tick = now;
        }
#else
        running = renderer_poll(mrender);
        bus_set_joypad_state(mbus, renderer_get_joypad_state(mrender));

        int cycles = cpu_step(mcpu);
        ppu_step(mppu, cycles);
        apu_step(mapu, cycles);

        if (mppu->frame_ready) {
            renderer_present(mrender, mppu->framebuffer);
            mppu->frame_ready = false;
        }
#endif
    }

    apu_destroy(mapu);
    renderer_destroy(mrender);
    
    return 0;
}
