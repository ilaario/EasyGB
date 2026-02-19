#include "include/ppu.h"
#include "include/debug.h"

enum {
    LCDC_ADDR = 0xFF40,
    STAT_ADDR = 0xFF41,
    SCY_ADDR = 0xFF42,
    SCX_ADDR = 0xFF43,
    LY_ADDR = 0xFF44,
    LYC_ADDR = 0xFF45,
    BGP_ADDR = 0xFF47,
    IF_ADDR = 0xFF0F
};

enum {
    SCREEN_HEIGHT = 144,
    SCREEN_WIDTH = 160,
    DOTS_PER_LINE = 456
};

static inline void write_io_ly(ppu p, uint8_t value) {
    p->ly = value;
    bus_set_ly(p->mbus, value);
}

static inline void write_io_stat_mode(ppu p, int mode) {
    uint8_t stat = bus_read8(p->mbus, STAT_ADDR);
    uint8_t new_stat = (uint8_t)((stat & 0xFCu) | ((uint8_t)mode & 0x03u));
    if (new_stat != stat) {
        bus_write8(p->mbus, STAT_ADDR, new_stat);
    }
}

static inline void request_vblank_interrupt(ppu p) {
    uint8_t old = bus_read8(p->mbus, IF_ADDR);
    bus_write8(p->mbus, IF_ADDR, (uint8_t)(old | 0x01u));
}

static inline void request_lcd_stat_interrupt(ppu p) {
    uint8_t old = bus_read8(p->mbus, IF_ADDR);
    bus_write8(p->mbus, IF_ADDR, (uint8_t)(old | 0x02u));
}

static void update_lyc_compare(ppu p) {
    uint8_t stat = bus_read8(p->mbus, STAT_ADDR);
    uint8_t lyc = bus_read8(p->mbus, LYC_ADDR);
    bool equal = (p->ly == lyc);

    uint8_t new_stat = equal ? (uint8_t)(stat | 0x04u)
                             : (uint8_t)(stat & (uint8_t)~0x04u);
    if (new_stat != stat) {
        bus_write8(p->mbus, STAT_ADDR, new_stat);
        stat = new_stat;
    }

    if (equal && !p->lyc_equal_last && ((stat & 0x40u) != 0u)) {
        request_lcd_stat_interrupt(p);
        dbg_log("PPU STAT IRQ (LYC==LY) LY=%u", (unsigned)p->ly);
    }

    p->lyc_equal_last = equal;
}

static void enter_mode(ppu p, int mode) {
    if (p->mode == mode) {
        return;
    }

    int old_mode = p->mode;
    p->mode = mode;
    write_io_stat_mode(p, mode);

    uint8_t stat = bus_read8(p->mbus, STAT_ADDR);
    bool stat_irq = false;

    if (mode == 0 && (stat & 0x08u) != 0u) {
        stat_irq = true;
    } else if (mode == 1 && (stat & 0x10u) != 0u) {
        stat_irq = true;
    } else if (mode == 2 && (stat & 0x20u) != 0u) {
        stat_irq = true;
    }

    if (stat_irq) {
        request_lcd_stat_interrupt(p);
        dbg_log("PPU STAT IRQ (mode %d) LY=%u dot=%d", mode, (unsigned)p->ly, p->dot_counter);
    }

    dbg_log("PPU mode %d->%d LY=%u dot=%d", old_mode, mode, (unsigned)p->ly, p->dot_counter);
}

