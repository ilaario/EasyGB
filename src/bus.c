#include "include/bus.h"
#include "include/debug.h"
#include <ctype.h>

struct Bus_internal {
    cartridge rom;
    uint8_t boot_rom[0x100];
    bool boot_rom_loaded;
    bool boot_rom_enabled;

    uint8_t vram[KIB(8)];
    uint8_t wram[KIB(8)];
    uint8_t hram[127];
    uint8_t oam[160];
    uint8_t io[0x80];
    uint32_t io_write_serial[0x80];
    uint8_t ie;
    uint8_t joypad_pressed;

    uint8_t *cart_ram;
    uint32_t rom_size_bytes;
    uint32_t cart_ram_size_bytes;
    uint16_t rom_bank_count;
    uint8_t ram_bank_count;
    bool ram_enabled;

    int mapper_type;
    int rom_bank;
    uint8_t ram_bank;
    uint8_t mbc1_low5;
    uint8_t mbc1_high2;
    uint8_t mbc1_mode;
    uint8_t mbc3_rtc_sel;

    uint16_t div_counter;
    uint16_t tima_counter;
    uint16_t ppu_counter;
};

#ifdef DEBUGLOG
static const char *bus_region_name(uint16_t addr) {
    if (addr <= 0x7FFF) return "ROM";
    if (addr <= 0x9FFF) return "VRAM";
    if (addr <= 0xBFFF) return "ERAM";
    if (addr <= 0xDFFF) return "WRAM";
    if (addr <= 0xFDFF) return "ECHO";
    if (addr <= 0xFE9F) return "OAM";
    if (addr <= 0xFEFF) return "UNUSABLE";
    if (addr <= 0xFF7F) return "IO";
    if (addr <= 0xFFFE) return "HRAM";
    return "IE";
}

#define BUS_LOG_R8(addr, val)                                                    \
    do {                                                                         \
        if (dbg_mem_enabled()) {                                                 \
            dbg_log_mem("R8 %s[%04X] -> %02X", bus_region_name((addr)),         \
                        (unsigned)(addr), (unsigned)(val));                      \
        }                                                                        \
    } while (0)

#define BUS_LOG_W8(addr, val)                                                    \
    do {                                                                         \
        if (dbg_mem_enabled()) {                                                 \
            dbg_log_mem("W8 %s[%04X] <= %02X", bus_region_name((addr)),         \
                        (unsigned)(addr), (unsigned)(val));                      \
        }                                                                        \
    } while (0)
#else
#define BUS_LOG_R8(addr, val) do { (void)(addr); (void)(val); } while (0)
#define BUS_LOG_W8(addr, val) do { (void)(addr); (void)(val); } while (0)
#endif

static bool try_load_boot_rom(bus b, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }

    size_t n = fread(b->mem->boot_rom, 1, sizeof(b->mem->boot_rom), f);
    fclose(f);
    if (n != sizeof(b->mem->boot_rom)) {
        return false;
    }

    b->mem->boot_rom_loaded = true;
    b->mem->boot_rom_enabled = true;
    dbg_log("Boot ROM loaded from '%s'", path);
    return true;
}

static void maybe_init_boot_rom(bus b) {
    b->mem->boot_rom_loaded = false;
    b->mem->boot_rom_enabled = false;
    memset(b->mem->boot_rom, 0, sizeof(b->mem->boot_rom));

    const char *env_path = getenv("EASYGB_BOOTROM");
    if (try_load_boot_rom(b, env_path)) {
        return;
    }

    // Conventional local fallback.
    (void)try_load_boot_rom(b, "input/boot/dmg_boot.bin");
}

