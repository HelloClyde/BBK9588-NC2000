#include "platform/bbk9588/jit_mips32.h"

extern "C" {
#include "ansi/w65c02.h"
}
#include "cpu.h"
#include "mem.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const unsigned jit_block_slots = 65536u;
static const unsigned jit_block_ways = 4u;
static const unsigned jit_block_buckets = jit_block_slots / jit_block_ways;
static const unsigned jit_max_block_instructions = 8u;
static const size_t jit_code_cache_bytes = 2u * 1024u * 1024u;
static const uintptr_t jit_cache_line_size = 32u;
static const unsigned guest_generation_shift = 8u;
static const unsigned guest_generation_slots =
    0x10000u >> guest_generation_shift;

typedef unsigned (*JitFunction)(void);

struct JitBlock {
    uint32_t epoch;
    uint32_t mapping;
    uint32_t page_generation;
    uint16_t pc;
    uint8_t instruction_count;
    uint8_t max_cycles;
    JitFunction function;
};

static JitBlock *blocks;
static uint32_t guest_code_generation[guest_generation_slots];
static uint8_t *code_allocation;
static uint32_t *code_cache;
static size_t code_word_capacity;
static size_t next_code_word;
static uint32_t code_epoch = 1u;
static uint8_t replacement_ways[jit_block_buckets];

extern "C" volatile uint32_t bbk_jit_blocks_compiled;
extern "C" volatile uint32_t bbk_jit_block_calls;
extern "C" volatile uint32_t bbk_jit_fallback_ops;
extern "C" volatile uint32_t bbk_jit_cache_resets;
extern "C" volatile uint32_t bbk_jit_max_block_instructions;
extern "C" volatile uint32_t bbk_jit_block_replacements;
extern "C" volatile uint32_t bbk_jit_cache_fallback_blocks;

volatile uint32_t bbk_jit_blocks_compiled;
volatile uint32_t bbk_jit_block_calls;
volatile uint32_t bbk_jit_fallback_ops;
volatile uint32_t bbk_jit_cache_resets;
volatile uint32_t bbk_jit_max_block_instructions;
volatile uint32_t bbk_jit_block_replacements;
volatile uint32_t bbk_jit_cache_fallback_blocks;

static unsigned block_bucket(uint16_t pc, uint32_t mapping)
{
    // NC2000 executes code through aggressively banked windows.  Hash the
    // backing mapping as part of the key so different ROM banks at the same
    // 16-bit PC do not continually evict and recompile one another.
    uint32_t value = mapping ^ (mapping >> 11) ^
                     ((uint32_t)pc * 0x9e3779b1u);
    value ^= value >> 16;
    return (unsigned)value & (jit_block_buckets - 1u);
}

static void synchronize_code_cache(void *start, size_t byte_count)
{
    uintptr_t first = (uintptr_t)start & ~(jit_cache_line_size - 1u);
    uintptr_t end = ((uintptr_t)start + byte_count + jit_cache_line_size - 1u) &
                    ~(jit_cache_line_size - 1u);
    __asm__ volatile("sync" ::: "memory");
    for (uintptr_t line = first; line < end; line += jit_cache_line_size)
        __asm__ volatile("cache 0x15, 0(%0)" :: "r"(line) : "memory");
    __asm__ volatile("sync" ::: "memory");
    for (uintptr_t line = first; line < end; line += jit_cache_line_size)
        __asm__ volatile("cache 0x10, 0(%0)" :: "r"(line) : "memory");
    __asm__ volatile("sync\n\tnop\n\tnop\n\tnop" ::: "memory");
}

static void advance_epoch(void)
{
    ++code_epoch;
    if (!code_epoch) {
        if (blocks) memset(blocks, 0, sizeof(*blocks) * jit_block_slots);
        code_epoch = 1u;
    }
}

static int initialize_code_cache(void)
{
    if (code_cache && blocks) return 1;
    if (!blocks) {
        blocks = (JitBlock *)calloc(jit_block_slots, sizeof(*blocks));
        if (!blocks) return 0;
    }
    code_allocation = (uint8_t *)malloc(jit_code_cache_bytes +
                                        jit_cache_line_size - 1u);
    if (!code_allocation) {
        free(blocks);
        blocks = 0;
        return 0;
    }
    uintptr_t aligned = ((uintptr_t)code_allocation + jit_cache_line_size - 1u) &
                        ~(jit_cache_line_size - 1u);
    code_cache = (uint32_t *)aligned;
    code_word_capacity = jit_code_cache_bytes / sizeof(uint32_t);
    next_code_word = 0u;
    return 1;
}

