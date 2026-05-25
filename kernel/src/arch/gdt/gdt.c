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
    0x0020890000000000, // low ist 0x28 TODO: do something
	0x0000000000000000, // high ist TODO: do something
};

void gdt_reload(void) {
    memcpy(get_current_cpu()->gdt, template, sizeof(template));

    // TODO: ist shenanigans

    gdtr_t gdtr = {
        .size = sizeof(template) - 1,
        .address = (uint64_t)&get_current_cpu()->gdt
    };

    _lgdt(&gdtr);
    _reload_segs();

    info("gdt (%u): reloaded GDT with %d entries, GDTR at 0x%.16llx\n", get_current_cpu()->id, sizeof(template) / sizeof(template[0]), (uint64_t)&gdtr);
}   