static inline uint8_t joyp_compute(bus b) {
    uint8_t select = (uint8_t)(b->mem->io[0x00] & 0x30u);
    uint8_t joyp = (uint8_t)(0xC0u | select | 0x0Fu);
    uint8_t p = b->mem->joypad_pressed;

    // P14 low selects d-pad
    if ((select & 0x10u) == 0u) {
        if ((p & JOY_RIGHT) != 0u) joyp &= (uint8_t)~0x01u;
        if ((p & JOY_LEFT)  != 0u) joyp &= (uint8_t)~0x02u;
        if ((p & JOY_UP)    != 0u) joyp &= (uint8_t)~0x04u;
        if ((p & JOY_DOWN)  != 0u) joyp &= (uint8_t)~0x08u;
    }

    // P15 low selects buttons
    if ((select & 0x20u) == 0u) {
        if ((p & JOY_A)      != 0u) joyp &= (uint8_t)~0x01u;
        if ((p & JOY_B)      != 0u) joyp &= (uint8_t)~0x02u;
        if ((p & JOY_SELECT) != 0u) joyp &= (uint8_t)~0x04u;
        if ((p & JOY_START)  != 0u) joyp &= (uint8_t)~0x08u;
    }

    return joyp;
}

static inline void joyp_request_irq_on_falling_edge(bus b, uint8_t old_joyp, uint8_t new_joyp) {
    uint8_t falling = (uint8_t)((old_joyp & 0x0Fu) & (uint8_t)~new_joyp);
    if (falling != 0u) {
        b->mem->io[0x0F] |= 0x10u;
    }
}

static inline bool is_mbc1(int mapper_type) {
    return mapper_type == 0x01 || mapper_type == 0x02 || mapper_type == 0x03;
}

static inline bool is_mbc3(int mapper_type) {
    return mapper_type == 0x0F || mapper_type == 0x10 ||
           mapper_type == 0x11 || mapper_type == 0x12 || mapper_type == 0x13;
}

static inline int clamp_rom_bank(bus b, int bank) {
    if (b->mem->rom_bank_count <= 1) {
        return 0;
    }

    if (bank <= 0) {
        bank = 1;
    }

    bank %= b->mem->rom_bank_count;
    if (bank == 0) {
        bank = 1;
    }
    return bank;
}

static inline uint8_t current_ram_bank(bus b) {
    if (b->mem->ram_bank_count == 0) {
        return 0;
    }

    if (is_mbc1(b->mem->mapper_type)) {
        if (b->mem->mbc1_mode != 0) {
            return (uint8_t)(b->mem->mbc1_high2 & 0x03u);
        }
        return 0;
    }

    if (is_mbc3(b->mem->mapper_type)) {
        if (b->mem->mbc3_rtc_sel <= 0x03u) {
            return (uint8_t)(b->mem->mbc3_rtc_sel & 0x03u);
        }
        return 0xFFu; // RTC registers not implemented.
    }

    return 0;
}

static inline void refresh_mbc1_rom_bank(bus b) {
    uint8_t low5 = (uint8_t)(b->mem->mbc1_low5 & 0x1Fu);
    if (low5 == 0) {
        low5 = 1;
    }

    uint8_t high2 = (b->mem->mbc1_mode == 0) ? (uint8_t)(b->mem->mbc1_high2 & 0x03u) : 0u;
    int bank = (int)((high2 << 5) | low5);
    b->mem->rom_bank = clamp_rom_bank(b, bank);
}

static void handle_mbc_write(bus b, uint16_t addr, uint8_t val) {
    if (is_mbc1(b->mem->mapper_type)) {
        if (addr <= 0x1FFFu) {
            b->mem->ram_enabled = (val & 0x0Fu) == 0x0Au;
            return;
        }
        if (addr <= 0x3FFFu) {
            b->mem->mbc1_low5 = (uint8_t)(val & 0x1Fu);
            refresh_mbc1_rom_bank(b);
            return;
        }
        if (addr <= 0x5FFFu) {
            b->mem->mbc1_high2 = (uint8_t)(val & 0x03u);
            refresh_mbc1_rom_bank(b);
            return;
        }
        b->mem->mbc1_mode = (uint8_t)(val & 0x01u);
        refresh_mbc1_rom_bank(b);
        return;
    }

    if (is_mbc3(b->mem->mapper_type)) {
        if (addr <= 0x1FFFu) {
            b->mem->ram_enabled = (val & 0x0Fu) == 0x0Au;
            return;
        }
        if (addr <= 0x3FFFu) {
            int bank = (int)(val & 0x7Fu);
            b->mem->rom_bank = clamp_rom_bank(b, bank);
            return;
        }
        if (addr <= 0x5FFFu) {
            b->mem->mbc3_rtc_sel = (uint8_t)(val & 0x0Fu);
            b->mem->ram_bank = (uint8_t)(val & 0x03u);
            return;
        }
        // 0x6000-0x7FFF: RTC latch, ignored for now.
        return;
    }
}

