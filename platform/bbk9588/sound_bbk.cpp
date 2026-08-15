#include "bda_audio.h"
#include "comm.h"
#include "dsp/dsp.h"
#include "state.h"

#include <stdint.h>
#include <string.h>

extern nc2k_states_t nc2k_states;

Dsp dsp;

extern "C" volatile uint32_t bbk_dsp_write_count;
extern "C" volatile uint32_t bbk_dsp_generated_samples;
extern "C" volatile uint32_t bbk_dsp_last_write;
volatile uint32_t bbk_dsp_write_count;
volatile uint32_t bbk_dsp_generated_samples;
volatile uint32_t bbk_dsp_last_write;

static const uint32_t output_rate = BDA_AUDIO_SAMPLE_RATE_22050;
static const uint32_t ring_samples = 4096u;
static const uint32_t block_samples = 512u;
static const uint32_t dsp_ring_samples = 16384u;
static const uint32_t dsp_busy_samples = 5000u;
static const uint32_t word_prebuffer_samples = 640u;
static const uint32_t dsp_phase_denominator = 441u;
static const uint32_t dsp_phase_increment = 160u;
static int16_t audio_ring[ring_samples];
static int16_t silence_block[block_samples];
static int16_t dsp_input_ring[dsp_ring_samples];
static int16_t dsp_playback_ring[dsp_ring_samples];
static uint32_t read_index;
static uint32_t write_index;
static uint32_t queued_samples;
static uint32_t dsp_input_read;
static uint32_t dsp_input_write;
static uint32_t dsp_input_count;
static uint32_t dsp_playback_read;
static uint32_t dsp_playback_write;
static uint32_t dsp_playback_count;
static uint32_t dsp_phase;
static int32_t dsp_sample_0;
static int32_t dsp_sample_1;
static uint64_t last_audio_cycle;
static uint32_t audio_cycle_fraction;
static uint32_t audio_cycle_numerator;
static uint32_t audio_cycle_denominator;
static uint32_t speech_hold_samples;
static int word_job_observed;
static int word_prebuffering;
static int audio_clock_started;
static int beeper_level;
static int32_t filter_input;
static int32_t filter_output;
static int audio_open;
static int original_attenuation;

static int16_t filter_beeper(int32_t input)
{
    int32_t output = input - filter_input + ((filter_output * 32440) >> 15);
    filter_input = input;
    filter_output = output;
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    return (int16_t)output;
}

static void queue_sample(int16_t sample)
{
    if (queued_samples == ring_samples) {
        read_index = (read_index + 1u) & (ring_samples - 1u);
        --queued_samples;
    }
    audio_ring[write_index] = sample;
    write_index = (write_index + 1u) & (ring_samples - 1u);
    ++queued_samples;
}

static int16_t clamp_sample(int32_t sample)
{
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return (int16_t)sample;
}

static void dsp_callback(unsigned char *data, int length)
{
    uint32_t samples = length > 0 ? (uint32_t)length / 2u : 0u;
    bbk_dsp_generated_samples += samples;
    while (samples && dsp_input_count < dsp_ring_samples) {
        uint32_t free_samples = dsp_ring_samples - dsp_input_count;
        uint32_t contiguous = dsp_ring_samples - dsp_input_write;
        uint32_t count = samples;
        if (count > free_samples) count = free_samples;
        if (count > contiguous) count = contiguous;
        memcpy(&dsp_input_ring[dsp_input_write], data,
               count * sizeof(int16_t));
        data += count * sizeof(int16_t);
        samples -= count;
        dsp_input_write = (dsp_input_write + count) & (dsp_ring_samples - 1u);
        dsp_input_count += count;
    }
    speech_hold_samples = output_rate;
}

#ifdef NC2000_DSP_SELF_TEST
static void queue_dsp_self_test()
{
    // Exercise the same DSP callback, FIFO and resampler used by ROM speech.
    // Mode 4 is signed 16-bit PCM represented as unsigned samples on the DSP bus.
    dsp.write(0xa0, 4);
    for (uint32_t i = 0; i < DSP_AUDIO_HZ; ++i) {
        uint32_t phase = i % 80u;
        int32_t sample = phase < 40u
            ? -12000 + (int32_t)phase * 600
            : 12000 - (int32_t)(phase - 40u) * 600;
        uint16_t encoded = (uint16_t)(sample + 0x8000);
        dsp.write((uint8_t)(encoded >> 8), (uint8_t)encoded);
    }
    dsp.write(0xff, 0);
}
#endif

