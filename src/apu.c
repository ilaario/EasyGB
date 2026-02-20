#include "include/apu.h"
#include "include/debug.h"

#ifdef EASYGB_USE_SDL
#include <SDL2/SDL.h>
#endif

#include <stdlib.h>
#include <string.h>

enum {
    GB_CPU_HZ = 4194304,
    APU_SAMPLE_RATE = 48000,
    APU_BATCH_SAMPLES = 512,
    FRAME_SEQ_PERIOD = 8192
};

#ifdef EASYGB_USE_SDL
typedef struct {
    uint8_t nrx0;
    uint8_t nrx1;
    uint8_t nrx2;
    uint8_t nrx3;
    uint8_t nrx4;

    bool has_sweep;
    bool enabled;
    bool dac_enabled;
    bool length_enable;
    bool env_add;
    bool sweep_negate;
    bool sweep_enabled;

    uint8_t duty_step;
    uint8_t length_counter;

    uint8_t volume;
    uint8_t env_period;
    uint8_t env_timer;

    uint16_t freq;
    uint16_t sweep_shadow;
    uint8_t sweep_period;
    uint8_t sweep_timer;
    uint8_t sweep_shift;

    uint32_t timer;
} square_channel;

typedef struct {
    uint8_t nr30;
    uint8_t nr31;
    uint8_t nr32;
    uint8_t nr33;
    uint8_t nr34;

    bool enabled;
    bool dac_enabled;
    bool length_enable;

    uint16_t length_counter;
    uint8_t pos;
    uint16_t freq;
    uint32_t timer;
} wave_channel;

typedef struct {
    uint8_t nr41;
    uint8_t nr42;
    uint8_t nr43;
    uint8_t nr44;

    bool enabled;
    bool dac_enabled;
    bool length_enable;
    bool env_add;

    uint8_t length_counter;
    uint8_t volume;
    uint8_t env_period;
    uint8_t env_timer;

    uint16_t lfsr;
    uint32_t timer;
} noise_channel;
#endif

struct APU {
    bus mbus;

#ifdef EASYGB_USE_SDL
    bool master_on;

    uint64_t sample_accum;
    uint32_t frame_seq_counter;
    uint8_t frame_seq_step;
    uint32_t io_seen[0x80];

    square_channel ch1;
    square_channel ch2;
    wave_channel ch3;
    noise_channel ch4;

    float hp_l_prev_in;
    float hp_r_prev_in;
    float hp_l_prev_out;
    float hp_r_prev_out;

    SDL_AudioDeviceID dev;
    bool audio_ready;
    float mixbuf[APU_BATCH_SAMPLES * 2];
    int mix_count;
#endif
};

#ifdef EASYGB_USE_SDL

static const uint8_t square_duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0}
};

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t square_period_cycles(uint16_t freq) {
    uint16_t f = (uint16_t)(freq & 0x07FFu);
    if (f >= 2048u) {
        return 8u;
    }
    {
        uint32_t p = (uint32_t)(2048u - f) * 4u;
        return p == 0u ? 8u : p;
    }
}

static inline uint32_t wave_period_cycles(uint16_t freq) {
    uint16_t f = (uint16_t)(freq & 0x07FFu);
    if (f >= 2048u) {
        return 2u;
    }
    {
        uint32_t p = (uint32_t)(2048u - f) * 2u;
        return p == 0u ? 2u : p;
    }
}

static inline uint32_t noise_period_cycles(uint8_t nr43) {
    static const uint16_t divisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    uint32_t base = divisors[nr43 & 0x07u];
    uint8_t shift = (uint8_t)((nr43 >> 4) & 0x0Fu);
    uint32_t p = base << shift;
    return p == 0u ? 8u : p;
}

static inline void square_update_freq(square_channel *ch) {
    ch->freq = (uint16_t)((((uint16_t)ch->nrx4 & 0x07u) << 8) | ch->nrx3);
}

static inline void square_update_dac(square_channel *ch) {
    ch->dac_enabled = (ch->nrx2 & 0xF8u) != 0u;
    if (!ch->dac_enabled) {
        ch->enabled = false;
    }
}

