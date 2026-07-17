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
#define SCREEN_FRAME_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT * 2u)
#define LCD_SOURCE_WIDTH (GAM4980_LCD_WIDTH + 1)
#define CORE_FRAME_BYTES (LCD_SOURCE_WIDTH * GAM4980_LCD_HEIGHT * 2u)
#define FRAME_QUEUE_CAPACITY 32u
#define FRAME_QUEUE_BYTES \
    (GAM4980_LCD_PACKED_SIZE * FRAME_QUEUE_CAPACITY)
#define FRAME_PRESENT_HOLD_TICKS 3u

#define VIEW_X 0
#define VIEW_Y 9
#define VIEW_WIDTH SCREEN_WIDTH
#define VIEW_HEIGHT 145
#define VIEW_FRAME_BYTES (VIEW_WIDTH * VIEW_HEIGHT * 2u)
#define SETTINGS_ROW_Y 159
#define SETTINGS_ROW_HEIGHT 22

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

typedef struct ui_rect {
    s32 x;
    s32 y;
    s32 width;
    s32 height;
} ui_rect_t;

typedef struct scale_axis_sample {
    u16 index;
    u16 weight;
} scale_axis_sample_t;

enum scaling_algorithm {
    SCALE_NEAREST = 0,
    SCALE_BILINEAR = 1,
    SCALE_NATIVE = 2,
    SCALE_ALGORITHM_COUNT = 3,
};

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
static const char k_config_path[] =
    "A:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\gam4980\\GAM4980.CFG";

static const touch_button_t k_buttons[] = {
    { 52, 185, 38, 38, GAM4980_KEY_UP, 0 },
    { 12, 225, 38, 38, GAM4980_KEY_LEFT, 0 },
    { 92, 225, 38, 38, GAM4980_KEY_RIGHT, 0 },
    { 52, 265, 38, 38, GAM4980_KEY_DOWN, 0 },
    { 148, 185, 84, 43, GAM4980_KEY_ENTER, "ENTER" },
    { 148, 232, 84, 34, GAM4980_KEY_EXIT, "EXIT" },
    { 148, 270, 40, 34, GAM4980_KEY_PAGE_UP, "PGUP" },
    { 192, 270, 40, 34, GAM4980_KEY_PAGE_DOWN, "PGDN" },
};

