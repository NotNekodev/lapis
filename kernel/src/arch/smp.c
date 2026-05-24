#include "log/log.h"
#include <arch/smp.h>

#include <arch/cpu.h>
#include <arch/gdt/gdt.h>
#include <arch/interrupts/idt.h>
#include <arch/io.h>
#include <limine.h>
#include <stdint.h>
#include <stddef.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = 0
};

static cpu_t cpu_list[256];
static cpu_t bsp_cpu_fallback;
static cpu_t *bsp_cpu_ptr = &bsp_cpu_fallback;
static struct limine_mp_response *mp_response;
static uint32_t cpu_count;
static volatile uint32_t cpu_started_count;

static void ap_entry(struct limine_mp_info *info) {
    cpu_t *cpu = (cpu_t *)(uintptr_t)info->extra_argument;
    cpu_set_current(cpu);
    debug("smp: launched AP %u\n", cpu->id);
    gdt_reload();
    idt_reload();
    __atomic_add_fetch(&cpu_started_count, 1, __ATOMIC_SEQ_CST);

    for (;;) {
        _hlt();
    }
}

void smp_prepare(void) {
    if (mp_request.response == NULL || mp_request.response->cpu_count == 0) {
        bsp_cpu_ptr = &bsp_cpu_fallback;
        cpu_set_current(bsp_cpu_ptr);
        __atomic_store_n(&cpu_started_count, 1, __ATOMIC_SEQ_CST);
        return;
    }

    mp_response = mp_request.response;
    cpu_count = (uint32_t)mp_response->cpu_count;
    if (cpu_count > 256) {
        cpu_count = 256;
    }

    for (uint32_t i = 0; i < cpu_count; ++i) {
        struct limine_mp_info *info = mp_response->cpus[i];
        cpu_t *cpu = &cpu_list[i];
        cpu->self = cpu;
        cpu->id = info->processor_id;
        cpu->lapic_id = info->lapic_id;
        info->extra_argument = (uint64_t)cpu;
    }

    bsp_cpu_ptr = &cpu_list[0];
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (mp_response->cpus[i]->lapic_id == mp_response->bsp_lapic_id) {
            bsp_cpu_ptr = &cpu_list[i];
            break;
        }
    }

    cpu_set_current(bsp_cpu_ptr);
    __atomic_store_n(&cpu_started_count, 1, __ATOMIC_SEQ_CST);
}

void smp_start_aps(void) {
    if (mp_response == NULL) {
        return;
    }

    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (&cpu_list[i] == bsp_cpu_ptr) {
            continue;
        }
        mp_response->cpus[i]->goto_address = ap_entry;
    }

    while (__atomic_load_n(&cpu_started_count, __ATOMIC_SEQ_CST) < cpu_count) {
        __asm__ volatile("pause");
    }
}

cpu_t *get_bsp(void) {
    return bsp_cpu_ptr;
}
