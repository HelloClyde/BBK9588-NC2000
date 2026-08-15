#pragma once

#include <stdint.h>

void bbk_jit_reset(void);
void bbk_jit_shutdown(void);
void bbk_jit_notify_write(uint16_t address);
int bbk_jit_execute_block(int cycle_budget);

extern "C" volatile uint32_t bbk_jit_blocks_compiled;
extern "C" volatile uint32_t bbk_jit_block_calls;
extern "C" volatile uint32_t bbk_jit_fallback_ops;
extern "C" volatile uint32_t bbk_jit_cache_resets;
extern "C" volatile uint32_t bbk_jit_max_block_instructions;
extern "C" volatile uint32_t bbk_jit_block_replacements;
extern "C" volatile uint32_t bbk_jit_cache_fallback_blocks;