static inline void wave_update_freq(wave_channel *ch) {
    ch->freq = (uint16_t)((((uint16_t)ch->nr34 & 0x07u) << 8) | ch->nr33);
}

static inline void wave_update_dac(wave_channel *ch) {
    ch->dac_enabled = (ch->nr30 & 0x80u) != 0u;
    if (!ch->dac_enabled) {
        ch->enabled = false;
    }
}

static inline void noise_update_dac(noise_channel *ch) {
    ch->dac_enabled = (ch->nr42 & 0xF8u) != 0u;
    if (!ch->dac_enabled) {
        ch->enabled = false;
    }
}

static inline uint16_t square_sweep_calculate(square_channel *ch, bool *overflow) {
    uint16_t delta = (uint16_t)(ch->sweep_shadow >> ch->sweep_shift);
    int32_t next = ch->sweep_negate
        ? (int32_t)ch->sweep_shadow - (int32_t)delta
        : (int32_t)ch->sweep_shadow + (int32_t)delta;

    if (next < 0 || next > 2047) {
        *overflow = true;
        return ch->sweep_shadow;
    }

    *overflow = false;
    return (uint16_t)next;
}

static void square_trigger(square_channel *ch) {
    square_update_freq(ch);

    if (ch->length_counter == 0u) {
        ch->length_counter = 64u;
    }

    ch->timer = square_period_cycles(ch->freq);
    ch->duty_step = 0u;
    ch->enabled = ch->dac_enabled;

    ch->volume = (uint8_t)((ch->nrx2 >> 4) & 0x0Fu);
    ch->env_period = (uint8_t)(ch->nrx2 & 0x07u);
    ch->env_timer = ch->env_period == 0u ? 8u : ch->env_period;
    ch->env_add = (ch->nrx2 & 0x08u) != 0u;

    if (ch->has_sweep) {
        ch->sweep_shadow = ch->freq;
        ch->sweep_period = (uint8_t)((ch->nrx0 >> 4) & 0x07u);
        ch->sweep_shift = (uint8_t)(ch->nrx0 & 0x07u);
        ch->sweep_negate = (ch->nrx0 & 0x08u) != 0u;
        ch->sweep_timer = ch->sweep_period == 0u ? 8u : ch->sweep_period;
        ch->sweep_enabled = (ch->sweep_period != 0u) || (ch->sweep_shift != 0u);

        if (ch->sweep_shift != 0u) {
            bool overflow = false;
            (void)square_sweep_calculate(ch, &overflow);
            if (overflow) {
                ch->enabled = false;
            }
        }
    }
}

static void wave_trigger(wave_channel *ch) {
    if (ch->length_counter == 0u) {
        ch->length_counter = 256u;
    }

    wave_update_freq(ch);
    ch->timer = wave_period_cycles(ch->freq);
    ch->pos = 0u;
    ch->enabled = ch->dac_enabled;
}

static void noise_trigger(noise_channel *ch) {
    if (ch->length_counter == 0u) {
        ch->length_counter = 64u;
    }

    ch->timer = noise_period_cycles(ch->nr43);
    ch->lfsr = 0x7FFFu;
    ch->enabled = ch->dac_enabled;

    ch->volume = (uint8_t)((ch->nr42 >> 4) & 0x0Fu);
    ch->env_period = (uint8_t)(ch->nr42 & 0x07u);
    ch->env_timer = ch->env_period == 0u ? 8u : ch->env_period;
    ch->env_add = (ch->nr42 & 0x08u) != 0u;
}

static void square_clock_length(square_channel *ch) {
    if (ch->length_enable && ch->length_counter > 0u) {
        ch->length_counter--;
        if (ch->length_counter == 0u) {
            ch->enabled = false;
        }
    }
}

static void wave_clock_length(wave_channel *ch) {
    if (ch->length_enable && ch->length_counter > 0u) {
        ch->length_counter--;
        if (ch->length_counter == 0u) {
            ch->enabled = false;
        }
    }
}

