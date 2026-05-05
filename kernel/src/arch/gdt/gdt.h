#ifndef _GDT_H
#define _GDT_H 1

#include <stdint.h>

typedef struct gdtr {
	uint16_t size;
	uint64_t address;
} __attribute__((packed)) gdtr_t;

void gdt_reload(void);

void _lgdt(void *ptr);
void _ltr(uint8_t sel);
void _reload_segs(void); // this sounds very VERY wrong

#endif // _GDT_H