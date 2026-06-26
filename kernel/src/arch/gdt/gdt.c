#include <arch/gdt/gdt.h>
#include <arch/cpu.h>
#include <log/log.h>
#include <util/memory.h>
#include <stdint.h>

// https://github.com/Mathewnd/Astral/blob/rewrite/kernel-src/arch/x86-64/gdt.c#L8
static uint64_t template[7] = {
	0, // NULL 0x0
	0x00af9b000000ffff, // code64 0x8
	0x00af93000000ffff, // data64 0x10
	0x00eff3000000ffff, // udata64 0x18
	0x00affb000000ffff, // ucode64 0x20
    0x0000890000000000, // tss low 0x28
	0x0000000000000000, // tss high 0x30
};

void gdt_set_tss(void *tss, uint32_t size) {
    uint64_t base = (uint64_t)tss;
    uint64_t limit = size - 1;

    uint64_t low = 0;
    low |= (limit & 0xFFFF);
    low |= (base & 0xFFFFFF) << 16;
    low |= (uint64_t)0x89 << 40;
    low |= ((limit >> 16) & 0xF) << 48;
    low |= ((base >> 24) & 0xFF) << 56;

    uint64_t high = (base >> 32) & 0xFFFFFFFF;

    get_current_cpu()->gdt[5] = low;
    get_current_cpu()->gdt[6] = high;
}

void gdt_reload(void) {
    cpu_t *cpu = get_current_cpu();
    memcpy(cpu->gdt, template, sizeof(template));
    memset(&cpu->tss, 0, sizeof(cpu->tss));
    gdt_set_tss(&cpu->tss, sizeof(cpu->tss));

    gdtr_t gdtr = {
        .size = sizeof(template) - 1,
        .address = (uint64_t)&cpu->gdt
    };
    _lgdt(&gdtr);

    _reload_segs();
    _ltr(GDT_SEL_TSS);

    info("gdt (%u): reloaded GDT with %d entries, GDTR at 0x%.16llx, TSS loaded\n", cpu->id, sizeof(template) / sizeof(template[0]), (uint64_t)&gdtr);
}
