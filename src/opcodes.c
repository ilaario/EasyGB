#include "include/opcodes.h"

#include <stdio.h>

Opcode opcodes[256];
static char opcode_names[256][8];

static const enum reg16 rp_table[4] = {
    REG_BC, REG_DE, REG_HL, REG_SP
};

static const enum reg16 rp2_table[4] = {
    REG_BC, REG_DE, REG_HL, REG_AF
};

static inline uint8_t read_r8(cpu c, uint8_t r) {
    switch (r) {
    case 0: return c->B;
    case 1: return c->C;
    case 2: return c->D;
    case 3: return c->E;
    case 4: return c->H;
    case 5: return c->L;
    case 6: return bus_read8(c->mbus, read_reg16(c, REG_HL));
    default: return c->A;
    }
}

static inline void write_r8(cpu c, uint8_t r, uint8_t val) {
    switch (r) {
    case 0: c->B = val; break;
    case 1: c->C = val; break;
    case 2: c->D = val; break;
    case 3: c->E = val; break;
    case 4: c->H = val; break;
    case 5: c->L = val; break;
    case 6: bus_write8(c->mbus, read_reg16(c, REG_HL), val); break;
    default: c->A = val; break;
    }
}

static inline uint16_t read_rp(cpu c, uint8_t p) {
    return read_reg16(c, rp_table[p & 0x03]);
}

static inline void write_rp(cpu c, uint8_t p, uint16_t v) {
    write_reg16(c, rp_table[p & 0x03], v);
}

static inline uint16_t read_rp2(cpu c, uint8_t p) {
    return read_reg16(c, rp2_table[p & 0x03]);
}

static inline void write_rp2(cpu c, uint8_t p, uint16_t v) {
    write_reg16(c, rp2_table[p & 0x03], v);
}

static inline void push16(cpu c, uint16_t val) {
    c->SP--;
    bus_write8(c->mbus, c->SP, (uint8_t)((val >> 8) & 0xFF));
    c->SP--;
    bus_write8(c->mbus, c->SP, (uint8_t)(val & 0xFF));
}

static inline uint16_t pop16(cpu c) {
    uint8_t low = bus_read8(c->mbus, c->SP++);
    uint8_t high = bus_read8(c->mbus, c->SP++);
    return (uint16_t)((high << 8) | low);
}

static inline bool condition_is_true(cpu c, uint8_t cond) {
    switch (cond & 0x03) {
    case 0: return !get_flag(c, FLAG_Z); // NZ
    case 1: return  get_flag(c, FLAG_Z); // Z
    case 2: return !get_flag(c, FLAG_C); // NC
    default: return  get_flag(c, FLAG_C); // C
    }
}

static inline uint8_t inc8(cpu c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, (v & 0x0F) == 0x0F);
    return r;
}

static inline uint8_t dec8(cpu c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (v & 0x0F) == 0x00);
    return r;
}

static inline void add_a(cpu c, uint8_t v, bool with_carry) {
    uint8_t carry = (with_carry && get_flag(c, FLAG_C)) ? 1u : 0u;
    uint16_t a = c->A;
    uint16_t sum = (uint16_t)(a + v + carry);

    c->A = (uint8_t)sum;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((a & 0x0F) + (v & 0x0F) + carry) > 0x0F);
    set_flag(c, FLAG_C, sum > 0xFF);
}

static inline void sub_a(cpu c, uint8_t v, bool with_carry) {
    uint8_t carry = (with_carry && get_flag(c, FLAG_C)) ? 1u : 0u;
    uint16_t a = c->A;
    uint16_t sub = (uint16_t)(v + carry);
    uint16_t res = (uint16_t)(a - sub);

    c->A = (uint8_t)res;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (a & 0x0F) < ((v & 0x0F) + carry));
    set_flag(c, FLAG_C, a < sub);
}

static inline void and_a(cpu c, uint8_t v) {
    c->A &= v;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, true);
    set_flag(c, FLAG_C, false);
}

static inline void xor_a(cpu c, uint8_t v) {
    c->A ^= v;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
}

static inline void or_a(cpu c, uint8_t v) {
    c->A |= v;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
}

static inline void cp_a(cpu c, uint8_t v) {
    uint16_t a = c->A;
    uint16_t res = (uint16_t)(a - v);
    set_flag(c, FLAG_Z, ((uint8_t)res) == 0);
    set_flag(c, FLAG_N, true);
    set_flag(c, FLAG_H, (a & 0x0F) < (v & 0x0F));
    set_flag(c, FLAG_C, a < v);
}

