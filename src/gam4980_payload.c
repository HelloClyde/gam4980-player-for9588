#include "bda_sdk.h"
#include "gam4980_core.h"

/* GCC may lower small initialized-array copies to these freestanding symbols. */
void *memcpy(void *destination, const void *source, bda_size_t size)
{
    bda_memcpy(destination, source, size);
    return destination;
}

void *memset(void *destination, int value, bda_size_t size)
{
    bda_memset(destination, value, size);
    return destination;
}

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define VX_HEADER_SIZE 24u
#define SCREEN_VX_BYTES (VX_HEADER_SIZE + SCREEN_WIDTH * SCREEN_HEIGHT * 2u)
#define LCD_SOURCE_WIDTH (GAM4980_LCD_WIDTH + 1)
#define CORE_FRAME_BYTES (LCD_SOURCE_WIDTH * GAM4980_LCD_HEIGHT * 2u)

#define VIEW_X 0
#define VIEW_Y 9
#define VIEW_WIDTH SCREEN_WIDTH
#define VIEW_HEIGHT 145
#define VIEW_FRAME_BYTES (VIEW_WIDTH * VIEW_HEIGHT * 2u)

#define TOUCH_QUEUE_SIZE 16u
#define ESCAPE_QUIT_TICKS 40u
#define SAVE_INTERVAL_TICKS 400u
#define MAX_CATCHUP_TICKS 8u

typedef struct touch_button {
    s32 x;
    s32 y;
    s32 width;
    s32 height;
    u8 key;
    const char *label;
} touch_button_t;

static const char k_window_title[] = "GAM4980";
static const char k_game_dir[] = "A:\\gam4980";
static const char k_game_selector_path[] = "A:\\gam4980\\";
static const char k_data_dir[] =
    "A:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\gam4980";
static const char k_rom_8_path[] =
    "A:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\gam4980\\8.BIN";
static const char k_rom_e_path[] =
    "A:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\gam4980\\E.BIN";
static const char k_log_path[] =
    "A:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\gam4980\\GAM4980.LOG";

static const touch_button_t k_buttons[] = {
    { 54, 166, 42, 42, GAM4980_KEY_UP, 0 },
    { 10, 210, 42, 42, GAM4980_KEY_LEFT, 0 },
    { 98, 210, 42, 42, GAM4980_KEY_RIGHT, 0 },
    { 54, 254, 42, 42, GAM4980_KEY_DOWN, 0 },
    { 150, 166, 82, 54, GAM4980_KEY_ENTER, "ENTER" },
    { 150, 228, 82, 42, GAM4980_KEY_EXIT, "EXIT" },
    { 150, 278, 38, 34, GAM4980_KEY_PAGE_UP, "PGUP" },
    { 194, 278, 38, 34, GAM4980_KEY_PAGE_DOWN, "PGDN" },
};