static void handle_special_io_write(bus b, uint16_t addr, uint8_t val) {
    // Writes to DIV reset it to 0 regardless of the written value.
    if (addr == 0xFF04) {
        b->mem->io[0x04] = 0x00;
        b->mem->div_counter = 0;
    }

    // Keep upper TAC bits high and reset internal TIMA prescaler on change.
    if (addr == 0xFF07) {
        b->mem->io[0x07] = (uint8_t)((val & 0x07u) | 0xF8u);
        b->mem->tima_counter = 0;
    }

    // LY is read-only; writes reset it.
    if (addr == 0xFF44) {
        b->mem->io[0x44] = 0x00;
        b->mem->ppu_counter = 0;
    }

    // When LCD is disabled, LY resets to 0 and the PPU timing state stops.
    if (addr == 0xFF40 && (val & 0x80u) == 0u) {
        b->mem->io[0x44] = 0x00;
        b->mem->ppu_counter = 0;
    }

    // Disable boot ROM mapping.
    if (addr == 0xFF50 && b->mem->boot_rom_loaded && val != 0) {
        b->mem->boot_rom_enabled = false;
    }

    // OAM DMA transfer: copy 160 bytes from XX00-XX9F to FE00-FE9F.
    if (addr == 0xFF46) {
        uint16_t src = (uint16_t)val << 8;
        for (uint16_t i = 0; i < 0x00A0u; i++) {
            b->mem->oam[i] = bus_read8(b, (uint16_t)(src + i));
        }
    }

    // Serial output (SB/SC): used by many test ROMs (e.g. blargg).
    // When SC has start bit + internal clock (0x81), emit SB to stdout.
    if (addr == 0xFF02 && (val & 0x81u) == 0x81u) {
        uint8_t ch = b->mem->io[0x01];
        putchar((char)ch);
        fflush(stdout);
        dbg_log("SERIAL TX: 0x%02X '%c'", ch,
                isprint((int)ch) ? (char)ch : '.');

        // Transfer complete: clear start bit, keep clock select.
        b->mem->io[0x02] = 0x01;
        // Raise serial interrupt request.
        b->mem->io[0x0F] |= 0x08;
    }
}

void snapshot_bus(bus b) {
    if (!b) {
        printf("[SNAPSHOT] Bus pointer is NULL\n");
        return;
    }

    // Create log directory if needed
    struct stat st = {0};
    if (stat("log", &st) == -1) {
        mkdir("log", 0700);
    }

    // Build timestamped filename
    char filename[256];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(filename, sizeof(filename),
             "log/bus_dump_%Y-%m-%d_%H-%M-%S.txt", t);

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("[ERROR] Unable to create snapshot file");
        return;
    }

    fprintf(f, "==================== BUS SNAPSHOT =====================\n");

    fprintf(f, "Mapper type : %d\n", b->mem->mapper_type);
    fprintf(f, "ROM bank    : %d\n", b->mem->rom_bank);
    fprintf(f, "IE register : 0x%02X\n", b->mem->ie);

    // VRAM
    fprintf(f, "\n-------------------- VRAM (8 KiB) ---------------------\n");
    for (int i = 0; i < KIB(8); i++) {
        if (i % 16 == 0) fprintf(f, "\n0x%04X: ", i);
        fprintf(f, "%02X ", b->mem->vram[i]);
    }

    // WRAM
    fprintf(f, "\n\n-------------------- WRAM (8 KiB) ---------------------\n");
    for (int i = 0; i < KIB(8); i++) {
        if (i % 16 == 0) fprintf(f, "\n0x%04X: ", i);
        fprintf(f, "%02X ", b->mem->wram[i]);
    }

    // HRAM
    fprintf(f, "\n\n------------------ HRAM (127 bytes) -------------------\n");
    for (int i = 0; i < 127; i++) {
        if (i % 16 == 0) fprintf(f, "\n0x%04X: ", i);
        fprintf(f, "%02X ", b->mem->hram[i]);
    }

    // OAM
    fprintf(f, "\n\n------------------- OAM (160 bytes) -------------------\n");
    for (int i = 0; i < 160; i++) {
        if (i % 16 == 0) fprintf(f, "\n0x%04X: ", i);
        fprintf(f, "%02X ", b->mem->oam[i]);
    }

    // IO
    fprintf(f, "\n\n-------------- IO Registers (0x80 bytes) --------------\n");
    for (int i = 0; i < 0x80; i++) {
        if (i % 16 == 0) fprintf(f, "\n0x%04X: ", i);
        fprintf(f, "%02X ", b->mem->io[i]);
    }

    fprintf(f, "\n=======================================================\n");

    fclose(f);
}

