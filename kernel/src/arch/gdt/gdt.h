#ifndef _GDT_H
#define _GDT_H 1

#include <stdint.h>

typedef struct gdtr {
	uint16_t size;
	uint64_t address;
} __attribute__((packed)) gdtr_t;

typedef struct tss_desc {
	uint16_t length;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t flags1;
	uint8_t flags2;
	uint8_t base_high;
	uint32_t base_upper;
	uint32_t reserved;
} __attribute__((packed)) tss_desc_t;

#define GDT_SEL_NULL  0x00
#define GDT_SEL_CODE64 0x08
#define GDT_SEL_DATA64 0x10
#define GDT_SEL_UDATA64 0x18
#define GDT_SEL_UCODE64 0x20
#define GDT_SEL_TSS 0x28

void gdt_reload(void);
void gdt_set_tss(void *tss, uint32_t size);
void _lgdt(void *ptr);
void _ltr(uint8_t sel);
void _reload_segs(void);
#endif // _GDT_H