static void noise_clock_length(noise_channel *ch) {
    if (ch->length_enable && ch->length_counter > 0u) {
        ch->length_counter--;
        if (ch->length_counter == 0u) {
            ch->enabled = false;
        }
    }
}

static void square_clock_envelope(square_channel *ch) {
    if (!ch->enabled || ch->env_period == 0u) {
        return;
    }

    if (ch->env_timer > 0u) {
        ch->env_timer--;
    }

    if (ch->env_timer == 0u) {
        ch->env_timer = ch->env_period == 0u ? 8u : ch->env_period;

        if (ch->env_add) {
            if (ch->volume < 15u) {
                ch->volume++;
            }
        } else {
            if (ch->volume > 0u) {
                ch->volume--;
            }
        }
    }
}

static void noise_clock_envelope(noise_channel *ch) {
    if (!ch->enabled || ch->env_period == 0u) {
        return;
    }

    if (ch->env_timer > 0u) {
        ch->env_timer--;
    }

    if (ch->env_timer == 0u) {
        ch->env_timer = ch->env_period == 0u ? 8u : ch->env_period;

        if (ch->env_add) {
            if (ch->volume < 15u) {
                ch->volume++;
            }
        } else {
            if (ch->volume > 0u) {
                ch->volume--;
            }
        }
    }
}

static void square_clock_sweep(square_channel *ch) {
    if (!ch->has_sweep || !ch->sweep_enabled) {
        return;
    }

    if (ch->sweep_timer > 0u) {
        ch->sweep_timer--;
    }

    if (ch->sweep_timer == 0u) {
        ch->sweep_timer = ch->sweep_period == 0u ? 8u : ch->sweep_period;

        if (ch->sweep_period != 0u) {
            bool overflow = false;
            uint16_t next = square_sweep_calculate(ch, &overflow);

            if (overflow) {
                ch->enabled = false;
                return;
            }

            if (ch->sweep_shift != 0u) {
                ch->sweep_shadow = next;
                ch->freq = next;

                (void)square_sweep_calculate(ch, &overflow);
                if (overflow) {
                    ch->enabled = false;
                }
            }
        }
    }
}

static void square_step_timer(square_channel *ch, int cycles) {
    if (!ch->enabled || cycles <= 0) {
        return;
    }

    if (ch->timer == 0u) {
        ch->timer = square_period_cycles(ch->freq);
    }

    while (cycles > 0) {
        uint32_t step = (uint32_t)cycles;
        if (step < ch->timer) {
            ch->timer -= step;
            break;
        }

        cycles -= (int)ch->timer;
        ch->timer = square_period_cycles(ch->freq);
        ch->duty_step = (uint8_t)((ch->duty_step + 1u) & 0x07u);
    }
}

static void wave_step_timer(wave_channel *ch, int cycles) {
    if (!ch->enabled || !ch->dac_enabled || cycles <= 0) {
        return;
    }

    if (ch->timer == 0u) {
        ch->timer = wave_period_cycles(ch->freq);
    }

    while (cycles > 0) {
        uint32_t step = (uint32_t)cycles;
        if (step < ch->timer) {
            ch->timer -= step;
            break;
        }

        cycles -= (int)ch->timer;
        ch->timer = wave_period_cycles(ch->freq);
        ch->pos = (uint8_t)((ch->pos + 1u) & 0x1Fu);
    }
}

static void noise_step_timer(noise_channel *ch, int cycles) {
    if (!ch->enabled || !ch->dac_enabled || cycles <= 0) {
        return;
    }

    if (ch->timer == 0u) {
        ch->timer = noise_period_cycles(ch->nr43);
    }

    while (cycles > 0) {
        uint32_t step = (uint32_t)cycles;
        if (step < ch->timer) {
            ch->timer -= step;
            break;
        }

        cycles -= (int)ch->timer;
        ch->timer = noise_period_cycles(ch->nr43);

        {
            uint16_t feedback = (uint16_t)((ch->lfsr ^ (ch->lfsr >> 1)) & 0x01u);
            ch->lfsr = (uint16_t)((ch->lfsr >> 1) | (feedback << 14));
            if ((ch->nr43 & 0x08u) != 0u) {
                ch->lfsr = (uint16_t)((ch->lfsr & ~(1u << 6)) | (feedback << 6));
            }
        }
    }
}

