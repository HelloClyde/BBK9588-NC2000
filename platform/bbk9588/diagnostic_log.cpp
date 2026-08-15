#include "platform/bbk9588/diagnostic_log.h"

#include "bda_filesystem.h"
#include "bda_time.h"

#include "cpu.h"
#include "mem.h"
#include "platform/bbk9588/jit_mips32.h"
#include "platform/bbk9588/sound_bbk.h"
#include "state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern nc2k_states_t nc2k_states;

static const uint32_t maximum_log_bytes = 512u * 1024u;
static const char *const log_paths[] = {
    "A:\\NC2000\\NC2000.LOG",
    "A:\\NC2000\\NC2001.LOG",
    "A:\\NC2000\\NC2002.LOG"
};
static const char fallback_log_path[] = "A:\\NC2000.LOG";

static const char *active_log_path;
static char current_phase[20] = "startup";
static uint32_t log_sequence;
static unsigned write_failures;
static int log_enabled;
static int log_busy;

static int raw_file_size(const char *path)
{
    int file = bda_fs_fopen_raw(path, "rb");
    int size;
    if (!bda_fs_file_is_valid(file)) return -1;
    size = bda_fs_seek_raw(file, 0, BDA_SEEK_END);
    (void)bda_fs_close_raw(file);
    return size;
}

static int append_bytes(const char *path, const char *data, unsigned size)
{
    int file = bda_fs_fopen_raw(path, "ab");
    int wrote;
    int closed;
    if (!bda_fs_file_is_valid(file)) return 0;
    wrote = size ? bda_fs_write_raw(file, data, size) : 0;
    closed = bda_fs_close_raw(file);
    return wrote == (int)size && closed == 0;
}

static void append_formatted(const char *format, va_list args)
{
    char line[448];
    int prefix;
    int payload;
    unsigned length;
    uint32_t milliseconds;

    if (!log_enabled || log_busy || !active_log_path) return;
    log_busy = 1;
    milliseconds = bda_gui_tick_count_25ms() * 25u;
    prefix = snprintf(line, sizeof(line), "%08u #%05u ",
                      milliseconds, ++log_sequence);
    if (prefix < 0) prefix = 0;
    if ((unsigned)prefix >= sizeof(line)) prefix = (int)sizeof(line) - 1;
    payload = vsnprintf(line + prefix, sizeof(line) - (unsigned)prefix,
                        format, args);
    if (payload < 0) payload = 0;
    length = (unsigned)prefix + (unsigned)payload;
    if (length > sizeof(line) - 3u) length = sizeof(line) - 3u;
    line[length++] = '\r';
    line[length++] = '\n';
    line[length] = 0;

    if (append_bytes(active_log_path, line, length))
        write_failures = 0u;
    else if (++write_failures >= 3u)
        log_enabled = 0;
    log_busy = 0;
}

void bbk_diag_log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    append_formatted(format, args);
    va_end(args);
}

void bbk_diag_log_initialize(void)
{
    static const char probe[] = "";
    active_log_path = 0;
    log_sequence = 0u;
    write_failures = 0u;
    log_busy = 0;
    log_enabled = 1;
    strcpy(current_phase, "startup");

    (void)bda_fs_mkdir("A:\\NC2000");
    for (unsigned index = 0u; index < sizeof(log_paths) / sizeof(log_paths[0]);
         ++index) {
        int size = raw_file_size(log_paths[index]);
        if (size >= 0 && (uint32_t)size >= maximum_log_bytes &&
            index + 1u < sizeof(log_paths) / sizeof(log_paths[0]))
            continue;
        if (append_bytes(log_paths[index], probe, 0u)) {
            active_log_path = log_paths[index];
            break;
        }
    }
    if (!active_log_path && append_bytes(fallback_log_path, probe, 0u))
        active_log_path = fallback_log_path;
    if (!active_log_path) {
        log_enabled = 0;
        return;
    }
    bbk_diag_log_printf("RUN_BEGIN version=NC2000-BBK-JITLOG2 path=%s",
                        active_log_path);
}

void bbk_diag_log_shutdown(void)
{
    bbk_diag_log_printf("RUN_END");
    log_enabled = 0;
}

void bbk_diag_log_set_phase(const char *phase)
{
    if (!phase) phase = "unknown";
    strncpy(current_phase, phase, sizeof(current_phase) - 1u);
    current_phase[sizeof(current_phase) - 1u] = 0;
}

const char *bbk_diag_log_path(void)
{
    return active_log_path ? active_log_path : fallback_log_path;
}

void bbk_diag_log_heartbeat(uint32_t milliseconds)
{
    bbk_diag_log_printf(
        "HB ms=%u phase=%s pc=%04X op=%02X a=%02X x=%02X y=%02X sp=%02X ps=%02X "
        "cyc=%llu irq=%u nmi=%u wai=%u lcd=%04X bk=%02X bsr=%02X dsp=%d/%u/%u/%u "
        "jit=%u/%u/%u/%u/%u/%u/%u map=%p,%p,%p,%p,%p,%p,%p,%p",
        milliseconds, current_phase,
        (unsigned)nc2k_states.mPC & 0xffffu,
        (unsigned)nc2k_states.mOpcode & 0xffu,
        (unsigned)nc2k_states.mA & 0xffu,
        (unsigned)nc2k_states.mX & 0xffu,
        (unsigned)nc2k_states.mY & 0xffu,
        (unsigned)nc2k_states.mSP & 0xffu,
        (unsigned)PS() & 0xffu,
        (unsigned long long)nc2k_states.cycles,
        (unsigned)nc2k_states.g_irq, (unsigned)nc2k_states.g_nmi,
        (unsigned)nc2k_states.g_wai, (unsigned)nc2k_states.lcdbuffaddr,
        (unsigned)nc2k_states.bk, (unsigned)nc2k_states.BSR,
        nc2k_states.dspRetData, (unsigned)nc2k_states.dspTrans,
        (unsigned)nc2k_states.dspSleep,
        (unsigned)bbk_audio_dsp_pending_samples(),
        (unsigned)bbk_jit_blocks_compiled,
        (unsigned)bbk_jit_block_calls,
        (unsigned)bbk_jit_fallback_ops,
        (unsigned)bbk_jit_cache_resets,
        (unsigned)bbk_jit_max_block_instructions,
        (unsigned)bbk_jit_block_replacements,
        (unsigned)bbk_jit_cache_fallback_blocks,
        (void *)memmap[0], (void *)memmap[1], (void *)memmap[2],
        (void *)memmap[3], (void *)memmap[4], (void *)memmap[5],
        (void *)memmap[6], (void *)memmap[7]);
}

void bbk_diag_log_abort(void)
{
    bbk_diag_log_printf("ABORT phase=%s pc=%04X op=%02X",
                        current_phase,
                        (unsigned)nc2k_states.mPC & 0xffffu,
                        (unsigned)nc2k_states.mOpcode & 0xffu);
}

void bbk_diag_log_assert(const char *expression, const char *file, int line)
{
    bbk_diag_log_printf("ASSERT phase=%s pc=%04X expr=%s file=%s line=%d",
                        current_phase,
                        (unsigned)nc2k_states.mPC & 0xffffu,
                        expression, file, line);
}