bus bus_init(cartridge cart) {
    bus rbus = malloc(sizeof(struct Bus));
    if (!rbus) {
        perror("[ERROR] Failed allocation of bus structure!");
        exit(EXIT_FAILURE);
    }

    rbus->mem = malloc(sizeof(struct Bus_internal));
    if (!rbus->mem) {
        perror("[ERROR] Failed allocation of bus internal memory!");
        exit(EXIT_FAILURE);
    }

    // bind cartridge
    rbus->mem->rom = cart;
    maybe_init_boot_rom(rbus);

    // Init Cart RAM
    size_t ram_bytes = KIB(cart->head->ram_size);

    if (ram_bytes > 0) {
        rbus->mem->cart_ram = calloc(1, ram_bytes);
        if (!rbus->mem->cart_ram) {
            perror("[ERROR] Failed allocating cartridge RAM");
            exit(EXIT_FAILURE);
        }
    } else {
        rbus->mem->cart_ram = NULL;
    }

    // --- Clear all RAM areas ---
    memset(rbus->mem->vram, 0x00, KIB(8));
    memset(rbus->mem->wram, 0x00, KIB(8));
    memset(rbus->mem->oam,  0x00, 160);
    memset(rbus->mem->hram, 0x00, 127);
    memset(rbus->mem->io,   0x00, 0x80);
    memset(rbus->mem->io_write_serial, 0x00, sizeof(rbus->mem->io_write_serial));

    // --- Default mapper state ---
    rbus->mem->mapper_type = cart->head->cart_type;
    rbus->mem->rom_size_bytes = KIB(cart->head->rom_size);
    rbus->mem->cart_ram_size_bytes = (uint32_t)ram_bytes;
    rbus->mem->rom_bank_count = (uint16_t)(rbus->mem->rom_size_bytes / 0x4000u);
    if (rbus->mem->rom_bank_count == 0) {
        rbus->mem->rom_bank_count = 1;
    }
    rbus->mem->ram_bank_count = (uint8_t)(ram_bytes / 0x2000u);
    rbus->mem->ram_enabled = false;
    rbus->mem->rom_bank = 1;
    rbus->mem->ram_bank = 0;
    rbus->mem->mbc1_low5 = 1;
    rbus->mem->mbc1_high2 = 0;
    rbus->mem->mbc1_mode = 0;
    rbus->mem->mbc3_rtc_sel = 0;

    // --- IO registers post-boot values (DMG) ---
    uint8_t* io = rbus->mem->io;

    io[0x00] = 0xCF; // JOYP
    io[0x01] = 0x00; // SB
    io[0x02] = 0x7E; // SC
    io[0x04] = 0xAB; // DIV
    io[0x05] = 0x00; // TIMA
    io[0x06] = 0x00; // TMA
    io[0x07] = 0xF8; // TAC
    io[0x0F] = 0xE1; // IF

    // Sound
    io[0x10] = 0x80;
    io[0x11] = 0xBF;
    io[0x12] = 0xF3;
    io[0x14] = 0xBF;
    io[0x16] = 0x3F;
    io[0x17] = 0x00;
    io[0x19] = 0xBF;
    io[0x1A] = 0x7F;
    io[0x1B] = 0xFF;
    io[0x1C] = 0x9F;
    io[0x1E] = 0xBF;
    io[0x20] = 0xFF;
    io[0x21] = 0x00;
    io[0x22] = 0x00;
    io[0x23] = 0xBF;
    io[0x24] = 0x77;
    io[0x25] = 0xF3;
    io[0x26] = 0xF1;

    // PPU
    io[0x40] = 0x91; // LCDC
    io[0x41] = 0x85; // STAT
    io[0x42] = 0x00; // SCY
    io[0x43] = 0x00; // SCX
    io[0x44] = 0x00; // LY
    io[0x45] = 0x00; // LYC
    io[0x46] = 0xFF; // DMA
    io[0x47] = 0xFC; // BGP
    io[0x48] = 0xFF; // OBP0
    io[0x49] = 0xFF; // OBP1
    io[0x4A] = 0x00; // WY
    io[0x4B] = 0x00; // WX
    io[0x50] = rbus->mem->boot_rom_loaded ? 0x00 : 0x01; // BOOT
    rbus->mem->joypad_pressed = 0;

    // Unused/DMG-only registers we can leave at 0xFF
    // but to be accurate, you set:
    io[0x4D] = 0xFF; // KEY1
    io[0x4F] = 0xFF; // VBK
    // HDMA registers, palette indexes, etc:
    io[0x51] = 0xFF;
    io[0x52] = 0xFF;
    io[0x53] = 0xFF;
    io[0x54] = 0xFF;
    io[0x55] = 0xFF;
    io[0x68] = 0xFF;
    io[0x69] = 0xFF;
    io[0x6A] = 0xFF;
    io[0x6B] = 0xFF;
    io[0x70] = 0xFF; // SVBK

    // Interrupt enable (IE)
    rbus->mem->ie = 0x00;
    rbus->mem->div_counter = 0;
    rbus->mem->tima_counter = 0;
    rbus->mem->ppu_counter = 0;

    // Function pointers (the bus logic)
    // li inizializzi tu altrove
    rbus->read8  = NULL;
    rbus->write8 = NULL;
    rbus->read16 = NULL;
    rbus->write16= NULL;

    return rbus;
}

