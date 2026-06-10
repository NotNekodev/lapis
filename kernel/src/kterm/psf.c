#include "psf.h"
#include "kernel.h"
#include "limine.h"

#include <log/log.h>
#include <kterm/font.h>
#include <stddef.h>

psf1_header_t *psf;

int psf_load_defaults(void) {
    return psf_load((void *)ttyfont);
}

int psf_load(void *buffer) {
    psf1_header_t *header = (psf1_header_t *)buffer;
    if (header->magic != PSF1_MAGIC) {
        error("PSF1: Invalid magic number, expected 0x%04x, got 0x%04x", PSF1_MAGIC, header->magic);
        return -1;
    }

    if (!(header->mode & PSF1_MODE512) && !(header->mode & PSF1_MODEHASTAB)) {
        error("PSF1: Unsupported mode, expected either PSF1_MODE512 or PSF1_MODEHASTAB");
        return -1;
    }

    psf = header;
    return 0;
}

void psf_putc(char c, uint32_t x, uint32_t y, uint32_t fg_r, uint32_t fg_g, uint32_t fg_b, uint32_t bg_r, uint32_t bg_g, uint32_t bg_b) {
    /* Glyph table starts immediately after header */
    uint8_t *glyphs = (uint8_t *)psf + sizeof(psf1_header_t);
    unsigned char idx = (unsigned char)c;
    uint8_t *targ = glyphs + (size_t)idx * psf->height;

    uint32_t *fb = (uint32_t *)kernel_info.framebuffer->address;
    uint32_t pitch_pixels = kernel_info.framebuffer->pitch / 4;

    for (size_t i = 0; i < psf->height; i++) {
        for (size_t j = 0; j < 8; j++) {
            uint8_t mask = 1 << (7 - j);
            uint32_t color;

            if (targ[i] & mask) {
                color = (fg_r << kernel_info.framebuffer->red_mask_shift) |
                        (fg_g << kernel_info.framebuffer->green_mask_shift) |
                        (fg_b << kernel_info.framebuffer->blue_mask_shift);
            } else {
                color = (bg_r << kernel_info.framebuffer->red_mask_shift) |
                        (bg_g << kernel_info.framebuffer->green_mask_shift) |
                        (bg_b << kernel_info.framebuffer->blue_mask_shift);
            }

            fb[(y + i) * pitch_pixels + (x + j)] = color;
        }
    }
}