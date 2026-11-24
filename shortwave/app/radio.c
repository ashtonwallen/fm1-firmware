/* SPDX-License-Identifier: MPL-2.0 */
#include "radio.h"
#include "fastmath.h"

#define SAMPLE_RATE 44100u
#define CONTROL_FRAMES 128u
#define SINE_SIZE 512u
#define DIAL_MAX 1000
#define PICKUP_WIDTH 205
#define ECHO_SIZE 4608u
#define ECHO_LEFT 3163u
#define ECHO_RIGHT 4519u

typedef struct {
    uint8_t chords[4][2];
    uint8_t melody[8];
    float pad_color;
    float pad_level;
    float note_ratio;
    float note_color;
    float note_level;
    uint16_t chord_ms;
    uint16_t gap_ms;
    uint16_t tail_ms;
} station_voices_t;

/* All four stations borrow from D minor so two can share the dial. */
static const station_voices_t voices[RADIO_STATIONS] = {
    { {{38, 45}, {38, 48}, {43, 50}, {38, 45}}, {62, 69, 64, 65, 62, 67, 60, 69},
      0.08f, 0.072f, 1.0f, 0.07f, 0.073f, 19200, 2950, 2720 },  /* Deep Field */
    { {{50, 57}, {50, 60}, {45, 57}, {48, 55}}, {74, 81, 76, 79, 84, 74, 77, 81},
      0.03f, 0.055f, 2.01f, 0.14f, 0.104f, 16300, 1970, 1410 },  /* Glass FM */
    { {{45, 53}, {48, 55}, {43, 58}, {50, 57}}, {65, 72, 69, 67, 64, 62, 70, 65},
      0.13f, 0.065f, 1.0f, 0.20f, 0.095f, 12100, 2180, 1520 },  /* Night Service */
    { {{50, 55}, {48, 57}, {46, 53}, {50, 57}}, {69, 72, 74, 67, 76, 77, 74, 70},
      0.24f, 0.063f, 3.0f, 0.09f, 0.088f, 22500, 2430, 2020 },  /* Soft Relay */
};

typedef struct {
    float pad_phase[2];
    float pad_hz[2];
    float pad_target[2];
    float note_phase;
    float mod_phase;
    float note_hz;
    float note_pan;
    float note_age;
    float note_speed;
    float flutter_phase;
    float mix;
    uint32_t note_frames;
    uint32_t chord_frames;
    uint32_t rng;
    unsigned chord;
    unsigned next_note;
} station_state_t;

typedef struct {
    float mix;
    float pad_rate[2];
    float note_rate;
    float mod_rate;
    float pan;
} tuned_station_t;

static station_state_t stations[RADIO_STATIONS];
static float wave[SINE_SIZE + 1];
static int16_t echo[ECHO_SIZE];
static unsigned echo_pos, echo_phase;
static float echo_send, echo_l, echo_r;
static float hiss_lp, hiss_slow, hiss_gain;
static float receiver_l, receiver_r, volume;
static uint32_t hiss_rng;

/* Publish one aligned word from the UI. The audio interrupt sees every
 * control from the same frame, even if it preempts the panel update. */
static volatile uint32_t controls;
static volatile uint32_t level;

static int clamp_int(int value, int high)
{
    return value < 0 ? 0 : value > high ? high : value;
}

int radio_station_center(int station)
{
    return 80 + 280 * clamp_int(station, RADIO_STATIONS - 1);
}

int radio_signal(int dial, int station)
{
    const int center = radio_station_center(station);
    const int offset = clamp_int(dial, DIAL_MAX) - center;
    const int distance = offset < 0 ? -offset : offset;
    if (distance >= PICKUP_WIDTH)
        return 0;
    const int t = (PICKUP_WIDTH - distance) * 1024 / PICKUP_WIDTH;
    const int smooth = (t * t * (3072 - 2 * t)) >> 20;
    return (smooth * DIAL_MAX + 512) >> 10;
}

int radio_level(void)
{
    return (int)level;
}

void radio_controls(int dial, int noise, int motion, int playback)
{
    controls = (uint32_t)clamp_int(dial, DIAL_MAX)
             | (uint32_t)clamp_int(noise, 100) << 10
             | (uint32_t)clamp_int(motion, 100) << 17
             | (uint32_t)(playback != 0) << 24;
}

static uint32_t random_word(uint32_t *state)
{
    uint32_t r = *state;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    *state = r;
    return r;
}

static float pitch(unsigned note)
{
    return 440.0f * fm_exp2f(((float)note - 69.0f) * (1.0f / 12.0f));
}

static uint32_t frames(unsigned ms)
{
    return ms * 441u / 10u;
}

static inline float osc(float phase)
{
    const float x = (phase - (float)(unsigned)phase) * (float)SINE_SIZE;
    const unsigned i = (unsigned)x;
    return wave[i] + (x - (float)i) * (wave[i + 1] - wave[i]);
}