static inline void add_hl(cpu c, uint16_t v) {
    uint32_t hl = read_reg16(c, REG_HL);
    uint32_t sum = hl + v;

    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF);
    set_flag(c, FLAG_C, sum > 0xFFFF);
    write_reg16(c, REG_HL, (uint16_t)sum);
}

static inline uint16_t add_sp_e8(cpu c, int8_t s8) {
    uint16_t sp = c->SP;
    uint16_t u8 = (uint16_t)(uint8_t)s8;
    uint16_t result = (uint16_t)(sp + s8);

    set_flag(c, FLAG_Z, false);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, ((sp & 0x0F) + (u8 & 0x0F)) > 0x0F);
    set_flag(c, FLAG_C, ((sp & 0xFF) + (u8 & 0xFF)) > 0xFF);
    return result;
}

static inline void daa(cpu c) {
    uint8_t a = c->A;
    uint8_t adjust = 0;
    bool carry = get_flag(c, FLAG_C);

    if (!get_flag(c, FLAG_N)) {
        if (get_flag(c, FLAG_H) || ((a & 0x0F) > 0x09)) {
            adjust |= 0x06;
        }
        if (carry || (a > 0x99)) {
            adjust |= 0x60;
            carry = true;
        }
        a = (uint8_t)(a + adjust);
    } else {
        if (get_flag(c, FLAG_H)) {
            adjust |= 0x06;
        }
        if (carry) {
            adjust |= 0x60;
        }
        a = (uint8_t)(a - adjust);
    }

    c->A = a;
    set_flag(c, FLAG_Z, c->A == 0);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
}

static inline uint8_t rlc(cpu c, uint8_t v) {
    bool carry = (v & 0x80u) != 0;
    uint8_t r = (uint8_t)((v << 1) | (carry ? 1u : 0u));
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
    return r;
}

static inline uint8_t rrc(cpu c, uint8_t v) {
    bool carry = (v & 0x01u) != 0;
    uint8_t r = (uint8_t)((v >> 1) | (carry ? 0x80u : 0u));
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
    return r;
}

static inline uint8_t rl(cpu c, uint8_t v) {
    uint8_t carry_in = get_flag(c, FLAG_C) ? 1u : 0u;
    bool carry_out = (v & 0x80u) != 0;
    uint8_t r = (uint8_t)((v << 1) | carry_in);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry_out);
    return r;
}

static inline uint8_t rr(cpu c, uint8_t v) {
    uint8_t carry_in = get_flag(c, FLAG_C) ? 0x80u : 0u;
    bool carry_out = (v & 0x01u) != 0;
    uint8_t r = (uint8_t)((v >> 1) | carry_in);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry_out);
    return r;
}

static inline uint8_t sla(cpu c, uint8_t v) {
    bool carry = (v & 0x80u) != 0;
    uint8_t r = (uint8_t)(v << 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
    return r;
}

static inline uint8_t sra(cpu c, uint8_t v) {
    bool carry = (v & 0x01u) != 0;
    uint8_t r = (uint8_t)((v >> 1) | (v & 0x80u));
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
    return r;
}

static inline uint8_t srl(cpu c, uint8_t v) {
    bool carry = (v & 0x01u) != 0;
    uint8_t r = (uint8_t)(v >> 1);
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, carry);
    return r;
}

static inline uint8_t swap(cpu c, uint8_t v) {
    uint8_t r = (uint8_t)((v << 4) | (v >> 4));
    set_flag(c, FLAG_Z, r == 0);
    set_flag(c, FLAG_N, false);
    set_flag(c, FLAG_H, false);
    set_flag(c, FLAG_C, false);
    return r;
}

static inline void do_alu_a_r(cpu c, uint8_t alu, uint8_t val) {
    switch (alu & 0x07) {
    case 0: add_a(c, val, false); break; // ADD
    case 1: add_a(c, val, true);  break; // ADC
    case 2: sub_a(c, val, false); break; // SUB
    case 3: sub_a(c, val, true);  break; // SBC
    case 4: and_a(c, val);        break; // AND
    case 5: xor_a(c, val);        break; // XOR
    case 6: or_a(c, val);         break; // OR
    default: cp_a(c, val);        break; // CP
    }
}

