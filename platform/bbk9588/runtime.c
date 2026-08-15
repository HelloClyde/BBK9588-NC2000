#include "bda_filesystem.h"
#include "bda_memory.h"
#include "bda_time.h"
#include "platform/bbk9588/diagnostic_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ALLOCATION_MAGIC 0x9588a110u
#define FILE_POOL_SIZE 8u

typedef union allocation_header {
    struct {
        uint32_t magic;
        uint32_t size;
    } fields;
    uint64_t alignment;
} allocation_header_t;

struct bbk_file {
    int handle;
    int used;
};

static struct bbk_file g_files[FILE_POOL_SIZE];
FILE *stdin;
FILE *stdout;
FILE *stderr;
int errno;

void *malloc(size_t size)
{
    allocation_header_t *header;
    if (size == 0u) size = 1u;
    if (size > 0xffffffffu - sizeof(*header)) return 0;
    header = (allocation_header_t *)bda_alloc((u32)(size + sizeof(*header)));
    if (!header || (uint32_t)header == 0xffffffffu) return 0;
    header->fields.magic = ALLOCATION_MAGIC;
    header->fields.size = (uint32_t)size;
    return header + 1;
}

void free(void *pointer)
{
    allocation_header_t *header;
    if (!pointer) return;
    header = (allocation_header_t *)pointer - 1;
    if (header->fields.magic != ALLOCATION_MAGIC) return;
    header->fields.magic = 0u;
    bda_free(header);
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *pointer;
    if (size && count > (size_t)-1 / size) return 0;
    total = count * size;
    pointer = malloc(total);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, size_t size)
{
    allocation_header_t *header;
    void *replacement;
    size_t copy_size;
    if (!pointer) return malloc(size);
    if (!size) {
        free(pointer);
        return 0;
    }
    header = (allocation_header_t *)pointer - 1;
    if (header->fields.magic != ALLOCATION_MAGIC) return 0;
    replacement = malloc(size);
    if (!replacement) return 0;
    copy_size = header->fields.size < size ? header->fields.size : size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    while (count--) *out++ = *in++;
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out <= in || out >= in + count) return memcpy(destination, source, count);
    out += count;
    in += count;
    while (count--) *--out = *--in;
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    while (count--) *out++ = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (count--) {
        if (*a != *b) return (int)*a - (int)*b;
        ++a;
        ++b;
    }
    return 0;
}

size_t strlen(const char *text)
{
    const char *end = text;
    while (*end) ++end;
    return (size_t)(end - text);
}

int strcmp(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count && *left && *left == *right) {
        ++left; ++right; --count;
    }
    return count ? (int)(uint8_t)*left - (int)(uint8_t)*right : 0;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    while ((*destination++ = *source++) != 0) {}
    return result;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    char *result = destination;
    while (count && *source) { *destination++ = *source++; --count; }
    while (count--) *destination++ = 0;
    return result;
}

char *strcat(char *destination, const char *source)
{
    strcpy(destination + strlen(destination), source);
    return destination;
}

char *strncat(char *destination, const char *source, size_t count)
{
    char *out = destination + strlen(destination);
    while (count-- && *source) *out++ = *source++;
    *out = 0;
    return destination;
}

char *strchr(const char *text, int character)
{
    char target = (char)character;
    do { if (*text == target) return (char *)text; } while (*text++);
    return 0;
}

char *strrchr(const char *text, int character)
{
    const char *match = 0;
    char target = (char)character;
    do { if (*text == target) match = text; } while (*text++);
    return (char *)match;
}

char *strstr(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (!length) return (char *)text;
    while (*text) {
        if (*text == *needle && memcmp(text, needle, length) == 0) return (char *)text;
        ++text;
    }
    return 0;
}

static unsigned digit_value(char character)
{
    if (character >= '0' && character <= '9') return (unsigned)(character - '0');
    if (character >= 'a' && character <= 'z') return (unsigned)(character - 'a' + 10);
    if (character >= 'A' && character <= 'Z') return (unsigned)(character - 'A' + 10);
    return 255u;
}

