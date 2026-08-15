#ifndef BBK9588_STDLIB_H
#define BBK9588_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
int atexit(void (*function)(void));
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);

#ifdef __cplusplus
}
#endif

#endif
