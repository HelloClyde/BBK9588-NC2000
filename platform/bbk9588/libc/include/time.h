#ifndef BBK9588_TIME_H
#define BBK9588_TIME_H

#include <stdint.h>

typedef int64_t time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#define CLOCK_REALTIME 0

#ifdef __cplusplus
extern "C" {
#endif
time_t time(time_t *result);
struct tm *localtime(const time_t *value);
int clock_gettime(int clock_id, struct timespec *value);
#ifdef __cplusplus
}
#endif

#endif