static inline void advance(float *phase, float rate)
{
    float next = *phase + rate;
    *phase = next >= 1.0f ? next - 1.0f : next;
}

static uint32_t chord(station_state_t *state, const station_voices_t *bank)
{
    state->pad_target[0] = pitch(bank->chords[state->chord][0]);
    state->pad_target[1] = pitch(bank->chords[state->chord][1]);
    return frames(bank->chord_ms + (random_word(&state->rng) >> 21));
}

static void note(station_state_t *state, const station_voices_t *bank)
{
    const uint32_t movement = random_word(&state->rng);
    unsigned index = state->next_note;
    if ((movement & 7u) == 0)
        index = (index + 1) & 7u;
    state->next_note = (index + 1) & 7u;
    state->note_hz = pitch(bank->melody[index]);
    state->note_age = 0.0f;
    state->note_speed = 1.0f / (float)frames(bank->tail_ms);
    state->note_pan = ((float)((movement >> 8) & 255u) - 127.5f) * (0.30f / 127.5f);
    state->note_frames = frames(bank->gap_ms + (movement >> 21));
}

void radio_init(void)
{
    for (unsigned i = 0; i < SINE_SIZE; i++)
        wave[i] = fm_sin_turns((float)i * (1.0f / (float)SINE_SIZE));
    wave[SINE_SIZE] = wave[0];
    for (unsigned i = 0; i < ECHO_SIZE; i++)
        echo[i] = 0;
    for (unsigned i = 0; i < RADIO_STATIONS; i++) {
        station_state_t state = {.rng = 0x4EF68F41u ^ (0x4F7C43C5u * i)};
        state.flutter_phase = 0.25f * (float)i;
        state.chord_frames = chord(&state, &voices[i]);
        state.pad_hz[0] = state.pad_target[0];
        state.pad_hz[1] = state.pad_target[1];
        state.note_age = 1.0f;
        state.note_frames = frames(330u + 577u * i);
        stations[i] = state;
    }
    echo_pos = 0;
    echo_phase = 0;
    echo_send = 0.0f;
    echo_l = 0.0f;
    echo_r = 0.0f;
    hiss_lp = 0.0f;
    hiss_slow = 0.0f;
    hiss_gain = 0.0f;
    receiver_l = 0.0f;
    receiver_r = 0.0f;
    volume = 0.0f;
    hiss_rng = 0x8E87A855u;
    level = 0;
    radio_controls(80, 24, 50, 1);
}

static tuned_station_t tune(unsigned station, float signal, float motion, uint32_t count)
{
    station_state_t *s = &stations[station];
    const station_voices_t *bank = &voices[station];
    if (s->chord_frames <= count) {
        s->chord = (s->chord + 1) & 3u;
        s->chord_frames += chord(s, bank);
    }
    s->chord_frames -= count;
    if (s->note_frames <= count)
        note(s, bank);
    s->note_frames -= count;

    const float sample_step = (float)count * (1.0f / (float)SAMPLE_RATE);
    advance(&s->flutter_phase, sample_step * (0.17f + 0.053f * (float)station));
    const float a = osc(s->flutter_phase);
    const float b = osc(s->flutter_phase * 3.0f + 0.18f);
    const float weak = 1.0f - signal;
    const float wow = 1.0f + a * (0.0012f + 0.0055f * weak) * motion
                           + b * (0.0018f * weak);
    for (unsigned p = 0; p < 2u; p++)
        s->pad_hz[p] += (s->pad_target[p] - s->pad_hz[p]) * sample_step * 0.90f;

    tuned_station_t t;
    t.mix = signal * (1.0f - weak * (0.08f + 0.17f * (0.5f + a * 0.5f)));
    t.pad_rate[0] = s->pad_hz[0] * wow * (1.0f / (float)SAMPLE_RATE);
    t.pad_rate[1] = s->pad_hz[1] * (2.0015f - wow) * (1.0f / (float)SAMPLE_RATE);
    t.note_rate = s->note_hz * wow * (1.0f / (float)SAMPLE_RATE);
    t.mod_rate = t.note_rate * bank->note_ratio;
    t.pan = s->note_pan;
    return t;
}

static inline void voice(unsigned station, const tuned_station_t *t, float *l, float *r)
{
    station_state_t *s = &stations[station];
    const station_voices_t *bank = &voices[station];
    s->mix += (t->mix - s->mix) * 0.0016f;
    const float a = osc(s->pad_phase[0]) + bank->pad_color * osc(2.0f * s->pad_phase[0]);
    const float b = osc(s->pad_phase[1]) + bank->pad_color * osc(3.0f * s->pad_phase[1]);
    advance(&s->pad_phase[0], t->pad_rate[0]);
    advance(&s->pad_phase[1], t->pad_rate[1]);
    float lead = 0.0f;
    if (s->note_age < 1.0f) {
        const float tail = 1.0f - s->note_age;
        const float attack = fm_minf(s->note_age * 45.0f, 1.0f);
        const float color = bank->note_color * tail * osc(s->mod_phase);
        lead = osc(1.0f + s->note_phase + color) * tail * tail * attack * bank->note_level;
        advance(&s->note_phase, t->note_rate);
        advance(&s->mod_phase, t->mod_rate);
        s->note_age += s->note_speed;
    }
    *l += s->mix * (bank->pad_level * (a + b * 0.63f) + lead * (1.0f - t->pan));
    *r += s->mix * (bank->pad_level * (a * 0.63f + b) + lead * (1.0f + t->pan));
}

