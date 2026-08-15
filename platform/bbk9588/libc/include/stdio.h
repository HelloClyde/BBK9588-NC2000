#ifndef BBK9588_STDIO_H
#define BBK9588_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bbk_file FILE;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int remove(const char *path);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *output, const char *format, ...);
int snprintf(char *output, size_t size, const char *format, ...);
int vsnprintf(char *output, size_t size, const char *format, va_list args);
int sscanf(const char *input, const char *format, ...);
int puts(const char *text);

#ifdef __cplusplus
}
#endif

#endif