static int opcode_ends_block(uint8_t opcode)
{
    switch (opcode) {
    case 0x00: case 0x10: case 0x20: case 0x30:
    case 0x40: case 0x4c: case 0x50: case 0x60:
    case 0x6c: case 0x70: case 0x7c: case 0x80:
    case 0x90: case 0xb0: case 0xcb: case 0xd0:
    case 0xdb: case 0xf0:
        return 1;

    case 0x04: case 0x06: case 0x08: case 0x0c: case 0x0e:
    case 0x14: case 0x16: case 0x1c: case 0x1e:
    case 0x26: case 0x2e: case 0x36: case 0x3e:
    case 0x46: case 0x48: case 0x4e: case 0x56: case 0x5a: case 0x5e:
    case 0x64: case 0x66: case 0x6e: case 0x74: case 0x76: case 0x7e:
    case 0x81: case 0x84: case 0x85: case 0x86:
    case 0x8c: case 0x8d: case 0x8e:
    case 0x91: case 0x92: case 0x94: case 0x95: case 0x96:
    case 0x99: case 0x9c: case 0x9d: case 0x9e:
    case 0xc6: case 0xce: case 0xd6: case 0xda: case 0xde:
    case 0xe6: case 0xee: case 0xf6: case 0xfe:
        return 1;
    default:
        return 0;
    }
}

static void emit_word(uint32_t *&output, uint32_t word)
{
    *output++ = word;
}

static void invalidate_generated_blocks(void)
{
    uintptr_t first = (uintptr_t)code_cache;
    uintptr_t last = first + code_word_capacity * sizeof(uint32_t);
    for (unsigned index = 0; index < jit_block_slots; ++index) {
        uintptr_t function = (uintptr_t)blocks[index].function;
        if (function >= first && function < last) {
            blocks[index].epoch = 0u;
            blocks[index].function = 0;
        }
    }
}

static JitFunction emit_block(CpuOpcodeHandler const *handlers, unsigned count)
{
    const size_t required_words = 4u + count * 5u + 5u;
    if (!initialize_code_cache()) return 0;
    if (next_code_word + required_words > code_word_capacity) {
        if (bbk_jit_cache_resets != 0u) return 0;

        // One recycle drops startup-only traces while retaining the useful
        // steady-state cache.  Repeated whole-cache recycling is pathological
        // during dictionary bank scans, so later misses safely fall back to a
        // cached one-opcode native handler in compile_block().
        invalidate_generated_blocks();
        next_code_word = 0u;
        ++bbk_jit_cache_resets;
    }

    uint32_t *start = code_cache + next_code_word;
    uint32_t *output = start;
    emit_word(output, 0x27bdffe8u); // addiu sp, sp, -24
    emit_word(output, 0xafbf0014u); // sw ra, 20(sp)
    emit_word(output, 0xafb00010u); // sw s0, 16(sp)
    emit_word(output, 0x00008021u); // move s0, zero

    for (unsigned index = 0; index < count; ++index) {
        uint32_t address = (uint32_t)(uintptr_t)handlers[index];
        emit_word(output, 0x3c190000u | (address >> 16));
        emit_word(output, 0x37390000u | (address & 0xffffu));
        emit_word(output, 0x0320f809u); // jalr t9
        emit_word(output, 0x00000000u); // delay slot
        emit_word(output, 0x02028021u); // addu s0, s0, v0
    }

    emit_word(output, 0x02001021u); // move v0, s0
    emit_word(output, 0x8fb00010u); // lw s0, 16(sp)
    emit_word(output, 0x8fbf0014u); // lw ra, 20(sp)
    emit_word(output, 0x03e00008u); // jr ra
    emit_word(output, 0x27bd0018u); // addiu sp, sp, 24

    size_t emitted_words = (size_t)(output - start);
    next_code_word += emitted_words;
    synchronize_code_cache(start, emitted_words * sizeof(uint32_t));
    return (JitFunction)(void *)start;
}

