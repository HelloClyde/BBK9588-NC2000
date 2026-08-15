typedef void (*constructor_t)(void);

extern constructor_t __init_array_start[];
extern constructor_t __init_array_end[];

void bbk_run_constructors(void)
{
    constructor_t *constructor;
    for (constructor = __init_array_start;
         constructor < __init_array_end;
         ++constructor) {
        (*constructor)();
    }
}