unsigned long strtoul(const char *text, char **end, int base)
{
    unsigned long value = 0;
    unsigned digit;
    while (*text == ' ' || *text == '\t' || *text == '\n') ++text;
    if (base == 0) {
        base = (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;
    }
    if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    while ((digit = digit_value(*text)) < (unsigned)base) {
        value = value * (unsigned)base + digit;
        ++text;
    }
    if (end) *end = (char *)text;
    return value;
}

long strtol(const char *text, char **end, int base)
{
    int negative = 0;
    while (*text == ' ' || *text == '\t' || *text == '\n') ++text;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    unsigned long value = strtoul(text, end, base);
    return negative ? -(long)value : (long)value;
}

int atoi(const char *text) { return (int)strtol(text, 0, 10); }
long atol(const char *text) { return strtol(text, 0, 10); }

static FILE *allocate_file(void)
{
    u32 index;
    for (index = 0u; index < FILE_POOL_SIZE; ++index) {
        if (!g_files[index].used) {
            g_files[index].used = 1;
            return &g_files[index];
        }
    }
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *stream = allocate_file();
    if (!stream) return 0;
    stream->handle = bda_fs_fopen_raw(path, mode);
    if (!bda_fs_file_is_valid(stream->handle)) {
        stream->used = 0;
        return 0;
    }
    return stream;
}

int fclose(FILE *stream)
{
    int result;
    if (!stream || !stream->used) return EOF;
    result = bda_fs_close_raw(stream->handle);
    stream->used = 0;
    stream->handle = 0;
    return result;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    size_t requested;
    int bytes;
    if (!stream || !stream->used || !size || !count) return 0u;
    if (count > 0xffffffffu / size) return 0u;
    requested = size * count;
    bytes = bda_fs_read_raw(stream->handle, buffer, (u32)requested);
    return bytes > 0 ? (size_t)bytes / size : 0u;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    size_t requested;
    int bytes;
    if (!stream || !stream->used || !size || !count) return 0u;
    if (count > 0xffffffffu / size) return 0u;
    requested = size * count;
    bytes = bda_fs_write_raw(stream->handle, buffer, (u32)requested);
    return bytes > 0 ? (size_t)bytes / size : 0u;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream || !stream->used) return -1;
    return bda_fs_seek_raw(stream->handle, (s32)offset, whence) < 0 ? -1 : 0;
}

long ftell(FILE *stream)
{
    if (!stream || !stream->used) return -1;
    return (long)bda_fs_tell_raw(stream->handle);
}

int fflush(FILE *stream) { (void)stream; return 0; }
int remove(const char *path) { (void)path; return -1; }

static void format_char(char *output, size_t size, size_t *offset, char value)
{
    if (*offset + 1u < size) output[*offset] = value;
    ++*offset;
}

static void format_unsigned(char *output, size_t size, size_t *offset,
                            uint64_t value, unsigned base, int width,
                            char pad, int uppercase)
{
    char digits[24];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int count = 0;
    do { digits[count++] = alphabet[value % base]; value /= base; }
    while (value && count < (int)sizeof(digits));
    while (width-- > count) format_char(output, size, offset, pad);
    while (count) format_char(output, size, offset, digits[--count]);
}