static void apu_frame_step(apu a, int cycles) {
    if (cycles <= 0) {
        return;
    }

    a->frame_seq_counter += (uint32_t)cycles;

    while (a->frame_seq_counter >= FRAME_SEQ_PERIOD) {
        a->frame_seq_counter -= FRAME_SEQ_PERIOD;
        a->frame_seq_step = (uint8_t)((a->frame_seq_step + 1u) & 0x07u);

        if ((a->frame_seq_step & 1u) == 0u) {
            square_clock_length(&a->ch1);
            square_clock_length(&a->ch2);
            wave_clock_length(&a->ch3);
            noise_clock_length(&a->ch4);
        }

        if (a->frame_seq_step == 2u || a->frame_seq_step == 6u) {
            square_clock_sweep(&a->ch1);
        }

        if (a->frame_seq_step == 7u) {
            square_clock_envelope(&a->ch1);
            square_clock_envelope(&a->ch2);
            noise_clock_envelope(&a->ch4);
        }
    }
}

static float square_output(const square_channel *ch) {
    if (!ch->enabled || !ch->dac_enabled || ch->volume == 0u) {
        return 0.0f;
    }

    {
        uint8_t duty = (uint8_t)((ch->nrx1 >> 6) & 0x03u);
        uint8_t out = square_duty_table[duty][ch->duty_step];
        float amp = out != 0u ? 1.0f : -1.0f;
        float vol = (float)ch->volume / 15.0f;
        return amp * vol;
    }
}

static float wave_output(apu a) {
    if (!a->ch3.enabled || !a->ch3.dac_enabled) {
        return 0.0f;
    }

    {
        uint8_t level_code = (uint8_t)((a->ch3.nr32 >> 5) & 0x03u);
        float level_mul = 0.0f;
        if (level_code == 1u) level_mul = 1.0f;
        else if (level_code == 2u) level_mul = 0.5f;
        else if (level_code == 3u) level_mul = 0.25f;

        if (level_mul == 0.0f) {
            return 0.0f;
        }

        uint8_t pos = (uint8_t)(a->ch3.pos & 0x1Fu);
        uint8_t wave_byte = bus_read8(a->mbus, (uint16_t)(0xFF30u + (pos >> 1)));
        uint8_t sample4 = (pos & 1u) == 0u ? (uint8_t)(wave_byte >> 4) : (uint8_t)(wave_byte & 0x0Fu);

        {
            float base = ((float)sample4 / 15.0f) * 2.0f - 1.0f;
            return base * level_mul;
        }
    }
}

static float noise_output(const noise_channel *ch) {
    if (!ch->enabled || !ch->dac_enabled || ch->volume == 0u) {
        return 0.0f;
    }

    {
        float amp = (ch->lfsr & 0x01u) == 0u ? 1.0f : -1.0f;
        float vol = (float)ch->volume / 15.0f;
        return amp * vol;
    }
}