static const u8 k_font[36][7] = {
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
    {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}, {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e},
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
    {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f}, {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
    {0x0f,0x10,0x10,0x17,0x11,0x11,0x0f}, {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, {0x01,0x01,0x01,0x01,0x11,0x11,0x0e},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, {0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, {0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a}, {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f},
};

static gam4980_buffers_t g_buffers;
static bda_file_selector_t g_file_selector;
static bda_gui_picture_t g_lcd_picture;
static u16 *g_scaled_framebuffer;
static u8 *g_screen_vx;
static bda_handle_t g_frame;
static bda_handle_t g_draw;
static bda_handle_t g_draw_owner;
static bda_handle_t g_back;
static void *g_draw_object;
static u32 g_touch_queue[TOUCH_QUEUE_SIZE];
static volatile u32 g_touch_read;
static volatile u32 g_touch_write;
static volatile int g_detached;
static u8 g_scale_x[VIEW_WIDTH];
static u8 g_scale_y[VIEW_HEIGHT];
static char g_save_path[160];
static u32 g_previous_keys;
static u32 g_hold_frames[6];
static u32 g_escape_down_tick;
static u32 g_frame_count;
static u32 g_loaded_game_size;
static u32 g_core_frame_phase;
static int g_full_redraw;
static int g_escape_pending;
static int g_close_requested;
static int g_core_break_logged;

static void copy_text(char *out, const char *text, u32 capacity)
{
    u32 index = 0;

    if (!capacity)
        return;
    if (text) {
        while (text[index] && index + 1u < capacity) {
            out[index] = text[index];
            ++index;
        }
    }
    out[index] = 0;
}

static u32 text_length(const char *text)
{
    u32 length = 0;
    while (text[length])
        ++length;
    return length;
}

static void log_line(const char *text)
{
    static const char newline[] = "\r\n";
    int file = bda_fs_fopen_raw(k_log_path, "ab");

    if (!bda_fs_file_is_valid(file))
        return;
    (void)bda_fs_write_raw(file, text, text_length(text));
    (void)bda_fs_write_raw(file, newline, 2);
    (void)bda_fs_close_raw(file);
}

static void log_hex_value(const char *prefix, u32 value)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[48];
    u32 length;
    int shift;

    copy_text(line, prefix, sizeof(line));
    length = text_length(line);
    for (shift = 28; shift >= 0 && length + 1u < sizeof(line); shift -= 4)
        line[length++] = hex[(value >> shift) & 0x0fu];
    line[length] = 0;
    log_line(line);
}

static void reset_log(void)
{
    int file = bda_fs_fopen_raw(k_log_path, "wb");
    if (bda_fs_file_is_valid(file))
        (void)bda_fs_close_raw(file);
}

static int valid_pointer(const void *pointer)
{
    return pointer && (s32)(u32)pointer != -1;
}

static void release_buffers(void)
{
    gam4980_deinit();
    if (valid_pointer(g_screen_vx))
        bda_free(g_screen_vx);
    if (valid_pointer(g_scaled_framebuffer))
        bda_free(g_scaled_framebuffer);
    if (valid_pointer(g_buffers.framebuffer))
        bda_free(g_buffers.framebuffer);
    if (valid_pointer(g_buffers.ram))
        bda_free(g_buffers.ram);
    if (valid_pointer(g_buffers.flash))
        bda_free(g_buffers.flash);
    if (valid_pointer(g_buffers.rom_8))
        bda_free(g_buffers.rom_8);
    if (valid_pointer(g_buffers.rom_e))
        bda_free(g_buffers.rom_e);
    bda_memset(&g_buffers, 0, sizeof(g_buffers));
    g_scaled_framebuffer = 0;
    g_screen_vx = 0;
}

static int allocate_buffers(void)
{
    bda_memset(&g_buffers, 0, sizeof(g_buffers));
    g_scaled_framebuffer = 0;
    g_screen_vx = 0;

    g_buffers.ram = (u8 *)bda_alloc(GAM4980_RAM_SIZE);
    g_buffers.flash = (u8 *)bda_alloc(GAM4980_FLASH_SIZE);
    g_buffers.rom_8 = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.rom_e = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.framebuffer = (u16 *)bda_alloc(CORE_FRAME_BYTES);
    g_scaled_framebuffer = (u16 *)bda_alloc(VIEW_FRAME_BYTES);
    g_screen_vx = (u8 *)bda_alloc(SCREEN_VX_BYTES);
    if (!valid_pointer(g_buffers.ram) || !valid_pointer(g_buffers.flash) ||
        !valid_pointer(g_buffers.rom_8) || !valid_pointer(g_buffers.rom_e) ||
        !valid_pointer(g_buffers.framebuffer) ||
        !valid_pointer(g_scaled_framebuffer) || !valid_pointer(g_screen_vx)) {
        release_buffers();
        return 0;
    }
    {
        u32 index;
        /* Map the active 159x96 LCD; the 160th source pixel is stride padding. */
        for (index = 0; index < VIEW_WIDTH; ++index)
            g_scale_x[index] = (u8)(index * GAM4980_LCD_WIDTH / VIEW_WIDTH);
        for (index = 0; index < VIEW_HEIGHT; ++index)
            g_scale_y[index] = (u8)(index * GAM4980_LCD_HEIGHT / VIEW_HEIGHT);
    }
    bda_memset(&g_lcd_picture, 0, sizeof(g_lcd_picture));
    g_lcd_picture.width = VIEW_WIDTH;
    g_lcd_picture.height = VIEW_HEIGHT;
    g_lcd_picture.source_pixels = g_scaled_framebuffer;
    g_lcd_picture.selected_index = -1;
    return 1;
}

static int read_exact(int file, u8 *out, u32 size)
{
    u32 total = 0;

    while (total < size) {
        int got = bda_fs_read_raw(file, out + total, size - total);
        if (got <= 0)
            return 0;
        total += (u32)got;
    }
    return 1;
}

static int write_exact(int file, const u8 *data, u32 size)
{
    u32 total = 0;

    while (total < size) {
        int wrote = bda_fs_write_raw(file, data + total, size - total);
        if (wrote <= 0)
            return 0;
        total += (u32)wrote;
    }
    return 1;
}

static int load_fixed_file(const char *path, u8 *out, u32 expected_size)
{
    int file = bda_fs_fopen_raw(path, "rb");
    int size;
    int ok;

    if (!bda_fs_file_is_valid(file))
        return 0;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) < 0) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    size = bda_fs_tell_raw(file);
    if (size != (int)expected_size || bda_fs_seek_raw(file, 0, BDA_SEEK_SET) < 0) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    ok = read_exact(file, out, expected_size);
    (void)bda_fs_close_raw(file);
    return ok;
}