int vsnprintf(char *output, size_t size, const char *format, va_list args)
{
    size_t offset = 0u;
    while (*format) {
        int width = 0;
        int long_count = 0;
        char pad = ' ';
        char specifier;
        if (*format != '%') { format_char(output, size, &offset, *format++); continue; }
        ++format;
        if (*format == '0') { pad = '0'; ++format; }
        while (*format >= '0' && *format <= '9') width = width * 10 + (*format++ - '0');
        while (*format == 'l') { ++long_count; ++format; }
        if (*format == 'z') { long_count = 1; ++format; }
        specifier = *format ? *format++ : 0;
        if (specifier == 's') {
            const char *text = va_arg(args, const char *);
            if (!text) text = "(null)";
            while (*text) format_char(output, size, &offset, *text++);
        } else if (specifier == 'c') {
            format_char(output, size, &offset, (char)va_arg(args, int));
        } else if (specifier == 'd' || specifier == 'i') {
            int64_t value = long_count >= 2 ? va_arg(args, long long) :
                (long_count == 1 ? va_arg(args, long) : va_arg(args, int));
            uint64_t magnitude;
            if (value < 0) { format_char(output, size, &offset, '-'); magnitude = (uint64_t)(-(value + 1)) + 1u; }
            else magnitude = (uint64_t)value;
            format_unsigned(output, size, &offset, magnitude, 10u, width, pad, 0);
        } else if (specifier == 'u' || specifier == 'x' || specifier == 'X') {
            uint64_t value = long_count >= 2 ? va_arg(args, unsigned long long) :
                (long_count == 1 ? va_arg(args, unsigned long) : va_arg(args, unsigned));
            format_unsigned(output, size, &offset, value, specifier == 'u' ? 10u : 16u,
                            width, pad, specifier == 'X');
        } else if (specifier == 'p') {
            format_unsigned(output, size, &offset, (uintptr_t)va_arg(args, void *), 16u, 8, '0', 0);
        } else if (specifier == '%') format_char(output, size, &offset, '%');
    }
    if (size) output[offset < size ? offset : size - 1u] = 0;
    return (int)offset;
}

int snprintf(char *output, size_t size, const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(output, size, format, args);
    va_end(args);
    return result;
}

int sprintf(char *output, const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(output, (size_t)-1, format, args);
    va_end(args);
    return result;
}

int printf(const char *format, ...)
{
    char ignored[192];
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(ignored, sizeof(ignored), format, args);
    va_end(args);
    return result;
}

int fprintf(FILE *stream, const char *format, ...)
{
    char buffer[192];
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (stream && stream->used && length > 0) {
        size_t bytes = (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1u;
        (void)fwrite(buffer, 1u, bytes, stream);
    }
    return length;
}

int sscanf(const char *input, const char *format, ...) { (void)input; (void)format; return 0; }
int puts(const char *text) { return printf("%s\n", text); }

int atexit(void (*function)(void)) { (void)function; return 0; }

void abort(void)
{
    bbk_diag_log_abort();
    for (;;) bda_sys_delay(1u);
}

void exit(int status)
{
    (void)status;
    abort();
}

void bbk_assert_fail(const char *expression, const char *file, int line)
{
    bbk_diag_log_assert(expression, file, line);
    abort();
}

time_t time(time_t *result)
{
    time_t value = 946684800ll + (time_t)(bda_gui_tick_count_25ms() / 40u);
    if (result) *result = value;
    return value;
}

static int leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

struct tm *localtime(const time_t *value)
{
    static struct tm result;
    static const int month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    time_t days = *value / 86400ll;
    time_t seconds = *value % 86400ll;
    int year = 1970;
    int month = 0;
    int day_of_year;
    while (seconds < 0) { seconds += 86400ll; --days; }
    result.tm_hour = (int)(seconds / 3600ll);
    result.tm_min = (int)((seconds / 60ll) % 60ll);
    result.tm_sec = (int)(seconds % 60ll);
    result.tm_wday = (int)((days + 4ll) % 7ll);
    if (result.tm_wday < 0) result.tm_wday += 7;
    while (days >= (leap_year(year) ? 366 : 365)) { days -= leap_year(year) ? 366 : 365; ++year; }
    day_of_year = (int)days;
    while (month < 11) {
        int length = month_days[month] + (month == 1 && leap_year(year));
        if (days < length) break;
        days -= length;
        ++month;
    }
    result.tm_year = year - 1900;
    result.tm_mon = month;
    result.tm_mday = (int)days + 1;
    result.tm_yday = day_of_year;
    result.tm_isdst = 0;
    return &result;
}

int clock_gettime(int clock_id, struct timespec *value)
{
    u32 milliseconds;
    (void)clock_id;
    if (!value) return -1;
    milliseconds = bda_gui_tick_count_25ms() * 25u;
    value->tv_sec = (time_t)(milliseconds / 1000u);
    value->tv_nsec = (long)(milliseconds % 1000u) * 1000000l;
    return 0;
}
