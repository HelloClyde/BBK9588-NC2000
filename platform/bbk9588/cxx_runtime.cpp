#include <stddef.h>
#include <stdlib.h>

void *operator new(size_t size) { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }
void operator delete(void *pointer) noexcept { free(pointer); }
void operator delete[](void *pointer) noexcept { free(pointer); }
void operator delete(void *pointer, size_t) noexcept { free(pointer); }
void operator delete[](void *pointer, size_t) noexcept { free(pointer); }

extern "C" int __cxa_atexit(void (*function)(void *), void *argument, void *dso)
{
    (void)function; (void)argument; (void)dso;
    return 0;
}

extern "C" void __cxa_pure_virtual(void) { abort(); }
extern "C" {
void *__dso_handle = 0;
}
