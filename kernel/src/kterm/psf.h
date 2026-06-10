#ifndef _PSF_H
#define _PSF_H

#include <stdint.h>

#define PSF1_MAGIC 0x0436

typedef struct psf1_header {
    uint16_t magic;
    uint8_t mode;
    uint8_t height;
} psf1_header_t;

typedef enum psf1_mode {
    PSF1_MODE512    = 0x01,
    PSF1_MODEHASTAB = 0x02,
    PSF1_MODESEQ    = 0x04,
} psf1_mode_t;

extern psf1_header_t *psf;

int psf_load_defaults(void);
int psf_load(void *buffer);

void psf_putc(char c, uint32_t x, uint32_t y, uint32_t fg_r, uint32_t fg_g,
             uint32_t fg_b, uint32_t bg_r, uint32_t bg_g, uint32_t bg_b);

#endif // _PSF_H