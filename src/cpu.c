#include "include/cpu.h"
#include "include/opcodes.h"
#include "include/debug.h"

#ifdef DEBUGLOG
#include <inttypes.h>
#endif

static const uint16_t interrupt_vectors[5] = {
    0x40, // VBlank
    0x48, // LCD STAT
    0x50, // Timer
    0x58, // Serial
    0x60  // Joypad
};

#ifdef DEBUGLOG
typedef struct {
    uint16_t pc;
    uint16_t sp;
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;
    uint8_t if_reg;
    uint8_t ie_reg;
    uint8_t ime_pending;
    bool ime;
    bool halted;
    int cycles;
} cpu_trace_state;

static bool trace_cfg_initialized = false;
static uint64_t trace_every = 1;
static uint64_t trace_until_step = 0;

static void cpu_trace_init_cfg(void) {
    if (trace_cfg_initialized) {
        return;
    }
    trace_cfg_initialized = true;

    const char *every = getenv("EASYGB_LOG_EVERY");
    if (every != NULL && every[0] != '\0') {
        unsigned long long parsed = strtoull(every, NULL, 10);
        if (parsed > 0) {
            trace_every = (uint64_t)parsed;
        }
    }

    const char *until = getenv("EASYGB_LOG_UNTIL");
    if (until != NULL && until[0] != '\0') {
        trace_until_step = (uint64_t)strtoull(until, NULL, 10);
    }
}

static bool cpu_trace_should_log(uint64_t step) {
    cpu_trace_init_cfg();

    if (!dbg_enabled()) {
        return false;
    }
    if (trace_until_step != 0 && step > trace_until_step) {
        return false;
    }
    return (step % trace_every) == 0;
}

static inline cpu_trace_state cpu_capture_state(cpu c) {
    cpu_trace_state s;
    s.pc = c->PC;
    s.sp = c->SP;
    s.af = read_reg16(c, REG_AF);
    s.bc = read_reg16(c, REG_BC);
    s.de = read_reg16(c, REG_DE);
    s.hl = read_reg16(c, REG_HL);
    s.if_reg = bus_read8(c->mbus, 0xFF0F);
    s.ie_reg = bus_read8(c->mbus, 0xFFFF);
    s.ime_pending = c->ime_pending;
    s.ime = c->ime;
    s.halted = c->halted;
    s.cycles = c->cycles;
    return s;
}

static void cpu_trace_log_begin(uint64_t step, const cpu_trace_state *s,
                                uint8_t op0, uint8_t op1, uint8_t op2) {
    dbg_log(
        "STEP=%" PRIu64
        " BEGIN PC=%04X OP=%02X NEXT=%02X %02X AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X "
        "IME=%d IMEP=%u HALT=%d IF=%02X IE=%02X CYC=%d",
        step,
        s->pc, op0, op1, op2,
        s->af, s->bc, s->de, s->hl, s->sp,
        s->ime ? 1 : 0, s->ime_pending, s->halted ? 1 : 0, s->if_reg, s->ie_reg, s->cycles
    );
}

static void cpu_trace_log_end(uint64_t step, const char *reason,
                              const cpu_trace_state *before, const cpu_trace_state *after) {
    dbg_log(
        "STEP=%" PRIu64
        " END reason=%s PC=%04X->%04X SP=%04X->%04X AF=%04X->%04X BC=%04X->%04X DE=%04X->%04X HL=%04X->%04X "
        "IME=%d->%d IMEP=%u->%u HALT=%d->%d IF=%02X->%02X IE=%02X->%02X dCYC=%d CYC=%d",
        step,
        reason,
        before->pc, after->pc,
        before->sp, after->sp,
        before->af, after->af,
        before->bc, after->bc,
        before->de, after->de,
        before->hl, after->hl,
        before->ime ? 1 : 0, after->ime ? 1 : 0,
        before->ime_pending, after->ime_pending,
        before->halted ? 1 : 0, after->halted ? 1 : 0,
        before->if_reg, after->if_reg,
        before->ie_reg, after->ie_reg,
        after->cycles - before->cycles,
        after->cycles
    );
}
#endif

