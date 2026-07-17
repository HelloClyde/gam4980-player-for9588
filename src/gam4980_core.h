#ifndef GAM4980_CORE_H
#define GAM4980_CORE_H

#include "bda_sdk.h"

#define GAM4980_LCD_WIDTH 159
#define GAM4980_LCD_HEIGHT 96
#define GAM4980_LCD_STRIDE (GAM4980_LCD_WIDTH + 1)
#define GAM4980_LCD_PACKED_STRIDE (GAM4980_LCD_STRIDE / 8)
#define GAM4980_LCD_PACKED_SIZE \
    (GAM4980_LCD_PACKED_STRIDE * GAM4980_LCD_HEIGHT)
#define GAM4980_ROM_SIZE 0x200000u
#define GAM4980_RAM_SIZE 0x8000u
#define GAM4980_FLASH_SIZE 0x200000u
#define GAM4980_SAVE_SIZE 0x14000u
#define GAM4980_GAME_MAX_SIZE 0x1e0000u
#define GAM4980_GAME_HEADER_SIZE 0x46u

enum gam4980_key {
    GAM4980_KEY_HELP = 0x29,
    GAM4980_KEY_SEARCH = 0x2a,
    GAM4980_KEY_MODIFY = 0x2c,
    GAM4980_KEY_DELETE = 0x2d,
    GAM4980_KEY_EXIT = 0x2e,
    GAM4980_KEY_ENTER = 0x2f,
    GAM4980_KEY_UP = 0x35,
    GAM4980_KEY_LEFT = 0x37,
    GAM4980_KEY_DOWN = 0x38,
    GAM4980_KEY_RIGHT = 0x39,
    GAM4980_KEY_PAGE_UP = 0x3a,
    GAM4980_KEY_PAGE_DOWN = 0x3b,
};

typedef struct gam4980_buffers {
    u8 *ram;
    u8 *flash;
    u8 *rom_8;
    u8 *rom_e;
    u16 *framebuffer;
} gam4980_buffers_t;

int gam4980_init(const gam4980_buffers_t *buffers);
void gam4980_deinit(void);
u8 *gam4980_game_storage(void);
int gam4980_load_game_header(const u8 *header, u32 size);
void gam4980_key_down(u8 key);
void gam4980_step_frame(void);
int gam4980_render_frame(void);
void gam4980_run_frame(void);
int gam4980_cpu_halted(void);
const u8 *gam4980_packed_frame(void);
const u16 *gam4980_expand_frame(const u8 *packed_frame);
const u16 *gam4980_framebuffer(void);
u8 *gam4980_save_data(void);
int gam4980_save_dirty(void);
void gam4980_save_mark_clean(void);
int gam4980_shutdown_requested(void);
u16 gam4980_shutdown_pc(void);

#endif
