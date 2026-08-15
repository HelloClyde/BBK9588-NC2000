#include "CC800IOName.h"
#include "NekoDriverIO.h"
#include "comm.h"
#include "cpu.h"
#include "io_new.h"
#include "iv_uart.h"
#include "mem.h"
#include "nc2000.h"
#include "platform/bbk9588/jit_mips32.h"
#include "ram.h"
#include "sound.h"
#include "state.h"

#include <time.h>

extern nc2k_states_t nc2k_states;

static u64_t &cycles = nc2k_states.cycles;
static u64_t &last_cycles = nc2k_states.last_cycles;
static uint8_t *rtc_reg = nc2k_states.ext_reg;
static bool &do_warm_reset = nc2k_states.do_warm_reset;

static uint32_t timebase_phase;
static uint32_t rtc_phase;
static uint32_t dsp_phase;

extern bool &timer0run;
extern bool &timer1run_tmie;

void cpu_run() { cpu_run3(); }
void cpu_run2() { cpu_run3(); }

static void sync_calendar(unsigned year_address)
{
    time_t current_time = time(0);
    struct tm *local = localtime(&current_time);
    if (!local || local->tm_year + 1900 > 2031) return;
    Store((uint16_t)year_address, (uint8_t)(local->tm_year - 103 + 0x7a));
    Store((uint16_t)(year_address + 1), (uint8_t)local->tm_mon);
    Store((uint16_t)(year_address + 2), (uint8_t)(local->tm_mday - 1));
    rtc_reg[2] = (uint8_t)local->tm_hour;
    rtc_reg[1] = (uint8_t)local->tm_min;
    rtc_reg[0] = (uint8_t)local->tm_sec;
}

void sync_time_2000() { sync_calendar(0x03fa); }
void sync_time_1020() { sync_calendar(0x0472); }

void init_cpu_new()
{
    if (!cpu) cpu = new CPUInterface();
    else cpu->reset();
    timebase_phase = 0u;
    rtc_phase = 0u;
    dsp_phase = 0u;
    bbk_jit_reset();
}

static int trigger_x_times_per_s(int count)
{
    uint32_t period = CYCLES_SECOND / (uint32_t)count;
    return (int)(cycles / period - last_cycles / period);
}

static int advance_rate_phase(uint32_t elapsed_cycles, uint32_t rate,
                              uint32_t *phase)
{
    uint32_t total = *phase + elapsed_cycles * rate;
    int triggers = 0;
    while (total >= CYCLES_SECOND) {
        total -= CYCLES_SECOND;
        ++triggers;
    }
    *phase = total;
    return triggers;
}

static bool keep_timer01(unsigned int cpu_ticks)
{
    bool interrupt = false;
    uint8_t *io = nc2k_states.ram_io;
    if (timer0run) {
        timer0ticks += (int)cpu_ticks;
        int shift = 1 + (w0c_b67_TMODESL == 1 ? w0c_b45_TM0S : w0c_b345_TMS);
        int increment = timer0ticks >> shift;
        if (increment) timer0ticks -= increment << shift;
        if (w0c_b67_TMODESL <= 1) {
            unsigned short value = io[io02_timer0_val] + increment;
            if (value > 0xff && (w0c_b67_TMODESL == 1 || timer1run_tmie)) {
                io[io01_int_status] |= 0x10u;
                interrupt = true;
            }
            io[io02_timer0_val] = (uint8_t)(w0c_b67_TMODESL == 1 ? value :
                value + io[io03_timer1_val]);
        } else if (w0c_b67_TMODESL == 2) {
            unsigned short value = io[io02_timer0_val] + increment;
            io[io02_timer0_val] = (uint8_t)value;
            if (value > 0xff) {
                unsigned short high = io[io03_timer1_val] + (value >> 8);
                if (high > 0xff) { io[io01_int_status] |= 0x20u; interrupt = true; }
                io[io03_timer1_val] = (uint8_t)high;
            }
        } else {
            unsigned short value = io[io02_timer0_val] + increment;
            io[io02_timer0_val] = (uint8_t)value;
            if (value > 0xff) {
                io[io01_int_status] |= 0x10u;
                interrupt = true;
                if (timer1run_tmie) {
                    unsigned short high = io[io03_timer1_val] + (value >> 8);
                    if (high > 0xff) io[io01_int_status] |= 0x20u;
                    io[io03_timer1_val] = (uint8_t)high;
                }
            }
        }
    }
    if (timer1run_tmie && w0c_b67_TMODESL == 1) {
        timer1ticks += (int)cpu_ticks;
        int shift = (w0c_b23_TM1S + 1) * 2;
        int increment = timer1ticks >> shift;
        if (increment) timer1ticks -= increment << shift;
        unsigned short value = io[io03_timer1_val] + increment;
        io[io03_timer1_val] = (uint8_t)value;
        if (value > 0xff) { io[io01_int_status] |= 0x20u; interrupt = true; }
    }
    return interrupt;
}

