#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bbk_diag_log_initialize(void);
void bbk_diag_log_shutdown(void);
void bbk_diag_log_set_phase(const char *phase);
void bbk_diag_log_printf(const char *format, ...);
void bbk_diag_log_heartbeat(uint32_t milliseconds);
const char *bbk_diag_log_path(void);

void bbk_diag_log_abort(void);
void bbk_diag_log_assert(const char *expression, const char *file, int line);

#ifdef __cplusplus
}
#endif
