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
	0x00eff3000000ffff, // udata64 0x1b
	0x00affb000000ffff, // ucode64 0x20
    0x0020890000000000, // tss low 0x2b
	0x0000000000000000, // tss high 0x30
};

void gdt_set_tss(void *tss) {
    uintptr_t base = (uintptr_t)tss;
    uint32_t limit = sizeof(ist_t) - 1;

    uint64_t low = 0;

    low |= (limit & 0xffff);
    low |= (base & 0xffff) << 16;
    low |= ((base >> 16) & 0xff) << 32;
    low |= (0x89ULL) << 40;
    low |= ((limit >> 16) & 0xf) << 48;
    low |= ((base >> 24) & 0xff) << 56;

    get_current_cpu()->gdt[5] = low;
    get_current_cpu()->gdt[6] = base >> 32;
}

void gdt_reload(void) {
    cpu_t *cpu = get_current_cpu();
    memcpy(cpu->gdt, template, sizeof(template));
    memset(&cpu->tss, 0, sizeof(cpu->tss));
    gdt_set_tss(&cpu->tss);

    gdtr_t gdtr = {
        .size = sizeof(template) - 1,
        .address = (uint64_t)&cpu->gdt
    };
    _lgdt(&gdtr);

    _reload_segs();
    _ltr(GDT_SEL_TSS);

    info("gdt (%u): reloaded GDT with %d entries, GDTR at 0x%.16llx, TSS loaded\n", cpu->id, sizeof(template) / sizeof(template[0]), (uint64_t)&gdtr);
}