static u32 fnv1a(const u8 *data, u32 size, u32 hash)
{
    u32 index;
    for (index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}

static void make_save_path(const u8 *header, u32 size)
{
    static const char hex[] = "0123456789ABCDEF";
    u32 hash = fnv1a(header, GAM4980_GAME_HEADER_SIZE, 2166136261u);
    char *out = g_save_path;
    int shift;

    hash = fnv1a((const u8 *)&size, sizeof(size), hash);
    copy_text(g_save_path, k_data_dir, sizeof(g_save_path));
    out += text_length(out);
    *out++ = '\\';
    *out++ = 'S';
    for (shift = 28; shift >= 0; shift -= 4)
        *out++ = hex[(hash >> shift) & 0x0f];
    copy_text(out, ".SAV", (u32)(g_save_path + sizeof(g_save_path) - out));
}

static void load_save(void)
{
    int file = bda_fs_fopen_raw(g_save_path, "rb");
    int size;

    if (!bda_fs_file_is_valid(file))
        return;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) < 0) {
        (void)bda_fs_close_raw(file);
        return;
    }
    size = bda_fs_tell_raw(file);
    if (size == (int)GAM4980_SAVE_SIZE &&
        bda_fs_seek_raw(file, 0, BDA_SEEK_SET) >= 0 &&
        read_exact(file, gam4980_save_data(), GAM4980_SAVE_SIZE)) {
        log_line("SAVE LOADED");
    }
    (void)bda_fs_close_raw(file);
}

static void write_save(void)
{
    int file;
    int ok;

    if (!g_save_path[0] || !gam4980_save_data() || !gam4980_save_dirty())
        return;
    file = bda_fs_fopen_raw(g_save_path, "wb");
    if (!bda_fs_file_is_valid(file)) {
        log_line("SAVE OPEN FAILED");
        return;
    }
    ok = write_exact(file, gam4980_save_data(), GAM4980_SAVE_SIZE);
    if (!ok)
        log_line("SAVE WRITE FAILED");
    (void)bda_fs_close_raw(file);
    if (ok) {
        gam4980_save_mark_clean();
        log_line("SAVE WRITTEN");
    }
}

