#include <arch/cpu.h>

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

void cpu_set_current(cpu_t *cpu) {
    cpu->self = cpu;
    write_msr(0xC0000101, (uint64_t)cpu);
}

cpu_t *get_current_cpu(void) {
    cpu_t *cpu;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(cpu));
    return cpu;
}

long get_current_cpuid(void) {
    return (long)get_current_cpu()->id;
}