static unsigned delay_pos(unsigned frames_ago)
{
    return echo_pos >= frames_ago ? echo_pos - frames_ago : echo_pos + ECHO_SIZE - frames_ago;
}

static void delays(float dry_l, float dry_r)
{
    echo_send += (0.5f * (dry_l + dry_r) - echo_send) * 0.19f;
    if (++echo_phase == 4u) {
        echo_phase = 0;
        const float wet_l = (float)echo[delay_pos(ECHO_LEFT)] * (1.0f / 32768.0f);
        const float wet_r = (float)echo[delay_pos(ECHO_RIGHT)] * (1.0f / 32768.0f);
        echo_l += (wet_l - echo_l) * 0.65f;
        echo_r += (wet_r - echo_r) * 0.65f;
        const float send = echo_send + 0.19f * echo_l + 0.19f * echo_r;
        echo[echo_pos] = (int16_t)(fm_clampf(send, -0.99f, 0.99f) * 32768.0f);
        if (++echo_pos == ECHO_SIZE)
            echo_pos = 0;
    }
}

static void block(int32_t *lr, uint32_t count, uint32_t settings, float master)
{
    const int dial = (int)(settings & 1023u);
    const float noise = (float)((settings >> 10) & 127u) * 0.01f;
    const float motion = (float)((settings >> 17) & 127u) * 0.01f;
    const float target_volume = (settings & (1u << 24)) ? master : 0.0f;
    tuned_station_t tuned[RADIO_STATIONS];
    unsigned active[RADIO_STATIONS];
    unsigned num_active = 0;
    float strength = 0.0f;
    for (unsigned i = 0; i < RADIO_STATIONS; i++) {
        const float signal = (float)radio_signal(dial, (int)i) * 0.001f;
        strength = fm_maxf(strength, signal);
        tuned[i] = tune(i, signal, motion, count);
        if (tuned[i].mix > 0.0001f || stations[i].mix > 0.0001f) {
            active[num_active++] = i;
        } else {
            stations[i].mix = 0.0f;
            stations[i].note_age = fm_minf(1.0f, stations[i].note_age + stations[i].note_speed * (float)count);
        }
    }
    const float weak = 1.0f - strength;
    const float brightness = 0.15f + 0.64f * strength;
    const float static_level = noise * (0.003f + 0.095f * weak * weak);
    float peak = 0.0f;

    for (uint32_t j = 0; j < count; j++) {
        float l = 0.0f;
        float r = 0.0f;
        for (unsigned p = 0; p < num_active; p++) {
            const unsigned i = active[p];
            voice(i, &tuned[i], &l, &r);
        }
        receiver_l += (l - receiver_l) * brightness;
        receiver_r += (r - receiver_r) * brightness;
        delays(receiver_l, receiver_r);

        const float raw = (float)(random_word(&hiss_rng) >> 16) * (1.0f / 32768.0f) - 1.0f;
        hiss_lp += (raw - hiss_lp) * 0.22f;
        hiss_slow += (hiss_lp - hiss_slow) * 0.003f;
        hiss_gain += (static_level - hiss_gain) * 0.0017f;
        const float static_sample = (hiss_lp - hiss_slow) * hiss_gain;
        l = receiver_l + 0.23f * echo_l + static_sample;
        r = receiver_r + 0.23f * echo_r + 0.94f * static_sample;

        volume += (target_volume - volume) * 0.0018f;
        if (target_volume == 0.0f && volume < 0.00001f)
            volume = 0.0f;
        l = fm_clampf(l * volume, -0.98f, 0.98f);
        r = fm_clampf(r * volume, -0.98f, 0.98f);
        lr[j * 2u] = (int32_t)(l * 8300000.0f);
        lr[j * 2u + 1u] = (int32_t)(r * 8300000.0f);
        if ((j & 3u) == 0)
            peak = fm_maxf(peak, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
    }
    level = (uint32_t)fm_maxf(peak * 1000.0f, (float)level - (float)count * (1.0f / 192.0f));
}

void radio_render(int32_t *lr, uint32_t count, uint32_t gain_q12)
{
    const uint32_t settings = controls;
    const float master = (float)gain_q12 * (1.0f / 4096.0f);
    while (count) {
        const uint32_t n = count > CONTROL_FRAMES ? CONTROL_FRAMES : count;
        block(lr, n, settings, master);
        lr += n * 2u;
        count -= n;
    }
}