static int load_game(const char *path)
{
    u8 header[GAM4980_GAME_HEADER_SIZE];
    int file = bda_fs_fopen_raw(path, "rb");
    int size;

    if (!bda_fs_file_is_valid(file))
        return -1;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) < 0) {
        (void)bda_fs_close_raw(file);
        return -2;
    }
    size = bda_fs_tell_raw(file);
    if (size < (int)GAM4980_GAME_HEADER_SIZE ||
        size > (int)GAM4980_GAME_MAX_SIZE ||
        bda_fs_seek_raw(file, 0, BDA_SEEK_SET) < 0 ||
        !read_exact(file, header, GAM4980_GAME_HEADER_SIZE) ||
        bda_fs_seek_raw(file, 0, BDA_SEEK_SET) < 0 ||
        !read_exact(file, gam4980_game_storage(), (u32)size)) {
        (void)bda_fs_close_raw(file);
        return -3;
    }
    (void)bda_fs_close_raw(file);
    if (gam4980_load_game_header(header, (u32)size) <= 0)
        return -4;
    g_loaded_game_size = (u32)size;
    make_save_path(header, (u32)size);
    load_save();
    gam4980_save_mark_clean();
    return 1;
}

static void write_u16_le(u8 *out, u16 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
}

static void write_u32_le(u8 *out, u32 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
    out[2] = (u8)(value >> 16);
    out[3] = (u8)(value >> 24);
}

static u16 rgb565(u32 red, u32 green, u32 blue)
{
    return (u16)(((red & 0xf8u) << 8) | ((green & 0xfcu) << 3) | (blue >> 3));
}

static void write_vx_header(u8 *image, u32 width, u32 height)
{
    int index;

    bda_memset(image, 0, VX_HEADER_SIZE);
    image[0] = 'V';
    image[1] = 'X';
    for (index = 2; index < 6; ++index)
        image[index] = 0xcc;
    write_u32_le(image + 6, width);
    write_u32_le(image + 10, height);
    for (index = 14; index < 20; ++index)
        image[index] = 0xcc;
    for (index = 20; index < (int)VX_HEADER_SIZE; ++index)
        image[index] = 0xff;
}

static void init_full_vx(void)
{
    bda_memset(g_screen_vx, 0, SCREEN_VX_BYTES);
    write_vx_header(g_screen_vx, SCREEN_WIDTH, SCREEN_HEIGHT);
}

static void put_pixel(int x, int y, u16 color)
{
    u32 offset;
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    offset = VX_HEADER_SIZE + (u32)(y * SCREEN_WIDTH + x) * 2u;
    write_u16_le(g_screen_vx + offset, color);
}

static void fill_rect(int x, int y, int width, int height, u16 color)
{
    int px;
    int py;
    for (py = y; py < y + height; ++py)
        for (px = x; px < x + width; ++px)
            put_pixel(px, py, color);
}

static int glyph_index(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'A' && character <= 'Z')
        return 10 + character - 'A';
    return -1;
}