cpu cpu_init(bus b){
    dbg_init();
    static bool dbg_shutdown_registered = false;
    if (!dbg_shutdown_registered) {
        atexit(dbg_shutdown);
        dbg_shutdown_registered = true;
    }

    cpu rcpu = (cpu)malloc(sizeof(struct CPU));
    if(rcpu == NULL){
        perror("[ERROR] Failed CPU allocation!");
        exit(EXIT_FAILURE);
    }

    rcpu -> mbus = b;

    // Set registers as if BIOS ended
    rcpu -> A = 0x01;
    rcpu -> F = 0xB0;

    rcpu -> B = 0x00;
    rcpu -> C = 0x13;

    rcpu -> D = 0x00;
    rcpu -> E = 0xD8;

    rcpu -> H = 0x01;
    rcpu -> L = 0x4D;

    rcpu -> SP = 0xFFFE;
    rcpu -> PC = 0x0100;

    // internal state
    rcpu -> halted = false;
    rcpu -> halt_bug = false;
    rcpu -> ime = true;
    rcpu -> ime_pending = 0;
    rcpu -> cycles = 0;

    opcode_init();
    dbg_log("CPU init complete: PC=%04X SP=%04X AF=%04X", rcpu->PC, rcpu->SP, read_reg16(rcpu, REG_AF));

    return rcpu;
}

uint16_t read_reg16(cpu c, enum reg16 reg){
    switch (reg) {
    case REG_BC:
        return (c -> B << 8) | c -> C;    
    case REG_DE:
        return (c -> D << 8) | c -> E;  
    case REG_HL:
        return (c -> H << 8) | c -> L;
    case REG_AF:
        return (c -> A << 8) | (c -> F & 0xF0);
    case REG_SP:
        return c -> SP;
    case REG_PC:
        return c -> PC;  
    default:
        return 0xFF;
    }
}

void write_reg16(cpu c, enum reg16 reg, uint16_t val){
    uint8_t high = (val >> 8) & 0xFF;
    uint8_t low  = val & 0xFF;

    switch(reg){
        case REG_BC:
            c -> B = high;
            c -> C = low;
            break;
        case REG_DE:
            c -> D = high;
            c -> E = low;
            break;
        case REG_HL:
            c -> H = high;
            c -> L = low;
            break;
        case REG_AF:
            c -> A = high;
            c -> F = low & 0xF0;
            break;
        case REG_SP:
            c -> SP = val;
            break;
        case REG_PC:
            c -> PC = val;
            break;
    }
}

void set_flag(cpu c, enum flag f, bool val){
    if(val == true) c -> F = c -> F | f;
    else c -> F &= (uint8_t)(~f);
    c -> F &= 0xF0;
}

bool get_flag(cpu c, enum flag f){
    return (c -> F & f) != 0;
}

void execute_opcode(cpu c, uint8_t opcode){
    Opcode entry = opcodes[opcode];

    if(entry.handler == NULL){
        dbg_log("FATAL: missing handler for opcode %02X at PC=%04X", opcode, c->PC);
        perror("[ERROR] Opcode not implemented!");
        printf("{DEBUG} Opcode value = 0x%X\n", opcode);
        snapshot_bus(c -> mbus);
        exit(EXIT_FAILURE);
    }

    entry.handler(c, opcode);
}

static inline uint8_t cpu_read_IF(cpu cpu) {
    return bus_read8(cpu->mbus, 0xFF0F);
}

static inline void cpu_write_IF(cpu cpu, uint8_t v) {
    bus_write8(cpu->mbus, 0xFF0F, v);
}

static inline uint8_t cpu_read_IE(cpu cpu) {
    return bus_read8(cpu->mbus, 0xFFFF);
}

static inline uint8_t cpu_read_pending_interrupts(cpu cpu) {
    uint8_t IF = cpu_read_IF(cpu);
    uint8_t IE = cpu_read_IE(cpu);
    return (uint8_t)(IF & IE & 0x1Fu);
}

bool interrupt_should_fire(cpu c){
    if(!c -> ime) return false;

    bool pending = cpu_read_pending_interrupts(c);
    return pending != 0;
}

static inline void cpu_push16(cpu cpu, uint16_t val) {
    cpu->SP--;
    bus_write8(cpu->mbus, cpu->SP, (val >> 8) & 0xFF);
    cpu->SP--;
    bus_write8(cpu->mbus, cpu->SP, val & 0xFF);
}

static inline int find_lowest_set_bit(uint8_t x) {
    for(int i = 0; i < 5; i++) {
        if(x & (1 << i)) return i;
    }
    return -1;
}