static int16_t resample_dsp()
{
    dsp_phase += dsp_phase_increment;
    if (dsp_phase >= dsp_phase_denominator) {
        dsp_phase -= dsp_phase_denominator;
        dsp_sample_0 = dsp_sample_1;
        if (dsp_playback_count) {
            dsp_sample_1 = dsp_playback_ring[dsp_playback_read];
            dsp_playback_read = (dsp_playback_read + 1u) & (dsp_ring_samples - 1u);
            --dsp_playback_count;
        } else {
            dsp_sample_1 = 0;
        }
    }
    int32_t delta = dsp_sample_1 - dsp_sample_0;
    if (dsp_sample_0 || dsp_sample_1) speech_hold_samples = output_rate;
    else if (speech_hold_samples) --speech_hold_samples;
    // 8000/22050 reduces exactly to 160/441.  The constant divisor avoids
    // the target's costly general-purpose division helper.
    return (int16_t)(dsp_sample_0 + delta * (int32_t)dsp_phase /
                     (int32_t)dsp_phase_denominator);
}

static void update_beeper(int next_level)
{
    uint64_t current_cycle = nc2k_states.cycles;
    if (!audio_clock_started || current_cycle < last_audio_cycle) {
        last_audio_cycle = current_cycle;
        audio_cycle_fraction = 0u;
        audio_clock_started = 1;
        beeper_level = next_level != 0;
        return;
    }
    uint64_t elapsed64 = current_cycle - last_audio_cycle;
    last_audio_cycle = current_cycle;
    if (elapsed64 > CYCLES_SECOND) {
        // A loaded/reset state must not synthesize seconds of stale audio.
        audio_cycle_fraction = 0u;
        beeper_level = next_level != 0;
        return;
    }
    uint32_t elapsed = (uint32_t)elapsed64;
    audio_cycle_fraction += elapsed * audio_cycle_numerator;
    while (audio_cycle_fraction >= audio_cycle_denominator) {
        audio_cycle_fraction -= audio_cycle_denominator;
        int32_t beeper = filter_beeper(beeper_level ? 7000 : 0);
        queue_sample(clamp_sample(beeper + resample_dsp()));
    }
    beeper_level = next_level != 0;
}

static uint32_t greatest_common_divisor(uint32_t first, uint32_t second)
{
    while (second) {
        uint32_t remainder = first % second;
        first = second;
        second = remainder;
    }
    return first ? first : 1u;
}

static void service_audio()
{
    if (!audio_open || queued_samples < block_samples || !bda_audio_ready()) return;
    if (read_index + block_samples <= ring_samples) {
        if (bda_audio_write(&audio_ring[read_index], block_samples * sizeof(int16_t)) ==
            (int)(block_samples * sizeof(int16_t))) {
            read_index = (read_index + block_samples) & (ring_samples - 1u);
            queued_samples -= block_samples;
        }
        return;
    }
    uint32_t first = ring_samples - read_index;
    memcpy(silence_block, &audio_ring[read_index], first * sizeof(int16_t));
    memcpy(silence_block + first, audio_ring,
           (block_samples - first) * sizeof(int16_t));
    if (bda_audio_write(silence_block, sizeof(silence_block)) ==
        (int)sizeof(silence_block)) {
        read_index = (read_index + block_samples) & (ring_samples - 1u);
        queued_samples -= block_samples;
    }
}

