#include "kernel.h"
#include "kterm/psf.h"
#include "stdbool.h"
#include "stddef.h"
#include <kterm/kterm.h>
#include <limine.h>
#include <util/memory.h>

#define FONT_WIDTH 8

#define TERM_ESC_PARAMS_MAX 16

#define MAX_ROWS 256
#define MAX_COLS 256

typedef struct terminal_cell {
    char ch;
    uint32_t fg;
    uint32_t bg;
} terminal_cell_t;

typedef enum term_escape_state {
    TERM_ESC_NONE = 0,
    TERM_ESC_SEEN,
    TERM_ESC_CSI,
} term_escape_state_t;

typedef struct term_escape_ctx {
    term_escape_state_t state;
    int params[TERM_ESC_PARAMS_MAX];
    int param_count;
    bool current_param_active;
    bool private_mode;
    bool bright;
    bool cursor_visible;
    uint64_t saved_cursor_x;
    uint64_t saved_cursor_y;
} term_escape_ctx_t;

uint64_t char_cursor_x = 0;
uint64_t char_cursor_y = 0;
static uint64_t old_cursor_x = 0;
static uint64_t old_cursor_y = 0;


struct kterm_ctx terminal_ctx;
struct limine_framebuffer *framebuffer = NULL;
bool init = false;

static inline uint32_t *fb_base(void) {
    return (uint32_t *)framebuffer->address;
}

static inline uint32_t fb_pitch_pixels(void) {
    return framebuffer->pitch / 4;
}

static inline uint32_t rgb_to_pixel(uint32_t r, uint32_t g, uint32_t b) {
    return (r << framebuffer->red_mask_shift) |
           (g << framebuffer->green_mask_shift) |
           (b << framebuffer->blue_mask_shift);
}

uint32_t bg_color[3] = {0x00, 0x00, 0x00};
uint32_t fg_color[3] = {0xFF, 0xFF, 0xFF};

static terminal_cell_t screen_buffer[MAX_ROWS][MAX_COLS];

static uint64_t scroll_base = 0;
static term_escape_ctx_t esc_ctx = {
    .state = TERM_ESC_NONE,
    .bright = false,
    .cursor_visible = true,
};


static uint32_t default_bg_rgb = 0x000000;
static uint32_t default_fg_rgb = 0xAAAAAA;