static const ui_rect_t k_settings_button = { 204, SETTINGS_ROW_Y, 28, 22 };
static const ui_rect_t k_settings_close = { 196, 72, 22, 22 };
static const ui_rect_t k_settings_options[SCALE_ALGORITHM_COUNT] = {
    { 28, 108, 184, 34 },
    { 28, 151, 184, 34 },
    { 28, 194, 184, 34 },
};
static const char *const k_algorithm_names[SCALE_ALGORITHM_COUNT] = {
    "NEAREST", "BILINEAR", "NATIVE",
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
static bda_gui_picture_t g_full_picture;
static u16 *g_scaled_framebuffer;
static u16 *g_screen_pixels;
static u8 *g_frame_queue;
static bda_handle_t g_frame;
static bda_handle_t g_draw;
static bda_handle_t g_draw_owner;
static void *g_draw_object;
static u32 g_touch_queue[TOUCH_QUEUE_SIZE];
static volatile u32 g_touch_read;
static volatile u32 g_touch_write;
static volatile int g_detached;
static u8 g_nearest_x[VIEW_WIDTH];
static u8 g_nearest_y[VIEW_HEIGHT];
static scale_axis_sample_t g_bilinear_x[VIEW_WIDTH];
static scale_axis_sample_t g_bilinear_y[VIEW_HEIGHT];
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
static int g_scale_algorithm = SCALE_BILINEAR;
static int g_settings_open;
static int g_settings_selection;
static int g_settings_key_release_ticks;
static u32 g_frame_queue_read;
static u32 g_frame_queue_write;
static u32 g_frame_queue_count;
static u32 g_frame_queue_drops;
static u32 g_frame_present_hold_ticks;

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

static void init_bilinear_axis(
    scale_axis_sample_t *samples, u32 destination_size, u32 source_size
)
{
    u32 index;

    for (index = 0; index < destination_size; ++index) {
        u32 fixed = index * (source_size - 1u) * 256u /
                    (destination_size - 1u);
        samples[index].index = (u16)(fixed >> 8);
        samples[index].weight = (u16)(fixed & 0xffu);
    }
}

static void release_buffers(void)
{
    gam4980_deinit();
    if (valid_pointer(g_screen_pixels))
        bda_free(g_screen_pixels);
    if (valid_pointer(g_scaled_framebuffer))
        bda_free(g_scaled_framebuffer);
    if (valid_pointer(g_frame_queue))
        bda_free(g_frame_queue);
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
    g_screen_pixels = 0;
    g_frame_queue = 0;
}

static int allocate_buffers(void)
{
    bda_memset(&g_buffers, 0, sizeof(g_buffers));
    g_scaled_framebuffer = 0;
    g_screen_pixels = 0;
    g_frame_queue = 0;

    g_buffers.ram = (u8 *)bda_alloc(GAM4980_RAM_SIZE);
    g_buffers.flash = (u8 *)bda_alloc(GAM4980_FLASH_SIZE);
    g_buffers.rom_8 = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.rom_e = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.framebuffer = (u16 *)bda_alloc(CORE_FRAME_BYTES);
    g_scaled_framebuffer = (u16 *)bda_alloc(VIEW_FRAME_BYTES);
    g_screen_pixels = (u16 *)bda_alloc(SCREEN_FRAME_BYTES);
    g_frame_queue = (u8 *)bda_alloc(FRAME_QUEUE_BYTES);
    if (!valid_pointer(g_buffers.ram) || !valid_pointer(g_buffers.flash) ||
        !valid_pointer(g_buffers.rom_8) || !valid_pointer(g_buffers.rom_e) ||
        !valid_pointer(g_buffers.framebuffer) ||
        !valid_pointer(g_scaled_framebuffer) ||
        !valid_pointer(g_screen_pixels) ||
        !valid_pointer(g_frame_queue)) {
        release_buffers();
        return 0;
    }
    {
        u32 index;
        /* Map the active 159x96 LCD; the 160th source pixel is stride padding. */
        for (index = 0; index < VIEW_WIDTH; ++index)
            g_nearest_x[index] =
                (u8)(index * GAM4980_LCD_WIDTH / VIEW_WIDTH);
        for (index = 0; index < VIEW_HEIGHT; ++index)
            g_nearest_y[index] =
                (u8)(index * GAM4980_LCD_HEIGHT / VIEW_HEIGHT);
    }
    init_bilinear_axis(
        g_bilinear_x, VIEW_WIDTH, GAM4980_LCD_WIDTH
    );
    init_bilinear_axis(
        g_bilinear_y, VIEW_HEIGHT, GAM4980_LCD_HEIGHT
    );
    bda_memset(&g_lcd_picture, 0, sizeof(g_lcd_picture));
    g_lcd_picture.width = VIEW_WIDTH;
    g_lcd_picture.height = VIEW_HEIGHT;
    g_lcd_picture.source_pixels = g_scaled_framebuffer;
    g_lcd_picture.selected_index = -1;
    bda_memset(&g_full_picture, 0, sizeof(g_full_picture));
    g_full_picture.width = SCREEN_WIDTH;
    g_full_picture.height = SCREEN_HEIGHT;
    g_full_picture.source_pixels = g_screen_pixels;
    g_full_picture.selected_index = -1;
    return 1;
}

static void clear_frame_queue(void)
{
    g_frame_queue_read = 0;
    g_frame_queue_write = 0;
    g_frame_queue_count = 0;
    g_frame_present_hold_ticks = 0;
}

static void enqueue_current_frame(void)
{
    u8 *destination;

    if (g_frame_queue_count == FRAME_QUEUE_CAPACITY) {
        g_frame_queue_read =
            (g_frame_queue_read + 1u) % FRAME_QUEUE_CAPACITY;
        --g_frame_queue_count;
        ++g_frame_queue_drops;
    }
    destination = g_frame_queue +
        g_frame_queue_write * GAM4980_LCD_PACKED_SIZE;
    bda_memcpy(
        destination, gam4980_packed_frame(), GAM4980_LCD_PACKED_SIZE
    );
    g_frame_queue_write =
        (g_frame_queue_write + 1u) % FRAME_QUEUE_CAPACITY;
    ++g_frame_queue_count;
}

static const u8 *peek_frame_queue(void)
{
    if (!g_frame_queue_count)
        return 0;
    return g_frame_queue +
        g_frame_queue_read * GAM4980_LCD_PACKED_SIZE;
}

static void pop_frame_queue(void)
{
    if (!g_frame_queue_count)
        return;
    g_frame_queue_read =
        (g_frame_queue_read + 1u) % FRAME_QUEUE_CAPACITY;
    --g_frame_queue_count;
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

static void load_scaling_config(void)
{
    u8 data[8];
    int file;

    g_scale_algorithm = SCALE_BILINEAR;
    file = bda_fs_fopen_raw(k_config_path, "rb");
    if (!bda_fs_file_is_valid(file))
        return;
    if (read_exact(file, data, sizeof(data)) &&
        data[0] == 'G' && data[1] == '4' &&
        data[2] == 'S' && data[3] == '1' &&
        data[4] < SCALE_ALGORITHM_COUNT) {
        g_scale_algorithm = data[4];
    }
    (void)bda_fs_close_raw(file);
}

static void write_scaling_config(void)
{
    u8 data[8] = { 'G', '4', 'S', '1', 0, 0, 0, 0 };
    int file;

    data[4] = (u8)g_scale_algorithm;
    file = bda_fs_fopen_raw(k_config_path, "wb");
    if (!bda_fs_file_is_valid(file)) {
        log_line("CONFIG OPEN FAILED");
        return;
    }
    if (!write_exact(file, data, sizeof(data)))
        log_line("CONFIG WRITE FAILED");
    (void)bda_fs_close_raw(file);
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

static int make_save_path(const char *game_path)
{
    u32 length;
    char *extension = 0;
    u32 index;

    if (!game_path)
        return 0;
    length = text_length(game_path);
    if (length < 4u || length >= sizeof(g_save_path))
        return 0;
    copy_text(g_save_path, game_path, sizeof(g_save_path));
    for (index = 0; index < length; ++index) {
        if (g_save_path[index] == '\\' || g_save_path[index] == '/')
            extension = 0;
        else if (g_save_path[index] == '.')
            extension = g_save_path + index;
    }
    if (!extension || extension + 4 != g_save_path + length)
        return 0;
    extension[1] = 's';
    extension[2] = 'a';
    extension[3] = 'v';
    return 1;
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
    if (!make_save_path(path))
        return -5;
    load_save();
    gam4980_save_mark_clean();
    return 1;
}

static u16 rgb565(u32 red, u32 green, u32 blue)
{
    return (u16)(((red & 0xf8u) << 8) | ((green & 0xfcu) << 3) | (blue >> 3));
}

static void fill_rect(int x, int y, int width, int height, u16 color)
{
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width > SCREEN_WIDTH ? SCREEN_WIDTH : x + width;
    int bottom = y + height > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + height;
    int py;

    if (left >= right || top >= bottom)
        return;
    for (py = top; py < bottom; ++py) {
        u16 *pixel = g_screen_pixels + py * SCREEN_WIDTH + left;
        u16 *end = pixel + (right - left);
        while (pixel < end)
            *pixel++ = color;
    }
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

static void draw_gear_icon(int center_x, int center_y, u16 color, u16 hole)
{
    fill_rect(center_x - 4, center_y - 4, 9, 9, color);
    fill_rect(center_x - 2, center_y - 6, 5, 2, color);
    fill_rect(center_x - 2, center_y + 5, 5, 2, color);
    fill_rect(center_x - 6, center_y - 2, 2, 5, color);
    fill_rect(center_x + 5, center_y - 2, 2, 5, color);
    fill_rect(center_x - 1, center_y - 1, 3, 3, hole);
}

static void draw_close_icon(const ui_rect_t *rect, u16 color)
{
    int index;
    int center_x = rect->x + rect->width / 2;
    int center_y = rect->y + rect->height / 2;

    for (index = -4; index <= 4; ++index) {
        fill_rect(center_x + index, center_y + index, 2, 2, color);
        fill_rect(center_x + index, center_y - index, 2, 2, color);
    }
}

static void draw_radio(int center_x, int center_y, int selected)
{
    u16 outline = rgb565(146, 164, 170);
    u16 accent = rgb565(41, 178, 178);
    u16 inside = rgb565(22, 34, 42);

    fill_rect(center_x - 4, center_y - 6, 9, 2, outline);
    fill_rect(center_x - 6, center_y - 4, 2, 9, outline);
    fill_rect(center_x + 5, center_y - 4, 2, 9, outline);
    fill_rect(center_x - 4, center_y + 5, 9, 2, outline);
    fill_rect(center_x - 4, center_y - 4, 9, 9, inside);
    if (selected)
        fill_rect(center_x - 2, center_y - 2, 5, 5, accent);
}

static void draw_settings_row(void)
{
    u16 border = rgb565(55, 76, 84);
    u16 fill = rgb565(17, 31, 39);
    u16 muted = rgb565(146, 164, 170);
    u16 text = rgb565(238, 244, 239);
    u16 accent = rgb565(41, 178, 178);
    int text_y = SETTINGS_ROW_Y + (SETTINGS_ROW_HEIGHT - 7) / 2;

    fill_rect(8, SETTINGS_ROW_Y, 224, SETTINGS_ROW_HEIGHT, border);
    fill_rect(10, SETTINGS_ROW_Y + 2, 192, SETTINGS_ROW_HEIGHT - 4, fill);
    draw_text(14, text_y, "SCALE", 1, muted);
    draw_text(51, text_y, k_algorithm_names[g_scale_algorithm], 1, text);

    fill_rect(
        k_settings_button.x, k_settings_button.y,
        k_settings_button.width, k_settings_button.height, accent
    );
    fill_rect(
        k_settings_button.x + 2, k_settings_button.y + 2,
        k_settings_button.width - 4, k_settings_button.height - 4, fill
    );
    draw_gear_icon(
        k_settings_button.x + k_settings_button.width / 2,
        k_settings_button.y + k_settings_button.height / 2,
        text, fill
    );
}

static void draw_settings_overlay(void)
{
    const ui_rect_t panel = { 14, 64, 212, 180 };
    u16 panel_border = rgb565(238, 177, 45);
    u16 panel_fill = rgb565(14, 25, 32);
    u16 row_fill = rgb565(22, 34, 42);
    u16 row_border = rgb565(55, 76, 84);
    u16 focus = rgb565(41, 178, 178);
    u16 text = rgb565(238, 244, 239);
    int title_width = label_width("SCALING", 2);
    int index;

    fill_rect(panel.x, panel.y, panel.width, panel.height, panel_border);
    fill_rect(panel.x + 2, panel.y + 2, panel.width - 4, panel.height - 4,
              panel_fill);
    draw_text((SCREEN_WIDTH - title_width) / 2, 75, "SCALING", 2, text);

    fill_rect(
        k_settings_close.x, k_settings_close.y,
        k_settings_close.width, k_settings_close.height, row_border
    );
    fill_rect(
        k_settings_close.x + 2, k_settings_close.y + 2,
        k_settings_close.width - 4, k_settings_close.height - 4, row_fill
    );
    draw_close_icon(&k_settings_close, text);

    for (index = 0; index < SCALE_ALGORITHM_COUNT; ++index) {
        const ui_rect_t *row = &k_settings_options[index];
        u16 border = index == g_settings_selection ? focus : row_border;

        fill_rect(row->x, row->y, row->width, row->height, border);
        fill_rect(row->x + 2, row->y + 2, row->width - 4, row->height - 4,
                  row_fill);
        draw_radio(row->x + 18, row->y + row->height / 2,
                   index == g_scale_algorithm);
        draw_text(row->x + 38, row->y + (row->height - 14) / 2,
                  k_algorithm_names[index], 2, text);
    }
}

static u16 lerp_rgb565(u16 first, u16 second, u32 weight)
{
    u32 inverse;
    u32 red;
    u32 green;
    u32 blue;

    if (!weight || first == second)
        return first;
    inverse = 256u - weight;
    red = ((first >> 11) * inverse + (second >> 11) * weight + 128u) >> 8;
    green = ((((first >> 5) & 63u) * inverse +
              ((second >> 5) & 63u) * weight + 128u) >> 8);
    blue = (((first & 31u) * inverse + (second & 31u) * weight + 128u) >> 8);
    return (u16)((red << 11) | (green << 5) | blue);
}

static void bilinear_scale_to_view(
    const u16 *source, int source_width, int source_height, int source_stride,
    const scale_axis_sample_t *x_samples,
    const scale_axis_sample_t *y_samples
)
{
    int y;

    for (y = 0; y < VIEW_HEIGHT; ++y) {
        u32 source_y = y_samples[y].index;
        u32 next_y = source_y + 1u < (u32)source_height ?
                     source_y + 1u : source_y;
        u32 y_weight = y_samples[y].weight;
        const u16 *top = source + source_y * source_stride;
        const u16 *bottom = source + next_y * source_stride;
        u16 *destination = g_scaled_framebuffer + y * VIEW_WIDTH;
        int x;

        for (x = 0; x < VIEW_WIDTH; ++x) {
            u32 source_x = x_samples[x].index;
            u32 next_x = source_x + 1u < (u32)source_width ?
                         source_x + 1u : source_x;
            u32 x_weight = x_samples[x].weight;
            u16 top_color = lerp_rgb565(
                top[source_x], top[next_x], x_weight
            );
            u16 bottom_color = lerp_rgb565(
                bottom[source_x], bottom[next_x], x_weight
            );

            destination[x] = lerp_rgb565(top_color, bottom_color, y_weight);
        }
    }
}

static void native_scale_to_view(const u16 *source)
{
    const int offset_x = (VIEW_WIDTH - GAM4980_LCD_WIDTH) / 2;
    const int offset_y = (VIEW_HEIGHT - GAM4980_LCD_HEIGHT) / 2;
    int y;

    bda_memset(g_scaled_framebuffer, 0, VIEW_FRAME_BYTES);
    for (y = 0; y < GAM4980_LCD_HEIGHT; ++y) {
        u16 *destination = g_scaled_framebuffer +
            (offset_y + y) * VIEW_WIDTH + offset_x;
        bda_memcpy(
            destination, source + y * LCD_SOURCE_WIDTH,
            GAM4980_LCD_WIDTH * 2u
        );
    }
}

static void scale_lcd_to_view(const u16 *source)
{
    int y;

    if (g_scale_algorithm == SCALE_BILINEAR) {
        bilinear_scale_to_view(
            source, GAM4980_LCD_WIDTH, GAM4980_LCD_HEIGHT, LCD_SOURCE_WIDTH,
            g_bilinear_x, g_bilinear_y
        );
        return;
    }
    if (g_scale_algorithm == SCALE_NATIVE) {
        native_scale_to_view(source);
        return;
    }

    for (y = 0; y < VIEW_HEIGHT; ++y) {
        const u16 *source_row = source + g_nearest_y[y] * LCD_SOURCE_WIDTH;
        u16 *destination = g_scaled_framebuffer + y * VIEW_WIDTH;
        int x;

        for (x = 0; x < VIEW_WIDTH; ++x)
            destination[x] = source_row[g_nearest_x[x]];
    }
}

static void copy_lcd_to_full_screen(void)
{
    int y;

    for (y = 0; y < VIEW_HEIGHT; ++y) {
        u16 *destination = g_screen_pixels +
            (VIEW_Y + y) * SCREEN_WIDTH + VIEW_X;
        bda_memcpy(destination, g_scaled_framebuffer + y * VIEW_WIDTH,
                   VIEW_WIDTH * 2u);
    }
}

static void render_game_screen(const u16 *source)
{
    u16 background = rgb565(10, 20, 27);
    u16 frame = rgb565(238, 177, 45);
    u32 index;

    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, background);
    fill_rect(0, VIEW_Y - 3, SCREEN_WIDTH, VIEW_HEIGHT + 6,
              rgb565(36, 55, 63));
    fill_rect(0, VIEW_Y - 2, SCREEN_WIDTH, VIEW_HEIGHT + 4, frame);
    scale_lcd_to_view(source);
    copy_lcd_to_full_screen();
    draw_settings_row();
    for (index = 0; index < sizeof(k_buttons) / sizeof(k_buttons[0]); ++index)
        draw_button(&k_buttons[index]);
    if (g_settings_open)
        draw_settings_overlay();
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

static int present_screen(const u8 *packed_frame)
{
    const u16 *source = gam4980_expand_frame(packed_frame);
    int draw_result;
    int x = VIEW_X;
    int y = VIEW_Y;
    int width = VIEW_WIDTH;
    int height = VIEW_HEIGHT;
    bda_gui_picture_t *picture = &g_lcd_picture;
    void *old_object;

    if (!source || !g_draw || !g_draw_object)
        return 0;
    if (g_full_redraw) {
        render_game_screen(source);
        g_full_picture.source_pixels = g_screen_pixels;
        g_full_picture.selected_index = -1;
        picture = &g_full_picture;
        x = 0;
        y = 0;
        width = SCREEN_WIDTH;
        height = SCREEN_HEIGHT;
    } else {
        scale_lcd_to_view(source);
        g_lcd_picture.source_pixels = g_scaled_framebuffer;
        g_lcd_picture.selected_index = -1;
    }

    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    draw_result = bda_gui_render_picture(
        g_draw, x, y, width, height, picture
    );
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    if (draw_result == 0)
        g_full_redraw = 0;
    return draw_result == 0;
}

static int point_in_button(int x, int y, const touch_button_t *button)
{
    return x >= button->x && x < button->x + button->width &&
           y >= button->y && y < button->y + button->height;
}

static int point_in_rect(int x, int y, const ui_rect_t *rect)
{
    return x >= rect->x && x < rect->x + rect->width &&
           y >= rect->y && y < rect->y + rect->height;
}

static u32 packet_mask(const bda_gui_input_packet_t *packet);

static void sync_previous_keys(void)
{
    bda_gui_input_packet_t packet;

    (void)bda_gui_input_packet(&packet);
    g_previous_keys = packet_mask(&packet);
}

static void open_settings(void)
{
    sync_previous_keys();
    clear_frame_queue();
    g_settings_selection = g_scale_algorithm;
    g_settings_key_release_ticks = 0;
    g_settings_open = 1;
    g_escape_pending = 0;
    g_full_redraw = 1;
}

static void close_settings(void)
{
    sync_previous_keys();
    g_settings_key_release_ticks = 0;
    g_settings_open = 0;
    g_full_redraw = 1;
}

static void apply_scaling_selection(void)
{
    if (g_settings_selection >= 0 &&
        g_settings_selection < SCALE_ALGORITHM_COUNT &&
        g_scale_algorithm != g_settings_selection) {
        g_scale_algorithm = g_settings_selection;
        write_scaling_config();
        log_line("SCALING CHANGED");
        log_line(k_algorithm_names[g_scale_algorithm]);
    }
    close_settings();
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
        if (g_settings_open) {
            if (point_in_rect(x, y, &k_settings_close)) {
                close_settings();
                continue;
            }
            for (index = 0; index < SCALE_ALGORITHM_COUNT; ++index) {
                if (point_in_rect(x, y, &k_settings_options[index])) {
                    g_settings_selection = (int)index;
                    apply_scaling_selection();
                    break;
                }
            }
            continue;
        }
        if (point_in_rect(x, y, &k_settings_button)) {
            open_settings();
            continue;
        }
        for (index = 0; index < sizeof(k_buttons) / sizeof(k_buttons[0]); ++index) {
            if (point_in_button(x, y, &k_buttons[index])) {
                clear_frame_queue();
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
            clear_frame_queue();
            gam4980_key_down(packet_key(index));
        } else if (current & bit) {
            ++g_hold_frames[index];
            if (g_hold_frames[index] >= 12u &&
                ((g_hold_frames[index] - 12u) % 3u) == 0u) {
                clear_frame_queue();
                gam4980_key_down(packet_key(index));
            }
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
        clear_frame_queue();
        gam4980_key_down(GAM4980_KEY_EXIT);
        g_escape_pending = 0;
    }
    g_previous_keys = current;
}

static void poll_settings_keys(void)
{
    bda_gui_input_packet_t packet;
    u32 current;
    u32 pressed;

    (void)bda_gui_input_packet(&packet);
    current = packet_mask(&packet);
    if (g_settings_key_release_ticks < 4) {
        if (current == 0)
            ++g_settings_key_release_ticks;
        else
            g_settings_key_release_ticks = 0;
        g_previous_keys = current;
        return;
    }
    pressed = current & ~g_previous_keys;

    if (pressed & (1u << 5)) {
        apply_scaling_selection();
    } else if (pressed & ((1u << 1) | (1u << 3))) {
        --g_settings_selection;
        if (g_settings_selection < 0)
            g_settings_selection = SCALE_ALGORITHM_COUNT - 1;
        g_full_redraw = 1;
    } else if (pressed & ((1u << 0) | (1u << 2))) {
        ++g_settings_selection;
        if (g_settings_selection >= SCALE_ALGORITHM_COUNT)
            g_settings_selection = 0;
        g_full_redraw = 1;
    }
    g_previous_keys = current;
}

static int gam_window_proc(bda_handle_t handle, u32 message, u32 wparam, u32 lparam)
{
    if (message == BDA_MSG_DRAW_CONTEXT_ATTACH) {
        bda_handle_t previous_draw = g_draw;

        g_frame = handle;
        (void)acquire_draw_context(handle);
        if (!g_draw_object)
            g_draw_object = bda_gui_draw_object_create(7);
        if (!previous_draw || g_draw != previous_draw)
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
    g_settings_open = 0;
    g_settings_selection = g_scale_algorithm;
    g_settings_key_release_ticks = 0;
    clear_frame_queue();
    g_frame_queue_drops = 0;

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
            last_frame_tick = now;
            if (elapsed_ticks > MAX_CATCHUP_TICKS)
                elapsed_ticks = MAX_CATCHUP_TICKS;
            if (g_settings_open) {
                poll_settings_keys();
                g_core_frame_phase = 0;
                if (g_full_redraw &&
                    !present_screen(gam4980_packed_frame()))
                    log_line("PRESENT FAILED");
            } else {
                const u8 *queued_frame;

                poll_game_keys(now);
                if (elapsed_ticks >= g_frame_present_hold_ticks)
                    g_frame_present_hold_ticks = 0;
                else
                    g_frame_present_hold_ticks -= elapsed_ticks;
                g_core_frame_phase += elapsed_ticks * 60u;
                while (g_core_frame_phase >= 40u) {
                    gam4980_step_frame();
                    g_core_frame_phase -= 40u;
                    if (gam4980_render_frame())
                        enqueue_current_frame();
                }
                g_frame_count += elapsed_ticks;
                queued_frame = peek_frame_queue();
                if (queued_frame && !g_frame_present_hold_ticks) {
                    if (present_screen(queued_frame)) {
                        pop_frame_queue();
                        g_frame_present_hold_ticks =
                            FRAME_PRESENT_HOLD_TICKS;
                    } else {
                        log_line("PRESENT FAILED");
                    }
                } else if (!queued_frame && g_full_redraw &&
                           !present_screen(gam4980_packed_frame())) {
                    log_line("PRESENT FAILED");
                }
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
            log_hex_value("FRAME DROPS=", g_frame_queue_drops);
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
    load_scaling_config();
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