static void apu_mix_sample(apu a, float *left, float *right) {
    if (!a->master_on) {
        *left = 0.0f;
        *right = 0.0f;
        return;
    }

    {
        uint8_t nr50 = bus_read8(a->mbus, 0xFF24);
        uint8_t nr51 = bus_read8(a->mbus, 0xFF25);

        float c1 = square_output(&a->ch1);
        float c2 = square_output(&a->ch2);
        float c3 = wave_output(a);
        float c4 = noise_output(&a->ch4);

        float l = 0.0f;
        float r = 0.0f;

        if ((nr51 & 0x10u) != 0u) l += c1;
        if ((nr51 & 0x20u) != 0u) l += c2;
        if ((nr51 & 0x40u) != 0u) l += c3;
        if ((nr51 & 0x80u) != 0u) l += c4;

        if ((nr51 & 0x01u) != 0u) r += c1;
        if ((nr51 & 0x02u) != 0u) r += c2;
        if ((nr51 & 0x04u) != 0u) r += c3;
        if ((nr51 & 0x08u) != 0u) r += c4;

        {
            float lvol = (float)(((nr50 >> 4) & 0x07u) + 1u) / 8.0f;
            float rvol = (float)((nr50 & 0x07u) + 1u) / 8.0f;
            const float master_gain = 0.22f;
            l *= lvol * master_gain;
            r *= rvol * master_gain;
        }

        {
            const float hp_r = 0.996f;
            float out_l = l - a->hp_l_prev_in + hp_r * a->hp_l_prev_out;
            float out_r = r - a->hp_r_prev_in + hp_r * a->hp_r_prev_out;
            a->hp_l_prev_in = l;
            a->hp_r_prev_in = r;
            a->hp_l_prev_out = out_l;
            a->hp_r_prev_out = out_r;
            l = out_l;
            r = out_r;
        }

        *left = clampf(l, -1.0f, 1.0f);
        *right = clampf(r, -1.0f, 1.0f);
    }
}

static void apu_reset_runtime(apu a) {
    memset(&a->ch1, 0, sizeof(a->ch1));
    memset(&a->ch2, 0, sizeof(a->ch2));
    memset(&a->ch3, 0, sizeof(a->ch3));
    memset(&a->ch4, 0, sizeof(a->ch4));

    a->ch1.has_sweep = true;
    a->ch2.has_sweep = false;
    a->ch4.lfsr = 0x7FFFu;

    a->frame_seq_counter = 0u;
    a->frame_seq_step = 0u;

    a->hp_l_prev_in = 0.0f;
    a->hp_r_prev_in = 0.0f;
    a->hp_l_prev_out = 0.0f;
    a->hp_r_prev_out = 0.0f;
}

static void apu_sync_regs_from_bus(apu a) {
    a->ch1.nrx0 = bus_read8(a->mbus, 0xFF10);
    a->ch1.nrx1 = bus_read8(a->mbus, 0xFF11);
    a->ch1.nrx2 = bus_read8(a->mbus, 0xFF12);
    a->ch1.nrx3 = bus_read8(a->mbus, 0xFF13);
    a->ch1.nrx4 = bus_read8(a->mbus, 0xFF14);
    a->ch1.length_counter = (uint8_t)(64u - (a->ch1.nrx1 & 0x3Fu));
    a->ch1.length_enable = (a->ch1.nrx4 & 0x40u) != 0u;
    square_update_freq(&a->ch1);
    square_update_dac(&a->ch1);

    a->ch2.nrx1 = bus_read8(a->mbus, 0xFF16);
    a->ch2.nrx2 = bus_read8(a->mbus, 0xFF17);
    a->ch2.nrx3 = bus_read8(a->mbus, 0xFF18);
    a->ch2.nrx4 = bus_read8(a->mbus, 0xFF19);
    a->ch2.length_counter = (uint8_t)(64u - (a->ch2.nrx1 & 0x3Fu));
    a->ch2.length_enable = (a->ch2.nrx4 & 0x40u) != 0u;
    square_update_freq(&a->ch2);
    square_update_dac(&a->ch2);

    a->ch3.nr30 = bus_read8(a->mbus, 0xFF1A);
    a->ch3.nr31 = bus_read8(a->mbus, 0xFF1B);
    a->ch3.nr32 = bus_read8(a->mbus, 0xFF1C);
    a->ch3.nr33 = bus_read8(a->mbus, 0xFF1D);
    a->ch3.nr34 = bus_read8(a->mbus, 0xFF1E);
    a->ch3.length_counter = (uint16_t)(256u - a->ch3.nr31);
    a->ch3.length_enable = (a->ch3.nr34 & 0x40u) != 0u;
    wave_update_freq(&a->ch3);
    wave_update_dac(&a->ch3);

    a->ch4.nr41 = bus_read8(a->mbus, 0xFF20);
    a->ch4.nr42 = bus_read8(a->mbus, 0xFF21);
    a->ch4.nr43 = bus_read8(a->mbus, 0xFF22);
    a->ch4.nr44 = bus_read8(a->mbus, 0xFF23);
    a->ch4.length_counter = (uint8_t)(64u - (a->ch4.nr41 & 0x3Fu));
    a->ch4.length_enable = (a->ch4.nr44 & 0x40u) != 0u;
    noise_update_dac(&a->ch4);
}