static inline void op_illegal(cpu c) {
    c->halted = true;
    c->ime = false;
    c->ime_pending = 0;
    c->PC--;
    c->cycles += 4;
}

void execute_cb(cpu c, uint8_t opcode) {
    uint8_t x = (uint8_t)(opcode >> 6);
    uint8_t y = (uint8_t)((opcode >> 3) & 0x07);
    uint8_t z = (uint8_t)(opcode & 0x07);
    uint8_t val = read_r8(c, z);

    switch (x) {
    case 0:
        switch (y) {
        case 0: val = rlc(c, val);  break;
        case 1: val = rrc(c, val);  break;
        case 2: val = rl(c, val);   break;
        case 3: val = rr(c, val);   break;
        case 4: val = sla(c, val);  break;
        case 5: val = sra(c, val);  break;
        case 6: val = swap(c, val); break;
        default: val = srl(c, val); break;
        }
        write_r8(c, z, val);
        c->cycles += (z == 6) ? 16 : 8;
        return;

    case 1:
        set_flag(c, FLAG_Z, (val & (uint8_t)(1u << y)) == 0);
        set_flag(c, FLAG_N, false);
        set_flag(c, FLAG_H, true);
        c->cycles += (z == 6) ? 12 : 8;
        return;

    case 2:
        val = (uint8_t)(val & ~(uint8_t)(1u << y));
        write_r8(c, z, val);
        c->cycles += (z == 6) ? 16 : 8;
        return;

    default:
        val = (uint8_t)(val | (uint8_t)(1u << y));
        write_r8(c, z, val);
        c->cycles += (z == 6) ? 16 : 8;
        return;
    }
}