static JitBlock *compile_block(JitBlock *block, uint16_t start_pc,
                               uint32_t mapping)
{
    CpuOpcodeHandler handlers[jit_max_block_instructions];
    unsigned count = 0u;
    uint16_t pc = start_pc;
    unsigned page = (unsigned)start_pc >> 13;
    unsigned generation_slot = (unsigned)start_pc >> guest_generation_shift;

    block->epoch = 0u;
    block->function = 0;
    if (start_pc < 0x80u || !memmap[page]) return block;

    while (count < jit_max_block_instructions) {
        if (((unsigned)pc >> 13) != page || pc < 0x80u ||
            ((unsigned)pc >> guest_generation_shift) != generation_slot)
            break;
        uint8_t opcode = memmap[page][pc & 0x1fffu];
        if (illegal_op_byte[opcode]) break;
        uint8_t length = CpuGetOpcodeLength(opcode);
        if (!length || (unsigned)pc + length > 0x10000u) break;
        handlers[count++] = CpuGetOpcodeHandler(opcode);
        pc = (uint16_t)(pc + length);
        if (opcode_ends_block(opcode)) break;
    }
    if (!count) return block;

    // A generated wrapper costs more than it saves for a one-opcode block.
    // The specialized handler is already native MIPS code, so cache it
    // directly and reserve generated code for blocks that can amortize the
    // call/prologue overhead across multiple guest instructions.
    JitFunction function = count == 1u ? handlers[0]
                                       : emit_block(handlers, count);
    if (!function && count > 1u) {
        // The generated cache is deliberately no longer recycled after the
        // first reset.  A predecoded one-opcode handler is still native MIPS,
        // remains valid permanently, and prevents a compile/reset storm.
        count = 1u;
        function = handlers[0];
        ++bbk_jit_cache_fallback_blocks;
    }
    if (!function) return block;
    block->pc = start_pc;
    block->mapping = mapping;
    block->page_generation = guest_code_generation[generation_slot];
    block->instruction_count = (uint8_t)count;
    block->max_cycles = (uint8_t)(count * 8u);
    block->function = function;
    block->epoch = code_epoch;
    ++bbk_jit_blocks_compiled;
    if (count > bbk_jit_max_block_instructions)
        bbk_jit_max_block_instructions = count;
    return block;
}

void bbk_jit_reset(void)
{
    memset(guest_code_generation, 0, sizeof(guest_code_generation));
    next_code_word = 0u;
    memset(replacement_ways, 0, sizeof(replacement_ways));
    advance_epoch();
    bbk_jit_blocks_compiled = 0u;
    bbk_jit_block_calls = 0u;
    bbk_jit_fallback_ops = 0u;
    bbk_jit_cache_resets = 0u;
    bbk_jit_max_block_instructions = 0u;
    bbk_jit_block_replacements = 0u;
    bbk_jit_cache_fallback_blocks = 0u;
}

void bbk_jit_shutdown(void)
{
    if (code_allocation) free(code_allocation);
    if (blocks) free(blocks);
    code_allocation = 0;
    code_cache = 0;
    blocks = 0;
    code_word_capacity = 0u;
    next_code_word = 0u;
    memset(replacement_ways, 0, sizeof(replacement_ways));
    advance_epoch();
}

void bbk_jit_notify_write(uint16_t address)
{
    if (address < 0x80u) return;
    ++guest_code_generation[(unsigned)address >> guest_generation_shift];
}

int bbk_jit_execute_block(int cycle_budget)
{
    if (!initialize_code_cache()) {
        ++bbk_jit_fallback_ops;
        return (int)CpuExecuteOP();
    }
    uint16_t pc = (uint16_t)mPC;
    unsigned page = (unsigned)pc >> 13;
    unsigned generation_slot = (unsigned)pc >> guest_generation_shift;
    uint32_t mapping = (uint32_t)(uintptr_t)memmap[page];
    uint32_t generation = guest_code_generation[generation_slot];
    unsigned bucket_index = block_bucket(pc, mapping);
    JitBlock *bucket = blocks + bucket_index * jit_block_ways;
    JitBlock *block = 0;
    JitBlock *available = 0;

    for (unsigned way = 0u; way < jit_block_ways; ++way) {
        JitBlock *candidate = bucket + way;
        if (candidate->epoch == code_epoch && candidate->function &&
            candidate->pc == pc && candidate->mapping == mapping) {
            block = candidate;
            break;
        }
        if (!available &&
            (candidate->epoch != code_epoch || !candidate->function))
            available = candidate;
    }

    if (!block || block->page_generation != generation) {
        if (!block) block = available;
        if (!block) {
            unsigned way = replacement_ways[bucket_index]++ &
                           (jit_block_ways - 1u);
            block = bucket + way;
            ++bbk_jit_block_replacements;
        }
        block = compile_block(block, pc, mapping);
    }
    if (!block->function || block->max_cycles > cycle_budget) {
        ++bbk_jit_fallback_ops;
        return (int)CpuExecuteOP();
    }
    ++bbk_jit_block_calls;
    return (int)block->function();
}