uint8_t bus_read8(bus b, uint16_t addr) {
    if (b->mem->boot_rom_enabled && addr < 0x0100u) {
        uint8_t v = b->mem->boot_rom[addr];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // 0000–3FFF: ROM bank 0
    if (addr <= 0x3FFF) {
        uint32_t base = 0;
        if (is_mbc1(b->mem->mapper_type) && b->mem->mbc1_mode != 0) {
            base = (uint32_t)((b->mem->mbc1_high2 & 0x03u) << 5) * 0x4000u;
            if (b->mem->rom_size_bytes != 0) {
                base %= b->mem->rom_size_bytes;
            }
        }
        uint8_t v = b->mem->rom->raw_cart[base + addr];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // 4000–7FFF: switchable ROM bank
    if (addr <= 0x7FFF) {
        uint32_t bank_offset = (uint32_t)b->mem->rom_bank * 0x4000u;
        if (b->mem->rom_size_bytes != 0) {
            bank_offset %= b->mem->rom_size_bytes;
        }
        uint32_t index = bank_offset + (uint32_t)(addr - 0x4000u);
        if (b->mem->rom_size_bytes != 0) {
            index %= b->mem->rom_size_bytes;
        }
        uint8_t v = b->mem->rom->raw_cart[index];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // 8000–9FFF: VRAM
    if (addr <= 0x9FFF) {
        uint8_t v = b->mem->vram[addr - 0x8000];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // A000–BFFF: External RAM
    if (addr <= 0xBFFF) {
        if (b->mem->cart_ram == NULL || !b->mem->ram_enabled) {
            BUS_LOG_R8(addr, 0xFF);
            return 0xFF; // niente RAM → bus "aperto"
        }
        uint8_t bank = current_ram_bank(b);
        if (bank == 0xFFu) {
            BUS_LOG_R8(addr, 0xFF);
            return 0xFF;
        }
        if (b->mem->ram_bank_count != 0) {
            bank = (uint8_t)(bank % b->mem->ram_bank_count);
        } else {
            bank = 0;
        }
        uint32_t ram_addr = (uint32_t)bank * 0x2000u + (uint32_t)(addr - 0xA000u);
        if (ram_addr >= b->mem->cart_ram_size_bytes) {
            BUS_LOG_R8(addr, 0xFF);
            return 0xFF;
        }
        uint8_t v = b->mem->cart_ram[ram_addr];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // C000–DFFF: WRAM
    if (addr <= 0xDFFF) {
        uint8_t v = b->mem->wram[addr - 0xC000];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // E000–FDFF: Echo RAM (mirror della C000–DDFF)
    if (addr <= 0xFDFF) {
        uint8_t v = b->mem->wram[addr - 0xE000];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // FE00–FE9F: OAM
    if (addr <= 0xFE9F) {
        uint8_t v = b->mem->oam[addr - 0xFE00];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // FEA0–FEFF: unusable
    if (addr <= 0xFEFF) {
        BUS_LOG_R8(addr, 0x00);
        return 0x00;
    }

    // FF00–FF7F: IO registers
    if (addr <= 0xFF7F) {
        uint8_t v = b->mem->io[addr - 0xFF00];
        if (addr == 0xFF00) {
            v = joyp_compute(b);
        }
        if (addr == 0xFF0F) {
            v = (uint8_t)(v | 0xE0u);
        }
        BUS_LOG_R8(addr, v);
        return v;
    }

    // FF80–FFFE: HRAM
    if (addr <= 0xFFFE) {
        uint8_t v = b->mem->hram[addr - 0xFF80];
        BUS_LOG_R8(addr, v);
        return v;
    }

    // FFFF: IE
    BUS_LOG_R8(addr, b->mem->ie);
    return b->mem->ie;
}

void bus_write8(bus b, uint16_t addr, uint8_t val) {
    // 0000–7FFF: Cartridge / MBC control (ROM non scrivibile)
    if (addr <= 0x7FFF) {
        handle_mbc_write(b, addr, val);
        BUS_LOG_W8(addr, val);
        return;
    }

    // 8000–9FFF: VRAM
    if (addr <= 0x9FFF) {
        b->mem->vram[addr - 0x8000] = val;
        BUS_LOG_W8(addr, val);
        return;
    }

    // A000–BFFF: External RAM
    if (addr <= 0xBFFF) {
        if (b->mem->cart_ram != NULL && b->mem->ram_enabled) {
            uint8_t bank = current_ram_bank(b);
            if (bank != 0xFFu) {
                if (b->mem->ram_bank_count != 0) {
                    bank = (uint8_t)(bank % b->mem->ram_bank_count);
                } else {
                    bank = 0;
                }
                uint32_t ram_addr = (uint32_t)bank * 0x2000u + (uint32_t)(addr - 0xA000u);
                if (ram_addr < b->mem->cart_ram_size_bytes) {
                    b->mem->cart_ram[ram_addr] = val;
                }
            }
        }
        BUS_LOG_W8(addr, val);
        return;
    }

    // C000–DFFF: WRAM
    if (addr <= 0xDFFF) {
        b->mem->wram[addr - 0xC000] = val;
        BUS_LOG_W8(addr, val);
        return;
    }

    // E000–FDFF: Echo RAM
    if (addr <= 0xFDFF) {
        b->mem->wram[addr - 0xE000] = val;
        BUS_LOG_W8(addr, val);
        return;
    }

    // FE00–FE9F: OAM
    if (addr <= 0xFE9F) {
        b->mem->oam[addr - 0xFE00] = val;
        BUS_LOG_W8(addr, val);
        return;
    }

    // FEA0–FEFF: unusable
    if (addr <= 0xFEFF) {
        BUS_LOG_W8(addr, val);
        return;
    }

    // FF00–FF7F: IO registers

    if (addr <= 0xFF7F) {
        if (addr == 0xFF00) {
            uint8_t old_joyp = joyp_compute(b);
            b->mem->io[0x00] = (uint8_t)(0xC0u | (val & 0x30u) | 0x0Fu);
            uint8_t new_joyp = joyp_compute(b);
            joyp_request_irq_on_falling_edge(b, old_joyp, new_joyp);
            b->mem->io_write_serial[0x00]++;
            BUS_LOG_W8(addr, val);
            return;
        }

        if (addr == 0xFF0F) {
            val = (uint8_t)((val & 0x1Fu) | 0xE0u);
        }
        b->mem->io[addr - 0xFF00] = val;
        handle_special_io_write(b, addr, val);
        b->mem->io_write_serial[addr - 0xFF00]++;
        BUS_LOG_W8(addr, val);
        return;
    }

    // FF80–FFFE: HRAM
    if (addr <= 0xFFFE) {
        b->mem->hram[addr - 0xFF80] = val;
        BUS_LOG_W8(addr, val);
        return;
    }

    // FFFF: IE
    b->mem->ie = val;
    BUS_LOG_W8(addr, val);
}

uint16_t bus_read16(bus b, uint16_t addr) {
    uint8_t low  = bus_read8(b, addr);
    uint8_t high = bus_read8(b, addr + 1);
    return (uint16_t)((high << 8) | low);
}

void bus_write16(bus b, uint16_t addr, uint16_t val) {
    uint8_t low  = (uint8_t)(val & 0xFF);
    uint8_t high = (uint8_t)(val >> 8);

    bus_write8(b, addr,     low);
    bus_write8(b, addr + 1, high);
}

void bus_set_ly(bus b, uint8_t ly) {
    b->mem->io[0x44] = ly;
    BUS_LOG_W8(0xFF44, ly);
}

void bus_set_joypad_state(bus b, uint8_t pressed_mask) {
    if (b == NULL || b->mem == NULL) {
        return;
    }

    uint8_t old_joyp = joyp_compute(b);
    b->mem->joypad_pressed = pressed_mask;
    uint8_t new_joyp = joyp_compute(b);
    joyp_request_irq_on_falling_edge(b, old_joyp, new_joyp);
}

bool bus_boot_rom_active(bus b) {
    return b != NULL && b->mem != NULL && b->mem->boot_rom_enabled;
}

uint32_t bus_get_io_write_serial(bus b, uint16_t addr) {
    if (b == NULL || b->mem == NULL) {
        return 0;
    }
    if (addr < 0xFF00u || addr > 0xFF7Fu) {
        return 0;
    }
    return b->mem->io_write_serial[addr - 0xFF00u];
}

static inline uint16_t timer_period_cycles(uint8_t tac) {
    switch (tac & 0x03u) {
    case 0x00: return 1024; // 4096 Hz
    case 0x01: return 16;   // 262144 Hz
    case 0x02: return 64;   // 65536 Hz
    default:   return 256;  // 16384 Hz
    }
}

void bus_tick(bus b, int cycles) {
    if (cycles <= 0) {
        return;
    }

    b->mem->div_counter = (uint16_t)(b->mem->div_counter + (uint16_t)cycles);
    while (b->mem->div_counter >= 256u) {
        b->mem->div_counter = (uint16_t)(b->mem->div_counter - 256u);
        b->mem->io[0x04] = (uint8_t)(b->mem->io[0x04] + 1u);
    }

    uint8_t tac = b->mem->io[0x07];
    if ((tac & 0x04u) == 0u) {
        return;
    }

    uint16_t period = timer_period_cycles(tac);
    b->mem->tima_counter = (uint16_t)(b->mem->tima_counter + (uint16_t)cycles);

    while (b->mem->tima_counter >= period) {
        b->mem->tima_counter = (uint16_t)(b->mem->tima_counter - period);

        if (b->mem->io[0x05] == 0xFFu) {
            b->mem->io[0x05] = b->mem->io[0x06];
            b->mem->io[0x0F] |= 0x04u; // Request timer interrupt
        } else {
            b->mem->io[0x05] = (uint8_t)(b->mem->io[0x05] + 1u);
        }
    }
}
   
