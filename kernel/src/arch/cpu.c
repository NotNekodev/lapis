#include <arch/cpu.h>

cpu_t *get_current_cpu(void) {
    return get_bsp(); // TODO: STUB STUB STUB !!!
}

long get_current_cpuid(void) {
    return 0; // TODO: STUB STUB STUB !!!
}