static const uint32_t ansi_palette[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

static uint32_t cell_fg_rgb(void) {
    return (fg_color[0] << framebuffer->red_mask_shift) |
           (fg_color[1] << framebuffer->green_mask_shift) |
           (fg_color[2] << framebuffer->blue_mask_shift);
}

static uint32_t cell_bg_rgb(void) {
    return (bg_color[0] << framebuffer->red_mask_shift) |
           (bg_color[1] << framebuffer->green_mask_shift) |
           (bg_color[2] << framebuffer->blue_mask_shift);
}

static void rgb_to_components(uint32_t rgb, uint32_t *r, uint32_t *g,
                              uint32_t *b) {
    *r = (rgb >> framebuffer->red_mask_shift) & 0xFF;
    *g = (rgb >> framebuffer->green_mask_shift) & 0xFF;
    *b = (rgb >> framebuffer->blue_mask_shift) & 0xFF;
}

static uint32_t ansi_basic_to_rgb(int color, bool bright) {
    if (color < 0 || color > 7)
        color = 7;
    return ansi_palette[color + (bright ? 8 : 0)];
}

static uint32_t ansi_256_to_rgb(int idx) {
    if (idx < 0)
        idx = 0;
    if (idx < 16)
        return ansi_palette[idx];
    if (idx < 232) {
        int cube = idx - 16;
        int r = cube / 36;
        int g = (cube / 6) % 6;
        int b = cube % 6;
        static const uint8_t levels[6] = {0, 95, 135, 175, 215, 255};
        return ((uint32_t)levels[r] << framebuffer->red_mask_shift) |
               ((uint32_t)levels[g] << framebuffer->green_mask_shift) |
               ((uint32_t)levels[b] << framebuffer->blue_mask_shift);
    }

    uint8_t gray = (uint8_t)(8 + (idx - 232) * 10);
    return ((uint32_t)gray << framebuffer->red_mask_shift) |
           ((uint32_t)gray << framebuffer->green_mask_shift) |
           ((uint32_t)gray << framebuffer->blue_mask_shift);
}

static void reset_escape_state(void) {
    esc_ctx.state = TERM_ESC_NONE;
    esc_ctx.param_count = 0;
    esc_ctx.current_param_active = false;
    esc_ctx.private_mode = false;
    memset(esc_ctx.params, 0, sizeof(esc_ctx.params));
}

static void reset_terminal_attributes(void) {
    esc_ctx.bright = false;
    kterm_set_fg(default_fg_rgb);
    kterm_set_bg(default_bg_rgb);
}

static void draw_char_at(uint64_t x, uint64_t y, char c, uint32_t fg_rgb,
                         uint32_t bg_rgb) {
    uint64_t px = x * FONT_WIDTH;
    uint64_t py = y * psf->height;
    uint32_t fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
    rgb_to_components(fg_rgb, &fg_r, &fg_g, &fg_b);
    rgb_to_components(bg_rgb, &bg_r, &bg_g, &bg_b);
    psf_putc(c, px, py, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
}

static void clear_cell(uint64_t row, uint64_t col) {
    screen_buffer[row][col].ch = ' ';
    screen_buffer[row][col].fg = cell_fg_rgb();
    screen_buffer[row][col].bg = cell_bg_rgb();
}

void kterm_set_bg(uint32_t rgb) {
    if (!framebuffer)
        framebuffer = kernel_info.framebuffer;
    if (!framebuffer)
        return;

    bg_color[0] = (rgb >> framebuffer->red_mask_shift) & 0xFF;
    bg_color[1] = (rgb >> framebuffer->green_mask_shift) & 0xFF;
    bg_color[2] = (rgb >> framebuffer->blue_mask_shift) & 0xFF;
}

void kterm_set_fg(uint32_t rgb) {
    if (!framebuffer)
        framebuffer = kernel_info.framebuffer;
    if (!framebuffer)
        return;

    fg_color[0] = (rgb >> framebuffer->red_mask_shift) & 0xFF;
    fg_color[1] = (rgb >> framebuffer->green_mask_shift) & 0xFF;
    fg_color[2] = (rgb >> framebuffer->blue_mask_shift) & 0xFF;
}


static void render_screen() {
    for (uint64_t y = 0; y < terminal_ctx.rows; y++) {
        uint64_t real_row = (scroll_base + y) % MAX_ROWS;
        for (uint64_t x = 0; x < terminal_ctx.cols; x++) {
            terminal_cell_t *cell = &screen_buffer[real_row][x];
            draw_char_at(x, y, cell->ch ? cell->ch : ' ', cell->fg, cell->bg);
        }
    }
}

void kterm_init() {
    framebuffer = kernel_info.framebuffer;
    if (!framebuffer || !framebuffer->address)
        return;

    terminal_ctx.rows    = framebuffer->height / psf->height;
    terminal_ctx.cols = framebuffer->width / FONT_WIDTH;

    reset_terminal_attributes();
    kterm_cls();
    render_screen();
    init = true;
}

void kterm_cls() {
    if (!framebuffer || !framebuffer->address)
        return;
    uint32_t *fb = fb_base();
    uint32_t pitch = fb_pitch_pixels();
    uint32_t bg_pixel = rgb_to_pixel(bg_color[0], bg_color[1], bg_color[2]);

    for (uint64_t y = 0; y < framebuffer->height; y++) {
        uint32_t row_base = y * pitch;
        for (uint64_t x = 0; x < framebuffer->width; x++) {
            fb[row_base + x] = bg_pixel;
        }
    }

    for (uint64_t i = 0; i < MAX_ROWS; i++) {
        for (uint64_t j = 0; j < MAX_COLS; j++) {
            clear_cell(i, j);
        }
    }

    char_cursor_x = 0;
    char_cursor_y = 0;
    scroll_base   = 0;
    reset_escape_state();
    render_screen();
    kterm_render_cursor(char_cursor_x, char_cursor_y);
}

void kterm_render_cursor(uint64_t x, uint64_t y) {
    if (!init || !framebuffer)
        return;

    if (old_cursor_x != x || old_cursor_y != y) {
        uint64_t old_row = (scroll_base + old_cursor_y) % MAX_ROWS;
        terminal_cell_t *old_cell = &screen_buffer[old_row][old_cursor_x];
        draw_char_at(old_cursor_x, old_cursor_y,
                     old_cell->ch ? old_cell->ch : ' ', old_cell->fg,
                     old_cell->bg);
    }

    if (!esc_ctx.cursor_visible) {
        old_cursor_x = x;
        old_cursor_y = y;
        return;
    }

    uint64_t px = x * FONT_WIDTH;
    uint64_t py = y * psf->height;
    uint32_t *fb = fb_base();
    uint32_t pitch = fb_pitch_pixels();

    for (uint64_t i = 0; i < FONT_WIDTH; i++) {
        for (uint64_t j = 0; j < psf->height; j++) {
            uint32_t color = fb[(py + j) * pitch + (px + i)];
            uint32_t inverted_color = ((color >> framebuffer->red_mask_shift) & 0xFF) << framebuffer->red_mask_shift |
                                      ((color >> framebuffer->green_mask_shift) & 0xFF) << framebuffer->green_mask_shift |
                                      ((color >> framebuffer->blue_mask_shift) & 0xFF) << framebuffer->blue_mask_shift;
            fb[(py + j) * pitch + (px + i)] = inverted_color;
        }
    }

    old_cursor_x = x;
    old_cursor_y = y;
}


static void scroll_up_by_one() {
    scroll_base = (scroll_base + 1) % MAX_ROWS;

    uint64_t new_line = (scroll_base + terminal_ctx.rows - 1) % MAX_ROWS;
    for (uint64_t i = 0; i < MAX_COLS; i++) {
        clear_cell(new_line, i);
    }

    render_screen();
}

void kterm_move_cursor(uint64_t x, uint64_t y) {
    if (!init)
        kterm_init();

    if (x >= terminal_ctx.cols)
        x = terminal_ctx.cols ? terminal_ctx.cols - 1 : 0;
    if (y >= terminal_ctx.rows)
        y = terminal_ctx.rows ? terminal_ctx.rows - 1 : 0;

    char_cursor_x = x;
    char_cursor_y = y;
    kterm_render_cursor(char_cursor_x, char_cursor_y);
}

void kterm_save_cursor(void) {
    esc_ctx.saved_cursor_x = char_cursor_x;
    esc_ctx.saved_cursor_y = char_cursor_y;
}

void kterm_restore_cursor(void) {
    kterm_move_cursor(esc_ctx.saved_cursor_x, esc_ctx.saved_cursor_y);
}

void kterm_clear_line(int mode) {
    if (!init)
        kterm_init();

    uint64_t row = (scroll_base + char_cursor_y) % MAX_ROWS;
    uint64_t start = 0;
    uint64_t end = terminal_ctx.cols ? terminal_ctx.cols - 1 : 0;
    if (mode == 0) {
        start = char_cursor_x;
    } else if (mode == 1) {
        end = char_cursor_x;
    }

    for (uint64_t x = start; x <= end && x < terminal_ctx.cols; x++) {
        clear_cell(row, x);
    }

    render_screen();
    kterm_render_cursor(char_cursor_x, char_cursor_y);
}

void kterm_clear_screen(int mode) {
    if (!init)
        kterm_init();

    if (mode == 2 || mode == 3) {
        kterm_cls();
        return;
    }

    for (uint64_t y = 0; y < terminal_ctx.rows; y++) {
        uint64_t row = (scroll_base + y) % MAX_ROWS;
        uint64_t start = 0;
        uint64_t end = terminal_ctx.cols ? terminal_ctx.cols - 1 : 0;

        if (mode == 0) {
            if (y < char_cursor_y)
                continue;
            if (y == char_cursor_y)
                start = char_cursor_x;
        } else if (mode == 1) {
            if (y > char_cursor_y)
                continue;
            if (y == char_cursor_y)
                end = char_cursor_x;
        }

        for (uint64_t x = start; x <= end && x < terminal_ctx.cols; x++) {
            clear_cell(row, x);
        }
    }

    render_screen();
    kterm_render_cursor(char_cursor_x, char_cursor_y);
}

static void term_apply_sgr(void) {
    int i = 0;

    if (!esc_ctx.param_count) {
        reset_terminal_attributes();
        return;
    }

    while (i < esc_ctx.param_count) {
        int p = esc_ctx.params[i];

        if (p == 0) {
            reset_terminal_attributes();
        } else if (p == 1) {
            esc_ctx.bright = true;
        } else if (p == 22) {
            esc_ctx.bright = false;
        } else if (p == 7) {
            uint32_t fg = cell_fg_rgb();
            uint32_t bg = cell_bg_rgb();
            kterm_set_fg(bg);
            kterm_set_bg(fg);
        } else if (p == 27) {
            reset_terminal_attributes();
        } else if (p == 39) {
            kterm_set_fg(default_fg_rgb);
        } else if (p == 49) {
            kterm_set_bg(default_bg_rgb);
        } else if (p >= 30 && p <= 37) {
            kterm_set_fg(ansi_basic_to_rgb(p - 30, esc_ctx.bright));
        } else if (p >= 40 && p <= 47) {
            kterm_set_bg(ansi_basic_to_rgb(p - 40, false));
        } else if (p >= 90 && p <= 97) {
            kterm_set_fg(ansi_basic_to_rgb(p - 90, true));
        } else if (p >= 100 && p <= 107) {
            kterm_set_bg(ansi_basic_to_rgb(p - 100, true));
        } else if ((p == 38 || p == 48) && i + 1 < esc_ctx.param_count) {
            bool is_fg = p == 38;
            int mode = esc_ctx.params[++i];

            if (mode == 5 && i + 1 < esc_ctx.param_count) {
                uint32_t rgb = ansi_256_to_rgb(esc_ctx.params[++i]);
                if (is_fg)
                    kterm_set_fg(rgb);
                else
                    kterm_set_bg(rgb);
            } else if (mode == 2 && i + 3 < esc_ctx.param_count) {
                uint8_t r = (uint8_t)esc_ctx.params[++i];
                uint8_t g = (uint8_t)esc_ctx.params[++i];
                uint8_t b = (uint8_t)esc_ctx.params[++i];
                uint32_t rgb = ((uint32_t)r << framebuffer->red_mask_shift) |
                               ((uint32_t)g << framebuffer->green_mask_shift) |
                               ((uint32_t)b << framebuffer->blue_mask_shift);
                if (is_fg)
                    kterm_set_fg(rgb);
                else
                    kterm_set_bg(rgb);
            }
        }

        i++;
    }
}

static void term_execute_csi(char cmd) {
    int p0 = esc_ctx.param_count > 0 ? esc_ctx.params[0] : 0;
    int p1 = esc_ctx.param_count > 1 ? esc_ctx.params[1] : 0;
    uint64_t n = p0 > 0 ? (uint64_t)p0 : 1;

    switch (cmd) {
    case 'A':
        kterm_move_cursor(char_cursor_x, char_cursor_y > n ? char_cursor_y - n : 0);
        break;
    case 'B':
        kterm_move_cursor(char_cursor_x, char_cursor_y + n);
        break;
    case 'C':
        kterm_move_cursor(char_cursor_x + n, char_cursor_y);
        break;
    case 'D':
        kterm_move_cursor(char_cursor_x > n ? char_cursor_x - n : 0, char_cursor_y);
        break;
    case 'E':
        kterm_move_cursor(0, char_cursor_y + n);
        break;
    case 'F':
        kterm_move_cursor(0, char_cursor_y > n ? char_cursor_y - n : 0);
        break;
    case 'G':
        kterm_move_cursor((p0 > 0 ? (uint64_t)(p0 - 1) : 0), char_cursor_y);
        break;
    case 'H':
    case 'f': {
        uint64_t row = p0 > 0 ? (uint64_t)(p0 - 1) : 0;
        uint64_t col = p1 > 0 ? (uint64_t)(p1 - 1) : 0;
        kterm_move_cursor(col, row);
        break;
    }
    case 'J':
        kterm_clear_screen(p0);
        break;
    case 'K':
        kterm_clear_line(p0);
        break;
    case 'm':
        term_apply_sgr();
        break;
    case 's':
        kterm_save_cursor();
        break;
    case 'u':
        kterm_restore_cursor();
        break;
    case 'h':
        if (esc_ctx.private_mode && p0 == 25) {
            esc_ctx.cursor_visible = true;
            kterm_render_cursor(char_cursor_x, char_cursor_y);
        }
        break;
    case 'l':
        if (esc_ctx.private_mode && p0 == 25) {
            esc_ctx.cursor_visible = false;
            kterm_render_cursor(char_cursor_x, char_cursor_y);
        }
        break;
    default:
        break;
    }
}

static void term_handle_escape(char c) {
    switch (esc_ctx.state) {
    case TERM_ESC_SEEN:
        if (c == '[') {
            esc_ctx.state = TERM_ESC_CSI;
            esc_ctx.param_count = 0;
            esc_ctx.current_param_active = false;
            esc_ctx.private_mode = false;
            memset(esc_ctx.params, 0, sizeof(esc_ctx.params));
        } else {
            reset_escape_state();
        }
        return;
    case TERM_ESC_CSI:
        if (c == '?') {
            esc_ctx.private_mode = true;
            return;
        }
        if (c >= '0' && c <= '9') {
            if (!esc_ctx.current_param_active && esc_ctx.param_count < TERM_ESC_PARAMS_MAX) {
                esc_ctx.current_param_active = true;
                esc_ctx.params[esc_ctx.param_count++] = 0;
            }
            if (esc_ctx.param_count > 0) {
                esc_ctx.params[esc_ctx.param_count - 1] =
                    esc_ctx.params[esc_ctx.param_count - 1] * 10 + (c - '0');
            }
            return;
        }
        if (c == ';') {
            if (!esc_ctx.current_param_active && esc_ctx.param_count < TERM_ESC_PARAMS_MAX) {
                esc_ctx.params[esc_ctx.param_count++] = 0;
            }
            esc_ctx.current_param_active = false;
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            term_execute_csi(c);
        }
        reset_escape_state();
        return;
    default:
        reset_escape_state();
        return;
    }
}

static void term_raw_putc(char c) {
    if (c == '\n') {
        char_cursor_x = 0;
        char_cursor_y++;
    } else if (c == '\r') {
        char_cursor_x = 0;
    } else if (c == '\b') {
        if (char_cursor_x > 0) {
            char_cursor_x--;
            uint64_t row = (scroll_base + char_cursor_y) % MAX_ROWS;
            clear_cell(row, char_cursor_x);
            draw_char_at(char_cursor_x, char_cursor_y,
                         screen_buffer[row][char_cursor_x].ch,
                         screen_buffer[row][char_cursor_x].fg,
                         screen_buffer[row][char_cursor_x].bg);
        }
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            term_raw_putc(' ');
        }
        return;
    } else {
        uint64_t row = (scroll_base + char_cursor_y) % MAX_ROWS;
        terminal_cell_t *cell = &screen_buffer[row][char_cursor_x];
        cell->ch = c;
        cell->fg = cell_fg_rgb();
        cell->bg = cell_bg_rgb();
        draw_char_at(char_cursor_x, char_cursor_y, cell->ch, cell->fg, cell->bg);
        char_cursor_x++;
    }

    if (char_cursor_x >= terminal_ctx.cols) {
        char_cursor_x = 0;
        char_cursor_y++;
    }

    if (char_cursor_y >= terminal_ctx.rows) {
        char_cursor_y = terminal_ctx.rows - 1;
        scroll_up_by_one();
    }

    kterm_render_cursor(char_cursor_x, char_cursor_y);
}

void kterm_putc(char c) {
    if (!init)
        kterm_init();

    if (esc_ctx.state != TERM_ESC_NONE) {
        term_handle_escape(c);
        return;
    }

    if (c == '\033') {
        esc_ctx.state = TERM_ESC_SEEN;
        return;
    }

    term_raw_putc(c);
}

void kterm_puts(const char *str) {
    if (!init)
        kterm_init();
    while (*str) {
        kterm_putc(*str++);
    }
}