static void draw_text(int x, int y, const char *text, int scale, u16 color)
{
    while (*text) {
        int glyph = glyph_index(*text++);
        int row;
        int column;
        if (glyph >= 0) {
            for (row = 0; row < 7; ++row) {
                for (column = 0; column < 5; ++column) {
                    if (k_font[glyph][row] & (1u << (4 - column)))
                        fill_rect(x + column * scale, y + row * scale,
                                  scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static int label_width(const char *text, int scale)
{
    return (int)text_length(text) * 6 * scale - scale;
}

static void draw_arrow(const touch_button_t *button)
{
    int cx = button->x + button->width / 2;
    int cy = button->y + button->height / 2;
    u16 color = rgb565(238, 244, 239);
    int row;

    if (button->key == GAM4980_KEY_UP || button->key == GAM4980_KEY_DOWN) {
        for (row = 0; row < 11; ++row) {
            int width = row * 2 + 1;
            int y = button->key == GAM4980_KEY_UP ? cy - 12 + row : cy + 12 - row;
            fill_rect(cx - width / 2, y, width, 1, color);
        }
        fill_rect(cx - 3, cy - 1, 7, 13, color);
    } else {
        for (row = 0; row < 11; ++row) {
            int height = row * 2 + 1;
            int x = button->key == GAM4980_KEY_LEFT ? cx - 12 + row : cx + 12 - row;
            fill_rect(x, cy - height / 2, 1, height, color);
        }
        fill_rect(cx - 1, cy - 3, 13, 7, color);
        if (button->key == GAM4980_KEY_RIGHT)
            fill_rect(cx - 11, cy - 3, 13, 7, color);
    }
}

static void draw_button(const touch_button_t *button)
{
    u16 border = rgb565(41, 178, 178);
    u16 fill = rgb565(27, 45, 55);
    u16 text = rgb565(238, 244, 239);
    int scale = button->width >= 70 ? 2 : 1;

    fill_rect(button->x, button->y, button->width, button->height, border);
    fill_rect(button->x + 2, button->y + 2,
              button->width - 4, button->height - 4, fill);
    if (button->label) {
        int width = label_width(button->label, scale);
        draw_text(button->x + (button->width - width) / 2,
                  button->y + (button->height - 7 * scale) / 2,
                  button->label, scale, text);
    } else {
        draw_arrow(button);
    }
}

static void scale_lcd_to_view(void)
{
    const u16 *source = gam4980_framebuffer();
    int y;

    for (y = 0; y < VIEW_HEIGHT; ++y) {
        const u16 *source_row = source + g_scale_y[y] * LCD_SOURCE_WIDTH;
        u16 *destination = g_scaled_framebuffer + y * VIEW_WIDTH;
        int x;

        for (x = 0; x < VIEW_WIDTH; ++x)
            destination[x] = source_row[g_scale_x[x]];
    }
}

static void copy_lcd_to_full_vx(void)
{
    int y;

    for (y = 0; y < VIEW_HEIGHT; ++y) {
        u8 *destination = g_screen_vx + VX_HEADER_SIZE +
            (u32)((VIEW_Y + y) * SCREEN_WIDTH + VIEW_X) * 2u;
        bda_memcpy(destination, g_scaled_framebuffer + y * VIEW_WIDTH,
                   VIEW_WIDTH * 2u);
    }
}

static void render_game_screen(void)
{
    u16 background = rgb565(10, 20, 27);
    u16 frame = rgb565(238, 177, 45);
    u32 index;

    init_full_vx();
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, background);
    fill_rect(0, VIEW_Y - 3, SCREEN_WIDTH, VIEW_HEIGHT + 6,
              rgb565(36, 55, 63));
    fill_rect(0, VIEW_Y - 2, SCREEN_WIDTH, VIEW_HEIGHT + 4, frame);
    scale_lcd_to_view();
    copy_lcd_to_full_vx();
    for (index = 0; index < sizeof(k_buttons) / sizeof(k_buttons[0]); ++index)
        draw_button(&k_buttons[index]);
}

static void release_draw_context(void)
{
    bda_handle_t draw = g_draw;
    if (!draw || (s32)draw == -1) {
        g_draw = 0;
        g_draw_owner = 0;
        return;
    }
    g_draw = 0;
    g_draw_owner = 0;
    bda_gui_end_draw(draw);
}

static int acquire_draw_context(bda_handle_t owner)
{
    if (g_draw && g_draw_owner == owner)
        return 1;
    release_draw_context();
    g_draw = bda_gui_current_draw(owner);
    if (!g_draw || (s32)g_draw == -1) {
        g_draw = 0;
        return 0;
    }
    g_draw_owner = owner;
    return 1;
}

static int present_back_rect(int x, int y, int width, int height)
{
    void *old_object;
    int copy_result;

    if (!g_draw || !g_back || !g_draw_object)
        return 0;
    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    copy_result = bda_gui_context_copy(
        g_back, x, y, width, height, g_draw, x, y, 0
    );
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    return copy_result == 0;
}

static int present_screen(void)
{
    int draw_result;
    int copy_result;

    if (!g_draw || !g_back || !g_draw_object)
        return 0;
    if (g_full_redraw) {
        render_game_screen();
        draw_result = bda_gui_draw_vx(g_back, 0, 0, g_screen_vx);
        copy_result = draw_result == 0 ?
            present_back_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) : 0;
        if (draw_result == 0 && copy_result)
            g_full_redraw = 0;
        return draw_result == 0 && copy_result;
    }

    (void)bda_gui_draw_guard_begin();
    scale_lcd_to_view();
    g_lcd_picture.source_pixels = g_scaled_framebuffer;
    g_lcd_picture.selected_index = -1;
    {
        void *old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
        draw_result = bda_gui_render_picture(
            g_draw, VIEW_X, VIEW_Y, VIEW_WIDTH, VIEW_HEIGHT, &g_lcd_picture
        );
        (void)bda_gui_select_draw_object(g_draw, old_object);
    }
    (void)bda_gui_draw_guard_end();
    return draw_result == 0;
}

static int point_in_button(int x, int y, const touch_button_t *button)
{
    return x >= button->x && x < button->x + button->width &&
           y >= button->y && y < button->y + button->height;
}

static void queue_touch(u32 packed)
{
    u32 next = (g_touch_write + 1u) % TOUCH_QUEUE_SIZE;
    if (next == g_touch_read)
        return;
    g_touch_queue[g_touch_write] = packed;
    g_touch_write = next;
}

static void drain_touches(void)
{
    while (g_touch_read != g_touch_write) {
        u32 packed = g_touch_queue[g_touch_read];
        int x = (s32)(short)(packed & 0xffffu);
        int y = (s32)(short)(packed >> 16);
        u32 index;

        g_touch_read = (g_touch_read + 1u) % TOUCH_QUEUE_SIZE;
        for (index = 0; index < sizeof(k_buttons) / sizeof(k_buttons[0]); ++index) {
            if (point_in_button(x, y, &k_buttons[index])) {
                gam4980_key_down(k_buttons[index].key);
                break;
            }
        }
    }
}

static u32 packet_mask(const bda_gui_input_packet_t *packet)
{
    u32 mask = 0;
    if (packet->bytes[BDA_INPUT_PACKET_RIGHT_INDEX] == 1u) mask |= 1u << 0;
    if (packet->bytes[BDA_INPUT_PACKET_LEFT_INDEX] == 1u) mask |= 1u << 1;
    if (packet->bytes[BDA_INPUT_PACKET_DOWN_INDEX] == 1u) mask |= 1u << 2;
    if (packet->bytes[BDA_INPUT_PACKET_UP_INDEX] == 1u) mask |= 1u << 3;
    if (packet->bytes[BDA_INPUT_PACKET_ESCAPE_INDEX] == 1u) mask |= 1u << 4;
    if (packet->bytes[BDA_INPUT_PACKET_ENTER_INDEX] == 1u) mask |= 1u << 5;
    return mask;
}

static u8 packet_key(unsigned index)
{
    static const u8 keys[6] = {
        GAM4980_KEY_RIGHT, GAM4980_KEY_LEFT, GAM4980_KEY_DOWN,
        GAM4980_KEY_UP, GAM4980_KEY_EXIT, GAM4980_KEY_ENTER,
    };
    return keys[index];
}

static void poll_game_keys(u32 now)
{
    bda_gui_input_packet_t packet;
    u32 current;
    u32 pressed;
    u32 released;
    unsigned index;

    (void)bda_gui_input_packet(&packet);
    current = packet_mask(&packet);
    pressed = current & ~g_previous_keys;
    released = g_previous_keys & ~current;

    for (index = 0; index < 6; ++index) {
        u32 bit = 1u << index;
        if (index == 4)
            continue;
        if (pressed & bit) {
            g_hold_frames[index] = 0;
            gam4980_key_down(packet_key(index));
        } else if (current & bit) {
            ++g_hold_frames[index];
            if (g_hold_frames[index] >= 12u &&
                ((g_hold_frames[index] - 12u) % 3u) == 0u)
                gam4980_key_down(packet_key(index));
        } else {
            g_hold_frames[index] = 0;
        }
    }

    if (pressed & (1u << 4)) {
        g_escape_pending = 1;
        g_escape_down_tick = now;
    }
    if (g_escape_pending && (current & (1u << 4)) &&
        bda_gui_tick_elapsed_25ms(g_escape_down_tick, now) >= ESCAPE_QUIT_TICKS) {
        g_close_requested = 1;
        g_escape_pending = 0;
    } else if (g_escape_pending && (released & (1u << 4))) {
        gam4980_key_down(GAM4980_KEY_EXIT);
        g_escape_pending = 0;
    }
    g_previous_keys = current;
}

static int gam_window_proc(bda_handle_t handle, u32 message, u32 wparam, u32 lparam)
{
    if (message == BDA_MSG_DRAW_CONTEXT_ATTACH) {
        g_frame = handle;
        (void)acquire_draw_context(handle);
        if (!g_draw_object)
            g_draw_object = bda_gui_draw_object_create(7);
        g_full_redraw = 1;
    } else if (message == BDA_MSG_DRAW_CONTEXT_DETACH) {
        if (!g_draw_owner || g_draw_owner == handle)
            release_draw_context();
        g_detached = 1;
    }
    if (message == BDA_MSG_TOUCH_RELEASE) {
        queue_touch(lparam);
        return 1;
    }
    if (message == BDA_MSG_TOUCH_COORDINATE)
        return 1;
    return bda_gui_default_proc(handle, message, wparam, lparam);
}

static int run_window(void)
{
    bda_frame_desc_t descriptor;
    bda_gui_message_t message;
    bda_gui_input_packet_t initial_packet;
    u32 last_frame_tick;
    u32 last_save_tick;
    u32 close_wait = 0;
    int window_ok = 0;

    bda_memset(&descriptor, 0, sizeof(descriptor));
    bda_memset(&message, 0, sizeof(message));
    bda_memset(g_touch_queue, 0, sizeof(g_touch_queue));
    bda_memset(g_hold_frames, 0, sizeof(g_hold_frames));
    g_frame = 0;
    g_draw = 0;
    g_draw_owner = 0;
    g_back = 0;
    g_draw_object = 0;
    g_touch_read = 0;
    g_touch_write = 0;
    g_detached = 0;
    g_previous_keys = 0;
    g_full_redraw = 1;
    g_escape_pending = 0;
    g_close_requested = 0;
    g_frame_count = 0;
    g_core_frame_phase = 0;
    g_core_break_logged = 0;

    descriptor.style = 0;
    descriptor.title = k_window_title;
    descriptor.wndproc = gam_window_proc;
    descriptor.height = SCREEN_WIDTH;
    descriptor.width = SCREEN_HEIGHT;
    g_frame = bda_gui_register_frame_desc(&descriptor);
    if (!g_frame || (s32)g_frame == -1)
        goto cleanup;
    (void)bda_gui_frame_activate(g_frame, 0x100);
    (void)acquire_draw_context(g_frame);
    g_draw_object = bda_gui_draw_object_create(7);
    if (!g_draw || !g_draw_object || (s32)(u32)g_draw_object == -1)
        goto cleanup;
    g_back = bda_gui_compatible_context_create(g_draw);
    if (!g_back || (s32)g_back == -1)
        goto cleanup;

    last_frame_tick = bda_gui_tick_count_25ms() - 1u;
    last_save_tick = last_frame_tick;
    (void)bda_gui_input_packet(&initial_packet);
    g_previous_keys = packet_mask(&initial_packet);
    log_line("WINDOW START");
    window_ok = 1;
    for (;;) {
        int pump_result = bda_gui_event_pump_frame_once(&message, g_frame);
        u32 now = bda_gui_tick_count_25ms();
        u32 elapsed_ticks = bda_gui_tick_elapsed_25ms(last_frame_tick, now);

        drain_touches();
        if (!g_close_requested && elapsed_ticks >= 1u) {
            int lcd_changed;

            last_frame_tick = now;
            if (elapsed_ticks > MAX_CATCHUP_TICKS)
                elapsed_ticks = MAX_CATCHUP_TICKS;
            poll_game_keys(now);
            g_core_frame_phase += elapsed_ticks * 60u;
            while (g_core_frame_phase >= 40u) {
                gam4980_step_frame();
                g_core_frame_phase -= 40u;
            }
            lcd_changed = gam4980_render_frame();
            g_frame_count += elapsed_ticks;
            if (g_full_redraw || lcd_changed) {
                if (!present_screen())
                    log_line("PRESENT FAILED");
            }
        }
        if (!g_close_requested &&
            bda_gui_tick_elapsed_25ms(last_save_tick, now) >= SAVE_INTERVAL_TICKS) {
            write_save();
            last_save_tick = now;
        }
        if (gam4980_shutdown_requested() && !g_core_break_logged) {
            log_hex_value("CORE BRK PC=", gam4980_shutdown_pc());
            log_hex_value("CORE FRAME=", g_frame_count);
            g_core_break_logged = 1;
        }
        if (g_close_requested && close_wait == 0) {
            write_save();
            (void)bda_gui_frame_stop(g_frame);
            (void)bda_gui_frame_release(g_frame);
        }
        bda_sys_delay(1);
        if (g_close_requested) {
            ++close_wait;
            if (!pump_result || g_detached || close_wait >= 128u)
                break;
        }
    }
    log_line("WINDOW END");

cleanup:
    if (g_back && (s32)g_back != -1) {
        bda_gui_compatible_context_free(g_back);
        g_back = 0;
    }
    release_draw_context();
    if (g_frame) {
        bda_gui_close_frame(g_frame);
        g_frame = 0;
    }
    return window_ok;
}

__attribute__((section(".text.bda_main")))
int bda_main(void)
{
    int result = 0;
    int selector_result;

    (void)bda_fs_mkdir(k_data_dir);
    (void)bda_fs_mkdir(k_game_dir);
    reset_log();
    log_line("START STANDALONE");
    g_save_path[0] = 0;
    g_loaded_game_size = 0;
    selector_result = bda_gui_select_file(
        &g_file_selector, k_game_selector_path, "gam", "Select GAM"
    );
    if (selector_result == BDA_FILE_SELECTOR_CANCELLED) {
        log_line("GAME SELECTION CANCELLED");
        goto cleanup;
    }
    if (selector_result != BDA_FILE_SELECTOR_SELECTED) {
        bda_msgbox(k_window_title, "Could not open the .gam file selector.");
        result = -1;
        goto cleanup;
    }
    log_line("GAME SELECTED");
    log_line(g_file_selector.path);
    if (!allocate_buffers()) {
        bda_msgbox(k_window_title, "Not enough memory (requires about 6.3 MiB).");
        result = -2;
        goto cleanup;
    }
    if (!load_fixed_file(k_rom_8_path, g_buffers.rom_8, GAM4980_ROM_SIZE)) {
        bda_msgbox(k_window_title, "Missing or invalid 8.BIN in application data.");
        result = -3;
        goto cleanup;
    }
    if (!load_fixed_file(k_rom_e_path, g_buffers.rom_e, GAM4980_ROM_SIZE)) {
        bda_msgbox(k_window_title, "Missing or invalid E.BIN in application data.");
        result = -4;
        goto cleanup;
    }
    if (gam4980_init(&g_buffers) <= 0) {
        bda_msgbox(k_window_title, "Firmware initialization failed.");
        result = -5;
        goto cleanup;
    }
    if (load_game(g_file_selector.path) <= 0) {
        bda_msgbox(k_window_title, "The selected .gam file is invalid or unreadable.");
        result = -6;
        goto cleanup;
    }
    log_hex_value("GAME SIZE=", g_loaded_game_size);
    if (!run_window()) {
        bda_msgbox(k_window_title, "Could not create the emulator window.");
        result = -7;
    }

cleanup:
    log_line("END");
    release_buffers();
    return result;
}

/* Single translation unit: the upstream-derived core includes s6502.c. */
#include "gam4980_core.c"
