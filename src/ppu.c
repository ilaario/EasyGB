#include "include/ppu.h"
#include "include/debug.h"

enum {
    LCDC_ADDR = 0xFF40,
    STAT_ADDR = 0xFF41,
    SCY_ADDR = 0xFF42,
    SCX_ADDR = 0xFF43,
    LY_ADDR = 0xFF44,
    LYC_ADDR = 0xFF45,
    OBP0_ADDR = 0xFF48,
    OBP1_ADDR = 0xFF49,
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
    uint16_t map_base = (lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;
    bool unsigned_tile_ids = (lcdc & 0x10u) != 0u;

    uint8_t bg_y = (uint8_t)(scy + line);
    uint16_t tile_row = (uint16_t)(bg_y >> 3) * 32u;
    uint8_t row_in_tile = (uint8_t)(bg_y & 0x07u);

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        uint8_t bg_x = (uint8_t)(scx + (uint8_t)x);
        uint16_t map_addr = (uint16_t)(map_base + tile_row + (uint16_t)(bg_x >> 3));
        uint8_t tile_id = bus_read8(p->mbus, map_addr);

        uint16_t tile_base;
        if (unsigned_tile_ids) {
            tile_base = (uint16_t)(0x8000u + ((uint16_t)tile_id * 16u));
        } else {
            int16_t signed_id = (int16_t)(int8_t)tile_id;
            tile_base = (uint16_t)(0x9000 + (signed_id * 16));
        }

        uint16_t tile_addr = (uint16_t)(tile_base + ((uint16_t)row_in_tile * 2u));
        uint8_t lo = bus_read8(p->mbus, tile_addr);
        uint8_t hi = bus_read8(p->mbus, (uint16_t)(tile_addr + 1u));
        int bit = 7 - (bg_x & 0x07u);
        uint8_t color_id = (uint8_t)((((hi >> bit) & 0x01u) << 1) | ((lo >> bit) & 0x01u));
        uint8_t shade = (uint8_t)((bgp >> (color_id * 2u)) & 0x03u);
        p->framebuffer[line][x] = shade;
    }
}

static void render_scanline_window(ppu p) {
    uint8_t lcdc = bus_read8(p->mbus, LCDC_ADDR);
    uint8_t line = p->ly;
    if (line >= SCREEN_HEIGHT) {
        return;
    }

    if ((lcdc & 0x20u) == 0u || (lcdc & 0x01u) == 0u) {
        return;
    }

    uint8_t wy = bus_read8(p->mbus, 0xFF4A);
    uint8_t wx = bus_read8(p->mbus, 0xFF4B);
    if (line < wy) {
        return;
    }

    int win_x0 = (int)wx - 7;
    if (win_x0 >= SCREEN_WIDTH) {
        return;
    }

    int start_x = win_x0 < 0 ? 0 : win_x0;
    uint8_t bgp = bus_read8(p->mbus, BGP_ADDR);
    uint16_t map_base = (lcdc & 0x40u) != 0u ? 0x9C00u : 0x9800u;
    bool unsigned_tile_ids = (lcdc & 0x10u) != 0u;

    uint8_t win_y = (uint8_t)(line - wy);
    uint16_t tile_row = (uint16_t)(win_y >> 3) * 32u;
    uint8_t row_in_tile = (uint8_t)(win_y & 0x07u);

    for (int x = start_x; x < SCREEN_WIDTH; x++) {
        uint8_t win_x = (uint8_t)(x - win_x0);
        uint16_t map_addr = (uint16_t)(map_base + tile_row + (uint16_t)(win_x >> 3));
        uint8_t tile_id = bus_read8(p->mbus, map_addr);

        uint16_t tile_base;
        if (unsigned_tile_ids) {
            tile_base = (uint16_t)(0x8000u + ((uint16_t)tile_id * 16u));
        } else {
            int16_t signed_id = (int16_t)(int8_t)tile_id;
            tile_base = (uint16_t)(0x9000 + (signed_id * 16));
        }

        uint16_t tile_addr = (uint16_t)(tile_base + ((uint16_t)row_in_tile * 2u));
        uint8_t lo = bus_read8(p->mbus, tile_addr);
        uint8_t hi = bus_read8(p->mbus, (uint16_t)(tile_addr + 1u));
        int bit = 7 - (win_x & 0x07u);
        uint8_t color_id = (uint8_t)((((hi >> bit) & 0x01u) << 1) | ((lo >> bit) & 0x01u));
        uint8_t shade = (uint8_t)((bgp >> (color_id * 2u)) & 0x03u);
        p->framebuffer[line][x] = shade;
    }
}