static void bump_rtc()
{
    if (++rtc_reg[0] == 60) {
        rtc_reg[0] = 0;
        if (++rtc_reg[1] == 60) {
            rtc_reg[1] = 0;
            if (++rtc_reg[2] == 24) { rtc_reg[2] = 0; ++rtc_reg[3]; }
        }
    }
}

bool is_clk_off() { return nc2k_states.ram_io[0x05] >> 5 == 7; }

void warm_reset_if_clkoff()
{
    if (is_clk_off()) do_warm_reset = true;
}

void cold_reset()
{
    nc2k_cold_reset();
    cpu->reset();
}

void warm_reset()
{
    nc2k_warm_reset();
    cpu->reset();
}

void debug_pc() {}

void cpu_run3()
{
    if (do_warm_reset) {
        do_warm_reset = false;
        nc2k_warm_reset();
        cpu->reset();
    }

    uint32_t requested_cycles = cpu_batch;
    uint32_t executed_cycles;
    if (is_clk_off()) {
        executed_cycles = requested_cycles;
        last_cycles = cycles;
        cycles += executed_cycles;
    } else if (enable_emulate_cks) {
        requested_cycles /= speed_scaledown;
        if (!requested_cycles) requested_cycles = 1;
        executed_cycles = (uint32_t)cpu->execute((int)requested_cycles);
        last_cycles = cycles;
        cycles += executed_cycles * speed_scaledown;
    } else {
        executed_cycles = (uint32_t)cpu->execute((int)requested_cycles);
        last_cycles = cycles;
        cycles += executed_cycles;
    }

    uint32_t elapsed_cycles = (uint32_t)(cycles - last_cycles);
    // The BBK frontend always uses timer01_speed_fix == 1.0.  Keeping this
    // path integer-only avoids soft-float u64<->double conversions per batch.
    if (keep_timer01(elapsed_cycles)) cpu->set_irq_pending();

    if (advance_rate_phase(elapsed_cycles, 185u, &timebase_phase) &&
        timeBaseEnable()) {
        setIrqTimeBase();
        cpu->set_irq_pending();
    }

    int rtc_triggers = advance_rate_phase(elapsed_cycles, 256u, &rtc_phase);
    for (int index = 0; index < rtc_triggers; ++index) {
        uint8_t subsecond = ++rtc_reg[4];
        if (subsecond == 0) {
            if (enable_keepon) Store(1025, 0);
            bump_rtc();
        }
        if ((subsecond & 0x7f) == 0) {
            if ((RCR0 & RCR0_2HZ) != 0) put_iv(IV_2HZ);
        }
        if ((subsecond & 0x7f) == 64 && nmiEnable()) cpu->set_nmi_pending();
        uint32_t sample_rate = get_sample_hz();
        if (sample_rate && trigger_x_times_per_s((int)sample_rate)) put_iv(IV_SAMPLE);
        if (peek_iv() != IV_NONE) {
            cpu->set_irq_pending();
            warm_reset_if_clkoff();
        }
    }

    dsp_move(advance_rate_phase(elapsed_cycles, 100u, &dsp_phase) *
             (DSP_AUDIO_HZ / 100));
}
