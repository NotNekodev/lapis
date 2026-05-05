#include <arch/gdt/gdt.h>

#include <util/memory.h>

#include <stdint.h>

// https://github.com/Mathewnd/Astral/blob/rewrite/kernel-src/arch/x86-64/gdt.c#L8
static uint64_t template[9] = {
	0, // NULL 0x0
	0x00af9b000000ffff, // code64 0x8
	0x00af93000000ffff, // data64 0x10
	0x00eff3000000ffff, // udata64 0x18
	0x00affb000000ffff, // ucode64 0x20
};

void gdt_reload(void) {
    // TODO: replace with per cpu gdt struct
    gdtr_t gdtr = {
        .size = sizeof(template) - 1,
        .address = (uint64_t)template
    };

    // TODO: get kernel rsp at the start of kmain, store it and load it into the ist here

    _lgdt(&gdtr);
    _reload_segs();
}   