#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"
#include "include/apu.h"
#include "include/debug.h"
#include "include/renderer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef EASYGB_USE_SDL
#include <SDL2/SDL.h>
#endif

enum {
    ROM_PATH_CAPACITY = 4096
};

#ifdef EASYGB_USE_SDL
static bool run_picker_command(const char *command, char *out_path, size_t out_size) {
    FILE *proc = popen(command, "r");
    if (proc == NULL) {
        return false;
    }

    if (fgets(out_path, (int)out_size, proc) == NULL) {
        (void)pclose(proc);
        return false;
    }

    int status = pclose(proc);
    if (status != 0) {
        return false;
    }

    out_path[strcspn(out_path, "\r\n")] = '\0';
    return out_path[0] != '\0';
}

static bool pick_rom_path(char *out_path, size_t out_size) {
    static const char *picker_commands[] = {
        "zenity --file-selection --title='EasyGB - Select ROM' "
        "--file-filter='Game Boy ROM | *.gb *.gbc' 2>/dev/null",
        "kdialog --title 'EasyGB - Select ROM' --getopenfilename . "
        "'*.gb *.gbc|Game Boy ROM (*.gb *.gbc)' 2>/dev/null",
        "osascript -e 'POSIX path of (choose file with prompt \"Select a Game Boy ROM for EasyGB\")' "
        "2>/dev/null"
    };

    for (size_t i = 0; i < (sizeof(picker_commands) / sizeof(picker_commands[0])); i++) {
        if (run_picker_command(picker_commands[i], out_path, out_size)) {
            return true;
        }
    }

    return false;
}
#endif

#ifdef EASYGB_USE_SDL
static bool is_valid_rom_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }

    return (strcasecmp(ext, ".gb") == 0) || (strcasecmp(ext, ".gbc") == 0);
}
#endif

static const char *resolve_rom_path(int argc, char const *argv[], char *selected_rom, size_t selected_rom_size) {
    if (argc == 2) {
        return argv[1];
    }

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [file.gb]\n", argv[0]);
        return NULL;
    }

#ifdef EASYGB_USE_SDL
    if (!pick_rom_path(selected_rom, selected_rom_size)) {
        fprintf(stderr, "No ROM provided and no graphical file picker was completed.\n");
        fprintf(stderr, "Install zenity/kdialog or pass the ROM path as argument.\n");
        fprintf(stderr, "Usage: %s [file.gb]\n", argv[0]);
        return NULL;
    }

    if (!is_valid_rom_path(selected_rom)) {
        fprintf(stderr, "Selected file is not a valid .gb/.gbc ROM: %s\n", selected_rom);
        return NULL;
    }

    return selected_rom;
#else
    (void)selected_rom;
    (void)selected_rom_size;
    fprintf(stderr, "Usage: %s [file.gb]\n", argv[0]);
    return NULL;
#endif
}

cartridge cart;
bus mbus;
cpu mcpu;
ppu mppu;
apu mapu;
gb_renderer mrender;

int main(int argc, char const *argv[]){
    dbg_init();

    char selected_rom[ROM_PATH_CAPACITY] = {0};
    const char *rom_path = resolve_rom_path(argc, argv, selected_rom, sizeof(selected_rom));
    if (rom_path == NULL) {
        return EXIT_FAILURE;
    }

    printf("%s\n", rom_path);
    dbg_log("Booting ROM: %s", rom_path);

    cart = read_cart(rom_path);
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