static void apu_power_on(apu a) {
    apu_reset_runtime(a);
    a->master_on = true;
    apu_sync_regs_from_bus(a);
}

static void apu_power_off(apu a) {
    apu_reset_runtime(a);
    a->master_on = false;

    if (a->audio_ready && a->dev != 0) {
        SDL_ClearQueuedAudio(a->dev);
        a->mix_count = 0;
    }
}

static void apu_on_write(apu a, uint16_t addr, uint8_t val) {
    if (addr == 0xFF26u) {
        bool want_on = (val & 0x80u) != 0u;
        if (!want_on && a->master_on) {
            apu_power_off(a);
        } else if (want_on && !a->master_on) {
            apu_power_on(a);
        }
        return;
    }

    if (!a->master_on) {
        return;
    }

    switch (addr) {
    case 0xFF10:
        a->ch1.nrx0 = val;
        a->ch1.sweep_period = (uint8_t)((val >> 4) & 0x07u);
        a->ch1.sweep_shift = (uint8_t)(val & 0x07u);
        a->ch1.sweep_negate = (val & 0x08u) != 0u;
        break;
    case 0xFF11:
        a->ch1.nrx1 = val;
        a->ch1.length_counter = (uint8_t)(64u - (val & 0x3Fu));
        break;
    case 0xFF12:
        a->ch1.nrx2 = val;
        square_update_dac(&a->ch1);
        break;
    case 0xFF13:
        a->ch1.nrx3 = val;
        square_update_freq(&a->ch1);
        break;
    case 0xFF14:
        a->ch1.nrx4 = val;
        a->ch1.length_enable = (val & 0x40u) != 0u;
        square_update_freq(&a->ch1);
        if ((val & 0x80u) != 0u) {
            square_trigger(&a->ch1);
        }
        break;

    case 0xFF16:
        a->ch2.nrx1 = val;
        a->ch2.length_counter = (uint8_t)(64u - (val & 0x3Fu));
        break;
    case 0xFF17:
        a->ch2.nrx2 = val;
        square_update_dac(&a->ch2);
        break;
    case 0xFF18:
        a->ch2.nrx3 = val;
        square_update_freq(&a->ch2);
        break;
    case 0xFF19:
        a->ch2.nrx4 = val;
        a->ch2.length_enable = (val & 0x40u) != 0u;
        square_update_freq(&a->ch2);
        if ((val & 0x80u) != 0u) {
            square_trigger(&a->ch2);
        }
        break;

    case 0xFF1A:
        a->ch3.nr30 = val;
        wave_update_dac(&a->ch3);
        break;
    case 0xFF1B:
        a->ch3.nr31 = val;
        a->ch3.length_counter = (uint16_t)(256u - val);
        break;
    case 0xFF1C:
        a->ch3.nr32 = val;
        break;
    case 0xFF1D:
        a->ch3.nr33 = val;
        wave_update_freq(&a->ch3);
        break;
    case 0xFF1E:
        a->ch3.nr34 = val;
        a->ch3.length_enable = (val & 0x40u) != 0u;
        wave_update_freq(&a->ch3);
        if ((val & 0x80u) != 0u) {
            wave_trigger(&a->ch3);
        }
        break;

    case 0xFF20:
        a->ch4.nr41 = val;
        a->ch4.length_counter = (uint8_t)(64u - (val & 0x3Fu));
        break;
    case 0xFF21:
        a->ch4.nr42 = val;
        noise_update_dac(&a->ch4);
        break;
    case 0xFF22:
        a->ch4.nr43 = val;
        break;
    case 0xFF23:
        a->ch4.nr44 = val;
        a->ch4.length_enable = (val & 0x40u) != 0u;
        if ((val & 0x80u) != 0u) {
            noise_trigger(&a->ch4);
        }
        break;

    default:
        break;
    }
}