void init_audio()
{
    read_index = write_index = queued_samples = 0u;
    dsp_input_read = dsp_input_write = dsp_input_count = 0u;
    dsp_playback_read = dsp_playback_write = dsp_playback_count = 0u;
    dsp_phase = 0u;
    dsp_sample_0 = dsp_sample_1 = 0;
    bbk_dsp_write_count = 0u;
    bbk_dsp_generated_samples = 0u;
    bbk_dsp_last_write = 0u;
    last_audio_cycle = 0u;
    audio_cycle_fraction = 0u;
    uint32_t divisor = greatest_common_divisor(CYCLES_SECOND, output_rate);
    audio_cycle_numerator = output_rate / divisor;
    audio_cycle_denominator = CYCLES_SECOND / divisor;
    speech_hold_samples = 0u;
    word_job_observed = 0;
    word_prebuffering = 0;
    audio_clock_started = 0;
    beeper_level = 0;
    filter_input = filter_output = 0;
    memset(silence_block, 0, sizeof(silence_block));
    dsp.callback = dsp_callback;
    set_dsp_log_level(0);
    original_attenuation = bda_audio_get_attenuation();
    bda_audio_open_pcm(BDA_AUDIO_SAMPLE_RATE_22050, BDA_AUDIO_BITS_16,
                       BDA_AUDIO_CHANNELS_MONO);
    bda_audio_set_attenuation(BDA_AUDIO_ATTENUATION_FULL_SCALE);
    audio_open = 1;
#ifdef NC2000_DSP_SELF_TEST
    queue_dsp_self_test();
#endif
}

void shutdown_audio()
{
    if (!audio_open) return;
    memset(silence_block, 0, sizeof(silence_block));
    bda_audio_set_attenuation((uint32_t)original_attenuation);
    if (bda_audio_ready()) (void)bda_audio_write(silence_block, sizeof(silence_block));
    bda_audio_stop();
    audio_open = 0;
}

void reset_dsp() { dsp.reset(); }
void write_data_to_dsp(uint8_t high, uint8_t low)
{
    ++bbk_dsp_write_count;
    bbk_dsp_last_write = ((uint32_t)high << 8) | low;
    dsp.write(high, low);
}
void dsp_move(int samples)
{
    // WORD mode used by dictionary SAY is decoded incrementally.  One CELP
    // frame per 10 ms DSP tick avoids blocking the UI/audio feeder on the
    // entire word while still decoding faster than real-time until the FIFO
    // reaches its normal busy threshold.
    bool word_pending = dsp.has_pending_word_decode();
    if (word_pending && !word_job_observed) {
        word_job_observed = 1;
        word_prebuffering = 1;
    }
    if (samples > 0 && word_pending && dsp_input_count < dsp_busy_samples)
        dsp.process_pending_word(1);

    word_pending = dsp.has_pending_word_decode();
    if (word_prebuffering) {
        if (dsp_input_count >= word_prebuffer_samples || !word_pending)
            word_prebuffering = 0;
        else
            samples = 0;
    }
    if (!word_pending) word_job_observed = 0;

    uint32_t remaining = samples > 0 ? (uint32_t)samples : 0u;
    while (remaining && dsp_input_count && dsp_playback_count < dsp_ring_samples) {
        uint32_t count = remaining;
        uint32_t playback_free = dsp_ring_samples - dsp_playback_count;
        uint32_t input_contiguous = dsp_ring_samples - dsp_input_read;
        uint32_t playback_contiguous = dsp_ring_samples - dsp_playback_write;
        if (count > dsp_input_count) count = dsp_input_count;
        if (count > playback_free) count = playback_free;
        if (count > input_contiguous) count = input_contiguous;
        if (count > playback_contiguous) count = playback_contiguous;
        memcpy(&dsp_playback_ring[dsp_playback_write],
               &dsp_input_ring[dsp_input_read], count * sizeof(int16_t));
        dsp_input_read = (dsp_input_read + count) & (dsp_ring_samples - 1u);
        dsp_playback_write = (dsp_playback_write + count) & (dsp_ring_samples - 1u);
        dsp_input_count -= count;
        dsp_playback_count += count;
        remaining -= count;
    }
}
bool sound_busy()
{
    return dsp_input_count > dsp_busy_samples || dsp.has_pending_word_decode();
}
void beeper_on_io_write(int value) { update_beeper(value); }
void manipulate_beeper(int value) { update_beeper(value); }
void post_cpu_run_sound_handling() { update_beeper(beeper_level); }

void bbk_audio_service() { service_audio(); }
bool bbk_audio_speech_active()
{
    return dsp_input_count || dsp_playback_count || speech_hold_samples ||
           dsp.buffered_samples();
}
uint32_t bbk_audio_dsp_pending_samples()
{
    return dsp_input_count + dsp_playback_count + dsp.buffered_samples();
}