static void execute_base(cpu c, uint8_t opcode) {
    uint8_t x = (uint8_t)(opcode >> 6);
    uint8_t y = (uint8_t)((opcode >> 3) & 0x07);
    uint8_t z = (uint8_t)(opcode & 0x07);
    uint8_t p = (uint8_t)(y >> 1);
    uint8_t q = (uint8_t)(y & 0x01);

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: // NOP
                c->cycles += 4;
                return;
            case 1: { // LD (a16), SP
                uint16_t addr = cpu_fetch16(c);
                bus_write8(c->mbus, addr, (uint8_t)(c->SP & 0xFF));
                bus_write8(c->mbus, (uint16_t)(addr + 1), (uint8_t)(c->SP >> 8));
                c->cycles += 20;
                return;
            }
            case 2: // STOP n8 (simplified)
                (void)cpu_fetch8(c);
                c->halted = true;
                c->cycles += 4;
                return;
            case 3: { // JR e8
                int8_t rel = (int8_t)cpu_fetch8(c);
                c->PC = (uint16_t)(c->PC + rel);
                c->cycles += 12;
                return;
            }
            default: { // JR cc, e8
                int8_t rel = (int8_t)cpu_fetch8(c);
                if (condition_is_true(c, (uint8_t)(y - 4))) {
                    c->PC = (uint16_t)(c->PC + rel);
                    c->cycles += 12;
                } else {
                    c->cycles += 8;
                }
                return;
            }
            }

        case 1:
            if (q == 0) { // LD rp[p], d16
                uint16_t imm = cpu_fetch16(c);
                write_rp(c, p, imm);
                c->cycles += 12;
            } else { // ADD HL, rp[p]
                add_hl(c, read_rp(c, p));
                c->cycles += 8;
            }
            return;

        case 2:
            if (q == 0) { // LD (rp[p]), A
                uint16_t addr = 0;
                switch (p) {
                case 0: addr = read_reg16(c, REG_BC); break;
                case 1: addr = read_reg16(c, REG_DE); break;
                case 2:
                    addr = read_reg16(c, REG_HL);
                    write_reg16(c, REG_HL, (uint16_t)(addr + 1));
                    break;
                default:
                    addr = read_reg16(c, REG_HL);
                    write_reg16(c, REG_HL, (uint16_t)(addr - 1));
                    break;
                }
                bus_write8(c->mbus, addr, c->A);
            } else { // LD A, (rp[p])
                uint16_t addr = 0;
                switch (p) {
                case 0: addr = read_reg16(c, REG_BC); break;
                case 1: addr = read_reg16(c, REG_DE); break;
                case 2:
                    addr = read_reg16(c, REG_HL);
                    write_reg16(c, REG_HL, (uint16_t)(addr + 1));
                    break;
                default:
                    addr = read_reg16(c, REG_HL);
                    write_reg16(c, REG_HL, (uint16_t)(addr - 1));
                    break;
                }
                c->A = bus_read8(c->mbus, addr);
            }
            c->cycles += 8;
            return;

        case 3:
            if (q == 0) { // INC rp[p]
                uint16_t v = (uint16_t)(read_rp(c, p) + 1);
                write_rp(c, p, v);
            } else { // DEC rp[p]
                uint16_t v = (uint16_t)(read_rp(c, p) - 1);
                write_rp(c, p, v);
            }
            c->cycles += 8;
            return;

        case 4: { // INC r[y]
            uint8_t val = read_r8(c, y);
            val = inc8(c, val);
            write_r8(c, y, val);
            c->cycles += (y == 6) ? 12 : 4;
            return;
        }

        case 5: { // DEC r[y]
            uint8_t val = read_r8(c, y);
            val = dec8(c, val);
            write_r8(c, y, val);
            c->cycles += (y == 6) ? 12 : 4;
            return;
        }

        case 6: { // LD r[y], d8
            uint8_t imm = cpu_fetch8(c);
            write_r8(c, y, imm);
            c->cycles += (y == 6) ? 12 : 8;
            return;
        }

        default:
            switch (y) {
            case 0: { // RLCA
                bool carry = (c->A & 0x80u) != 0;
                c->A = (uint8_t)((c->A << 1) | (carry ? 1u : 0u));
                set_flag(c, FLAG_Z, false);
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, carry);
                c->cycles += 4;
                return;
            }
            case 1: { // RRCA
                bool carry = (c->A & 0x01u) != 0;
                c->A = (uint8_t)((c->A >> 1) | (carry ? 0x80u : 0u));
                set_flag(c, FLAG_Z, false);
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, carry);
                c->cycles += 4;
                return;
            }
            case 2: { // RLA
                uint8_t carry_in = get_flag(c, FLAG_C) ? 1u : 0u;
                bool carry_out = (c->A & 0x80u) != 0;
                c->A = (uint8_t)((c->A << 1) | carry_in);
                set_flag(c, FLAG_Z, false);
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, carry_out);
                c->cycles += 4;
                return;
            }
            case 3: { // RRA
                uint8_t carry_in = get_flag(c, FLAG_C) ? 0x80u : 0u;
                bool carry_out = (c->A & 0x01u) != 0;
                c->A = (uint8_t)((c->A >> 1) | carry_in);
                set_flag(c, FLAG_Z, false);
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, carry_out);
                c->cycles += 4;
                return;
            }
            case 4: // DAA
                daa(c);
                c->cycles += 4;
                return;
            case 5: // CPL
                c->A = (uint8_t)(~c->A);
                set_flag(c, FLAG_N, true);
                set_flag(c, FLAG_H, true);
                c->cycles += 4;
                return;
            case 6: // SCF
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, true);
                c->cycles += 4;
                return;
            default: // CCF
                set_flag(c, FLAG_N, false);
                set_flag(c, FLAG_H, false);
                set_flag(c, FLAG_C, !get_flag(c, FLAG_C));
                c->cycles += 4;
                return;
            }
        }

    case 1:
        if (opcode == 0x76) { // HALT
            c->halted = true;
            c->cycles += 4;
            return;
        }
        write_r8(c, y, read_r8(c, z)); // LD r[y], r[z]
        c->cycles += (y == 6 || z == 6) ? 8 : 4;
        return;

    case 2: { // ALU A, r[z]
        uint8_t val = read_r8(c, z);
        do_alu_a_r(c, y, val);
        c->cycles += (z == 6) ? 8 : 4;
        return;
    }

    default:
        switch (z) {
        case 0:
            if (y <= 3) { // RET cc
                if (condition_is_true(c, y)) {
                    c->PC = pop16(c);
                    c->cycles += 20;
                } else {
                    c->cycles += 8;
                }
                return;
            }
            if (y == 4) { // LDH (a8), A
                uint16_t addr = (uint16_t)(0xFF00u + cpu_fetch8(c));
                bus_write8(c->mbus, addr, c->A);
                c->cycles += 12;
                return;
            }
            if (y == 5) { // ADD SP, e8
                int8_t e8 = (int8_t)cpu_fetch8(c);
                c->SP = add_sp_e8(c, e8);
                c->cycles += 16;
                return;
            }
            if (y == 6) { // LDH A, (a8)
                uint16_t addr = (uint16_t)(0xFF00u + cpu_fetch8(c));
                c->A = bus_read8(c->mbus, addr);
                c->cycles += 12;
                return;
            }
            { // LD HL, SP+e8
                int8_t e8 = (int8_t)cpu_fetch8(c);
                write_reg16(c, REG_HL, add_sp_e8(c, e8));
                c->cycles += 12;
                return;
            }

        case 1:
            if (q == 0) { // POP rp2[p]
                write_rp2(c, p, pop16(c));
                c->cycles += 12;
                return;
            }
            switch (p) {
            case 0: // RET
                c->PC = pop16(c);
                c->cycles += 16;
                return;
            case 1: // RETI
                c->PC = pop16(c);
                c->ime = true;
                c->ime_pending = 0;
                c->cycles += 16;
                return;
            case 2: // JP HL
                c->PC = read_reg16(c, REG_HL);
                c->cycles += 4;
                return;
            default: // LD SP, HL
                c->SP = read_reg16(c, REG_HL);
                c->cycles += 8;
                return;
            }

        case 2:
            if (y <= 3) { // JP cc, a16
                uint16_t addr = cpu_fetch16(c);
                if (condition_is_true(c, y)) {
                    c->PC = addr;
                    c->cycles += 16;
                } else {
                    c->cycles += 12;
                }
                return;
            }
            if (y == 4) { // LD (FF00+C), A
                bus_write8(c->mbus, (uint16_t)(0xFF00u + c->C), c->A);
                c->cycles += 8;
                return;
            }
            if (y == 5) { // LD (a16), A
                uint16_t addr = cpu_fetch16(c);
                bus_write8(c->mbus, addr, c->A);
                c->cycles += 16;
                return;
            }
            if (y == 6) { // LD A, (FF00+C)
                c->A = bus_read8(c->mbus, (uint16_t)(0xFF00u + c->C));
                c->cycles += 8;
                return;
            }
            { // LD A, (a16)
                uint16_t addr = cpu_fetch16(c);
                c->A = bus_read8(c->mbus, addr);
                c->cycles += 16;
                return;
            }

        case 3:
            if (y == 0) { // JP a16
                c->PC = cpu_fetch16(c);
                c->cycles += 16;
                return;
            }
            if (y == 1) { // CB prefix
                uint8_t cbop = cpu_fetch8(c);
                execute_cb(c, cbop);
                return;
            }
            if (y == 6) { // DI
                c->ime = false;
                c->ime_pending = 0;
                c->cycles += 4;
                return;
            }
            if (y == 7) { // EI
                c->ime_pending = 2;
                c->cycles += 4;
                return;
            }
            op_illegal(c);
            return;

        case 4:
            if (y <= 3) { // CALL cc, a16
                uint16_t addr = cpu_fetch16(c);
                if (condition_is_true(c, y)) {
                    push16(c, c->PC);
                    c->PC = addr;
                    c->cycles += 24;
                } else {
                    c->cycles += 12;
                }
                return;
            }
            op_illegal(c);
            return;

        case 5:
            if (q == 0) { // PUSH rp2[p]
                push16(c, read_rp2(c, p));
                c->cycles += 16;
                return;
            }
            if (p == 0) { // CALL a16
                uint16_t addr = cpu_fetch16(c);
                push16(c, c->PC);
                c->PC = addr;
                c->cycles += 24;
                return;
            }
            op_illegal(c);
            return;

        case 6: { // ALU A, d8
            uint8_t imm = cpu_fetch8(c);
            do_alu_a_r(c, y, imm);
            c->cycles += 8;
            return;
        }

        default: // RST
            push16(c, c->PC);
            c->PC = (uint16_t)(y * 0x08);
            c->cycles += 16;
            return;
        }
    }
}

void opcode_init() {
    for (int i = 0; i < 256; i++) {
        snprintf(opcode_names[i], sizeof(opcode_names[i]), "OP%02X", i);
        opcodes[i].name = opcode_names[i];
        opcodes[i].handler = execute_base;
        opcodes[i].cycles = 0;
    }
}