static void apu_process_io_writes(apu a) {
    uint16_t addr;
    for (addr = 0xFF10u; addr <= 0xFF3Fu; addr++) {
        uint8_t idx = (uint8_t)(addr - 0xFF00u);
        uint32_t serial = bus_get_io_write_serial(a->mbus, addr);
        if (serial != a->io_seen[idx]) {
            a->io_seen[idx] = serial;
            apu_on_write(a, addr, bus_read8(a->mbus, addr));
        }
    }
}

static void apu_step_counters(apu a, int cpu_cycles) {
    if (!a->master_on || cpu_cycles <= 0) {
        return;
    }

    apu_frame_step(a, cpu_cycles);
    square_step_timer(&a->ch1, cpu_cycles);
    square_step_timer(&a->ch2, cpu_cycles);
    wave_step_timer(&a->ch3, cpu_cycles);
    noise_step_timer(&a->ch4, cpu_cycles);
}

#endif

apu apu_init(bus b) {
    apu a = (apu)calloc(1, sizeof(struct APU));
    if (a == NULL) {
        return NULL;
    }

    a->mbus = b;

#ifdef EASYGB_USE_SDL
    a->master_on = (bus_read8(b, 0xFF26) & 0x80u) != 0u;
    apu_reset_runtime(a);
    if (a->master_on) {
        apu_sync_regs_from_bus(a);
    }

    {
        int i;
        for (i = 0; i < 0x80; i++) {
            a->io_seen[i] = bus_get_io_write_serial(b, (uint16_t)(0xFF00u + (uint16_t)i));
        }
    }

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0u) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            dbg_log("APU: SDL audio init failed: %s", SDL_GetError());
            return a;
        }
    }

    {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = APU_SAMPLE_RATE;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = NULL;

        a->dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (a->dev == 0) {
            dbg_log("APU: SDL_OpenAudioDevice failed: %s", SDL_GetError());
            return a;
        }
    }

    SDL_PauseAudioDevice(a->dev, 0);
    a->audio_ready = true;
    dbg_log("APU init complete");
#else
    (void)b;
#endif

    return a;
}

void apu_destroy(apu a) {
    if (a == NULL) {
        return;
    }

#ifdef EASYGB_USE_SDL
    if (a->audio_ready && a->dev != 0) {
        if (a->mix_count > 0) {
            SDL_QueueAudio(a->dev, a->mixbuf, (uint32_t)(a->mix_count * 2 * (int)sizeof(float)));
            a->mix_count = 0;
        }
        SDL_CloseAudioDevice(a->dev);
    }
#endif

    free(a);
}

void apu_step(apu a, int cpu_cycles) {
    if (a == NULL || cpu_cycles <= 0) {
        return;
    }

#ifdef EASYGB_USE_SDL
    if (!a->audio_ready || a->dev == 0) {
        return;
    }

    apu_process_io_writes(a);
    apu_step_counters(a, cpu_cycles);

    a->sample_accum += (uint64_t)cpu_cycles * (uint64_t)APU_SAMPLE_RATE;

    while (a->sample_accum >= (uint64_t)GB_CPU_HZ) {
        float left;
        float right;

        a->sample_accum -= (uint64_t)GB_CPU_HZ;

        apu_mix_sample(a, &left, &right);
        a->mixbuf[a->mix_count * 2] = left;
        a->mixbuf[a->mix_count * 2 + 1] = right;
        a->mix_count++;

        if (a->mix_count >= APU_BATCH_SAMPLES) {
            uint32_t queued = SDL_GetQueuedAudioSize(a->dev);
            uint32_t hard_limit = (uint32_t)(APU_SAMPLE_RATE * 2 * (int)sizeof(float) / 2); // ~500ms
            if (queued > hard_limit) {
                SDL_ClearQueuedAudio(a->dev);
            }
            SDL_QueueAudio(a->dev, a->mixbuf, (uint32_t)(a->mix_count * 2 * (int)sizeof(float)));
            a->mix_count = 0;
        }
    }
#else
    (void)a;
#endif
}