static void render_scanline_bg(ppu p) {
    uint8_t lcdc = bus_read8(p->mbus, LCDC_ADDR);
    uint8_t line = p->ly;
    if (line >= SCREEN_HEIGHT) {
        return;
    }

    if ((lcdc & 0x01u) == 0u) {
        memset(p->framebuffer[line], 0, sizeof(p->framebuffer[line]));
        return;
    }

    uint8_t scy = bus_read8(p->mbus, SCY_ADDR);
    uint8_t scx = bus_read8(p->mbus, SCX_ADDR);
    uint8_t bgp = bus_read8(p->mbus, BGP_ADDR);

    uint8_t bg_y = (uint8_t)(scy + line);
    uint16_t tile_map_base = (lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;
    bool unsigned_tile_ids = (lcdc & 0x10u) != 0u;
    uint16_t tile_row_offset = (uint16_t)(((uint16_t)(bg_y >> 3)) * 32u);
    uint8_t tile_line = (uint8_t)((bg_y & 0x07u) << 1);

    uint8_t x = 0;
    while (x < SCREEN_WIDTH) {
        uint8_t bg_x = (uint8_t)(scx + x);
        uint16_t tile_map_addr = (uint16_t)(tile_map_base + tile_row_offset + (uint16_t)(bg_x >> 3));
        uint8_t tile_id = bus_read8(p->mbus, tile_map_addr);

        uint16_t tile_addr;
        if (unsigned_tile_ids) {
            tile_addr = (uint16_t)(0x8000u + ((uint16_t)tile_id << 4) + tile_line);
        } else {
            tile_addr = (uint16_t)(0x9000 + (((int16_t)(int8_t)tile_id) << 4) + tile_line);
        }

        uint8_t lo = bus_read8(p->mbus, tile_addr);
        uint8_t hi = bus_read8(p->mbus, (uint16_t)(tile_addr + 1u));

        int bit = 7 - (int)(bg_x & 0x07u);
        uint8_t run = (uint8_t)(8u - (bg_x & 0x07u));
        uint8_t remaining = (uint8_t)(SCREEN_WIDTH - x);
        if (run > remaining) {
            run = remaining;
        }

        for (uint8_t i = 0; i < run; i++) {
            uint8_t color_id = (uint8_t)((((hi >> bit) & 0x01u) << 1) | ((lo >> bit) & 0x01u));
            uint8_t shade = (uint8_t)((bgp >> (color_id << 1)) & 0x03u);
            p->framebuffer[line][(uint8_t)(x + i)] = shade;
            bit--;
        }

        x = (uint8_t)(x + run);
    }
}

static void update_mode_during_visible_line(ppu p) {
    int dot = p->dot_counter;

    if (dot < 80) {
        enter_mode(p, 2);
        return;
    }

    if (dot < 252) {
        if (p->mode != 3) {
            enter_mode(p, 3);
            render_scanline_bg(p);
            dbg_log("PPU rendered line LY=%u", (unsigned)p->ly);
        }
        return;
    }

    enter_mode(p, 0);
}

ppu ppu_init(bus b) {
    ppu p = (ppu)malloc(sizeof(struct PPU));
    if (p == NULL) {
        perror("[ERROR] Failed PPU allocation!");
        exit(EXIT_FAILURE);
    }

    p->mbus = b;
    p->mode = 0;
    p->dot_counter = 0;
    p->ly = 0;
    p->frame_ready = false;
    p->lyc_equal_last = false;

    memset(p->framebuffer, 0, sizeof(p->framebuffer));

    write_io_ly(p, 0);
    write_io_stat_mode(p, 0);
    update_lyc_compare(p);

    dbg_log("PPU init complete");
    return p;
}

void ppu_step(ppu p, int cycles) {
    if (cycles <= 0) {
        return;
    }

    uint8_t lcdc = bus_read8(p->mbus, LCDC_ADDR);
    if ((lcdc & 0x80u) == 0u) {
        if (p->dot_counter != 0 || p->ly != 0 || p->mode != 0) {
            p->dot_counter = 0;
            p->mode = 0;
            write_io_ly(p, 0);
            write_io_stat_mode(p, 0);
            p->lyc_equal_last = false;
            update_lyc_compare(p);
            dbg_log("PPU LCD disabled, state reset");
        }
        return;
    }

    update_lyc_compare(p);
    p->dot_counter += cycles;

    while (p->dot_counter >= DOTS_PER_LINE) {
        p->dot_counter -= DOTS_PER_LINE;

        uint8_t next_ly = (uint8_t)(p->ly + 1u);
        if (next_ly > 153u) {
            next_ly = 0u;
            p->frame_ready = true;
            dbg_log("PPU frame ready");
        }

        write_io_ly(p, next_ly);
        update_lyc_compare(p);

        if (next_ly == 144u) {
            enter_mode(p, 1);
            request_vblank_interrupt(p);
            dbg_log("PPU VBlank IRQ requested");
        }
    }

    if (p->ly < SCREEN_HEIGHT) {
        update_mode_during_visible_line(p);
    } else {
        enter_mode(p, 1);
    }
}
