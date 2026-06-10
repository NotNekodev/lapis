#ifndef _KTERM_H
#define _KTERM_H

#include <stdint.h>

typedef struct kterm_ctx {
    uint64_t rows;
    uint64_t cols;
} kterm_ctx_t;

extern uint32_t bg_color[3];
extern uint32_t fg_color[3];

void kterm_init(void);
void kterm_render_cursor(uint64_t x, uint64_t y);
void kterm_move_cursor(uint64_t x, uint64_t y);
void kterm_clear_line(int mode);
void kterm_clear_screen(int mode);
void kterm_save_cursor(void);
void kterm_restore_cursor(void);

void kterm_putc(char c);
void kterm_puts(const char *str);
void kterm_cls(void);

void kterm_set_bg(uint32_t rgb);
void kterm_set_fg(uint32_t rgb);

#endif // _KTERM_H