void cpu_service_interrupt(cpu cpu) {

    uint8_t IF = cpu_read_IF(cpu);
    uint8_t IE = cpu_read_IE(cpu);

    uint8_t pending = IF & IE;

    if (pending == 0) return;

    // trova il primo IRQ attivo in ordine di priorità
    int irq = find_lowest_set_bit(pending);
    if (irq < 0) return;

    // disabilita interrupt master
    cpu->ime = false;

    // clear bit in IF
    IF &= ~(1 << irq);
    cpu_write_IF(cpu, IF);

    // push PC
    cpu_push16(cpu, cpu->PC);

    // jump al vettore dell’interrupt
    dbg_log("IRQ service: irq=%d vector=%04X PC=%04X SP=%04X IF=%02X IE=%02X",
            irq, interrupt_vectors[irq], cpu->PC, cpu->SP, IF, IE);
    cpu->PC = interrupt_vectors[irq];
    cpu->cycles += 20;
}

void handle_interrupts(cpu cpu) {
    uint8_t pending = cpu_read_pending_interrupts(cpu);
    if (pending == 0) {
        return;
    }

    if (cpu->ime) {
        cpu_service_interrupt(cpu);
        cpu->halted = false;
    } else {
        dbg_log("HALT released by pending interrupt with IME=0");
        cpu->halted = false;
    }
}

static inline void cpu_apply_ime_delay(cpu c) {
    if (c->ime_pending > 0) {
        c->ime_pending--;
        if (c->ime_pending == 0) {
            c->ime = true;
            dbg_log("IME enabled after EI delay");
        }
    }
}

int cpu_step(cpu c){
    uint64_t step = dbg_next_step();
#ifndef DEBUGLOG
    (void)step;
#endif
    int cycles_before_step = c -> cycles;

#ifdef DEBUGLOG
    cpu_trace_state before = {0};
    cpu_trace_state after = {0};
    bool trace_this_step = false;
    uint8_t preview0 = 0;
    uint8_t preview1 = 0;
    uint8_t preview2 = 0;

    if (dbg_enabled()) {
        before = cpu_capture_state(c);
        preview0 = bus_read8(c->mbus, c->PC);
        preview1 = bus_read8(c->mbus, (uint16_t)(c->PC + 1));
        preview2 = bus_read8(c->mbus, (uint16_t)(c->PC + 2));
        trace_this_step = cpu_trace_should_log(step);
        if (trace_this_step) {
            cpu_trace_log_begin(step, &before, preview0, preview1, preview2);
        }
    }
#endif

    if(c -> halted){
        int cycles_before = c -> cycles;
        handle_interrupts(c);
        if (c->halted && c->cycles == cycles_before) {
            c -> cycles += 4;
        }
        int step_cycles = c->cycles - cycles_before_step;
        bus_tick(c -> mbus, step_cycles);
        cpu_apply_ime_delay(c);

#ifdef DEBUGLOG
        if (trace_this_step) {
            after = cpu_capture_state(c);
            cpu_trace_log_end(step, after.halted ? "HALT_WAIT" : "HALT_WAKE", &before, &after);
        }
#endif
        return step_cycles;
    }

    if(!c->halt_bug && interrupt_should_fire(c)){
        cpu_service_interrupt(c);
        int step_cycles = c->cycles - cycles_before_step;
        bus_tick(c -> mbus, step_cycles);
        cpu_apply_ime_delay(c);

#ifdef DEBUGLOG
        if (trace_this_step) {
            after = cpu_capture_state(c);
            cpu_trace_log_end(step, "INTERRUPT", &before, &after);
        }
#endif
        return step_cycles;
    }

    uint8_t opcode = cpu_fetch8(c);
    if (c->halt_bug) {
        c->halt_bug = false;
        c->PC = (uint16_t)(c->PC - 1);
    }

    execute_opcode(c, opcode);
    int step_cycles = c->cycles - cycles_before_step;
    bus_tick(c -> mbus, step_cycles);
    cpu_apply_ime_delay(c);

#ifdef DEBUGLOG
    if (trace_this_step) {
        after = cpu_capture_state(c);
        if (opcode == 0xCB) {
            cpu_trace_log_end(step, "OPCODE_CB", &before, &after);
        } else {
            cpu_trace_log_end(step, "OPCODE", &before, &after);
        }
    }
#endif
    return step_cycles;
}

uint8_t cpu_fetch8(cpu c){
    uint8_t byte = bus_read8(c -> mbus, c -> PC);
    c -> PC += 1;
    return byte;
}

uint16_t cpu_fetch16(cpu c){
    uint16_t bytes = bus_read16(c -> mbus, c -> PC);
    c -> PC += 2;
    return bytes;
}
