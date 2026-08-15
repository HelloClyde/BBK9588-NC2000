#ifndef BBK9588_ASSERT_H
#define BBK9588_ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#ifdef __cplusplus
extern "C" {
#endif
void bbk_assert_fail(const char *expression, const char *file, int line)
    __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#define assert(expression) \
    ((expression) ? (void)0 : bbk_assert_fail(#expression, __FILE__, __LINE__))
#endif

#endif
