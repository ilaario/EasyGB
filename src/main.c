#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"
#include "include/debug.h"
#include "include/renderer.h"

cartridge cart;
bus mbus;
cpu mcpu;
ppu mppu;
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
    
    // snapshot_bus(mbus);

    bool running = true;
#ifdef EASYGB_USE_SDL
    const int cycles_per_frame = 70224; // 154 lines * 456 dots
#endif
    while (running) {
#ifdef EASYGB_USE_SDL
        running = renderer_poll(mrender);
        bus_set_joypad_state(mbus, renderer_get_joypad_state(mrender));
        int frame_cycles = 0;
        while (running && frame_cycles < cycles_per_frame) {
            int cycles = cpu_step(mcpu);
            ppu_step(mppu, cycles);
            frame_cycles += cycles;

            if (mppu->frame_ready) {
                renderer_present(mrender, mppu->framebuffer);
                mppu->frame_ready = false;
                break;
            }
        }
#else
        running = renderer_poll(mrender);
        bus_set_joypad_state(mbus, renderer_get_joypad_state(mrender));

        int cycles = cpu_step(mcpu);
        ppu_step(mppu, cycles);

        if (mppu->frame_ready) {
            renderer_present(mrender, mppu->framebuffer);
            mppu->frame_ready = false;
        }
#endif
    }

    renderer_destroy(mrender);
    
    return 0;
}