static void render_scanline_obj(ppu p) {
    uint8_t lcdc = bus_read8(p->mbus, LCDC_ADDR);
    uint8_t line = p->ly;
    if (line >= SCREEN_HEIGHT) {
        return;
    }

    if ((lcdc & 0x02u) == 0u) {
        return;
    }

    uint8_t obp0 = bus_read8(p->mbus, OBP0_ADDR);
    uint8_t obp1 = bus_read8(p->mbus, OBP1_ADDR);
    int sprite_h = ((lcdc & 0x04u) != 0u) ? 16 : 8;
    int sprites_on_line = 0;

    for (int i = 0; i < 40; i++) {
        uint16_t base = (uint16_t)(0xFE00u + (uint16_t)(i * 4));
        int sy = (int)bus_read8(p->mbus, base) - 16;
        int sx = (int)bus_read8(p->mbus, (uint16_t)(base + 1u)) - 8;
        uint8_t tile = bus_read8(p->mbus, (uint16_t)(base + 2u));
        uint8_t flags = bus_read8(p->mbus, (uint16_t)(base + 3u));

        int y = (int)line - sy;
        if (y < 0 || y >= sprite_h) {
            continue;
        }

        if (++sprites_on_line > 10) {
            break;
        }

        if ((flags & 0x40u) != 0u) {
            y = sprite_h - 1 - y;
        }

        if (sprite_h == 16) {
            tile &= 0xFEu;
            if (y >= 8) {
                tile = (uint8_t)(tile + 1u);
                y -= 8;
            }
        }

        uint16_t tile_addr = (uint16_t)(0x8000u + ((uint16_t)tile << 4) + ((uint16_t)y << 1));
        uint8_t lo = bus_read8(p->mbus, tile_addr);
        uint8_t hi = bus_read8(p->mbus, (uint16_t)(tile_addr + 1u));
        uint8_t pal = ((flags & 0x10u) != 0u) ? obp1 : obp0;
        bool bg_priority = (flags & 0x80u) != 0u;
        bool xflip = (flags & 0x20u) != 0u;

        for (int px = 0; px < 8; px++) {
            int bit = xflip ? px : (7 - px);
            uint8_t color_id = (uint8_t)((((hi >> bit) & 0x01u) << 1) | ((lo >> bit) & 0x01u));
            if (color_id == 0u) {
                continue; // Transparent for OBJ
            }

            int x = sx + px;
            if (x < 0 || x >= SCREEN_WIDTH) {
                continue;
            }

            if (bg_priority && p->framebuffer[line][x] != 0u) {
                continue;
            }

            uint8_t shade = (uint8_t)((pal >> (color_id << 1)) & 0x03u);
            p->framebuffer[line][x] = shade;
        }
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
            render_scanline_window(p);
            render_scanline_obj(p);
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
    p->frame_counter = 0;

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
            p->frame_counter++;
            if (dbg_enabled() && (p->frame_counter % 60u) == 0u) {
                int nonzero = 0;
                for (int y = 0; y < SCREEN_HEIGHT; y++) {
                    for (int x = 0; x < SCREEN_WIDTH; x++) {
                        if (p->framebuffer[y][x] != 0u) {
                            nonzero++;
                        }
                    }
                }

                dbg_log("PPU frame=%llu nonzero_pixels=%d LCDC=%02X BGP=%02X SCX=%02X SCY=%02X",
                        (unsigned long long)p->frame_counter,
                        nonzero,
                        (unsigned)bus_read8(p->mbus, LCDC_ADDR),
                        (unsigned)bus_read8(p->mbus, BGP_ADDR),
                        (unsigned)bus_read8(p->mbus, SCX_ADDR),
                        (unsigned)bus_read8(p->mbus, SCY_ADDR));
            }
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
