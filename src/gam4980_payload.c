#include "bda_audio.h"
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

#define PCM_SAMPLE_COUNT 512u
#define PCM_BYTE_COUNT (PCM_SAMPLE_COUNT * (u32)sizeof(s16))
#define PCM_PREFILL_BLOCK_COUNT 4u
#define PCM_PUMP_BLOCK_LIMIT 4u

#define VIEW_X 0
#define VIEW_Y 9
#define VIEW_WIDTH SCREEN_WIDTH
#define VIEW_HEIGHT 145
#define VIEW_FRAME_BYTES (VIEW_WIDTH * VIEW_HEIGHT * 2u)
#define SETTINGS_ROW_Y 159
#define SETTINGS_ROW_HEIGHT 22

#define TOUCH_QUEUE_SIZE 16u
#define TOUCH_REPEAT_DELAY_TICKS 12u
#define TOUCH_REPEAT_INTERVAL_TICKS 3u
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
    u8 repeat;
} touch_button_t;

typedef struct touch_event {
    u32 message;
    u32 packed;
} touch_event_t;

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

enum input_panel {
    INPUT_PANEL_GAMEPAD = 0,
    INPUT_PANEL_KEYBOARD = 1,
};

enum keyboard_page {
    KEYBOARD_PAGE_ALPHA = 0,
    KEYBOARD_PAGE_FUNCTION = 1,
};

enum settings_tab {
    SETTINGS_TAB_DISPLAY = 0,
    SETTINGS_TAB_AUDIO,
    SETTINGS_TAB_GAME,
    SETTINGS_TAB_COUNT,
};

enum display_settings_item {
    DISPLAY_SETTINGS_SCALE_NEAREST = 0,
    DISPLAY_SETTINGS_SCALE_BILINEAR,
    DISPLAY_SETTINGS_SCALE_NATIVE,
    DISPLAY_SETTINGS_LCD_THEME,
    DISPLAY_SETTINGS_LCD_GHOSTING,
    DISPLAY_SETTINGS_ITEM_COUNT,
};

enum audio_settings_item {
    AUDIO_SETTINGS_ENABLED = 0,
    AUDIO_SETTINGS_VOLUME,
    AUDIO_SETTINGS_ITEM_COUNT,
};

enum game_settings_item {
    GAME_SETTINGS_RESTART = 0,
    GAME_SETTINGS_ITEM_COUNT,
};

enum session_action {
    SESSION_ACTION_EXIT = 0,
    SESSION_ACTION_RESTART,
    SESSION_ACTION_CHANGE_GAME,
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

static const touch_button_t k_gamepad_buttons[] = {
    { 52, 185, 38, 38, GAM4980_KEY_UP, 0, 1 },
    { 12, 225, 38, 38, GAM4980_KEY_LEFT, 0, 1 },
    { 92, 225, 38, 38, GAM4980_KEY_RIGHT, 0, 1 },
    { 52, 265, 38, 38, GAM4980_KEY_DOWN, 0, 1 },
    { 148, 185, 84, 43, GAM4980_KEY_ENTER, "ENTER", 0 },
    { 148, 232, 84, 34, GAM4980_KEY_EXIT, "EXIT", 0 },
    { 148, 270, 40, 34, GAM4980_KEY_PAGE_UP, "PGUP", 1 },
    { 192, 270, 40, 34, GAM4980_KEY_PAGE_DOWN, "PGDN", 1 },
};

static const touch_button_t k_alpha_keyboard_buttons[] = {
    {   4, 184, 31, 22, GAM4980_KEY_HELP,   "HELP", 0 },
    {  37, 184, 31, 22, GAM4980_KEY_SEARCH, "FIND", 0 },
    {  70, 184, 27, 22, GAM4980_KEY_INSERT, "INS",  0 },
    {  99, 184, 31, 22, GAM4980_KEY_MODIFY, "EDIT", 0 },
    { 132, 184, 27, 22, GAM4980_KEY_DELETE, "DEL",  1 },
    { 161, 184, 31, 22, GAM4980_KEY_EXIT,   "EXIT", 0 },
    { 194, 184, 42, 22, GAM4980_KEY_ENTER,  "ENTER", 0 },

    {   5, 208, 21, 22, GAM4980_KEY_1, "1", 1 },
    {  28, 208, 21, 22, GAM4980_KEY_2, "2", 1 },
    {  51, 208, 21, 22, GAM4980_KEY_3, "3", 1 },
    {  74, 208, 21, 22, GAM4980_KEY_4, "4", 1 },
    {  97, 208, 21, 22, GAM4980_KEY_5, "5", 1 },
    { 120, 208, 21, 22, GAM4980_KEY_6, "6", 1 },
    { 143, 208, 21, 22, GAM4980_KEY_7, "7", 1 },
    { 166, 208, 21, 22, GAM4980_KEY_8, "8", 1 },
    { 189, 208, 21, 22, GAM4980_KEY_9, "9", 1 },
    { 212, 208, 21, 22, GAM4980_KEY_0, "0", 1 },

    {   5, 232, 21, 22, GAM4980_KEY_Q, "Q", 1 },
    {  28, 232, 21, 22, GAM4980_KEY_W, "W", 1 },
    {  51, 232, 21, 22, GAM4980_KEY_E, "E", 1 },
    {  74, 232, 21, 22, GAM4980_KEY_R, "R", 1 },
    {  97, 232, 21, 22, GAM4980_KEY_T, "T", 1 },
    { 120, 232, 21, 22, GAM4980_KEY_Y, "Y", 1 },
    { 143, 232, 21, 22, GAM4980_KEY_U, "U", 1 },
    { 166, 232, 21, 22, GAM4980_KEY_I, "I", 1 },
    { 189, 232, 21, 22, GAM4980_KEY_O, "O", 1 },
    { 212, 232, 21, 22, GAM4980_KEY_P, "P", 1 },

    {   5, 256, 21, 22, GAM4980_KEY_INPUT, "IME", 0 },
    {  28, 256, 21, 22, GAM4980_KEY_A, "A", 1 },
    {  51, 256, 21, 22, GAM4980_KEY_S, "S", 1 },
    {  74, 256, 21, 22, GAM4980_KEY_D, "D", 1 },
    {  97, 256, 21, 22, GAM4980_KEY_F, "F", 1 },
    { 120, 256, 21, 22, GAM4980_KEY_G, "G", 1 },
    { 143, 256, 21, 22, GAM4980_KEY_H, "H", 1 },
    { 166, 256, 21, 22, GAM4980_KEY_J, "J", 1 },
    { 189, 256, 21, 22, GAM4980_KEY_K, "K", 1 },
    { 212, 256, 21, 22, GAM4980_KEY_L, "L", 1 },

    {   3, 280, 37, 22, GAM4980_KEY_SHIFT, "SHIFT", 0 },
    {  42, 280, 19, 22, GAM4980_KEY_Z, "Z", 1 },
    {  63, 280, 19, 22, GAM4980_KEY_X, "X", 1 },
    {  84, 280, 19, 22, GAM4980_KEY_C, "C", 1 },
    { 105, 280, 19, 22, GAM4980_KEY_V, "V", 1 },
    { 126, 280, 19, 22, GAM4980_KEY_B, "B", 1 },
    { 147, 280, 19, 22, GAM4980_KEY_N, "N", 1 },
    { 168, 280, 19, 22, GAM4980_KEY_M, "M", 1 },
    { 189, 280, 48, 22, GAM4980_KEY_SPACE, "SPACE", 1 },
};

static const touch_button_t k_function_keyboard_buttons[] = {
    {   4, 184, 55, 27, GAM4980_KEY_POWER,    "POWER", 0 },
    {  63, 184, 55, 27, GAM4980_KEY_MENU,     "MENU",  0 },
    { 122, 184, 55, 27, GAM4980_KEY_EC_SJ,    "SJ",    0 },
    { 181, 184, 55, 27, GAM4980_KEY_EC_SW,    "SW",    0 },
    {   4, 215, 55, 27, GAM4980_KEY_CE,       "CE",    0 },
    {  63, 215, 55, 27, GAM4980_KEY_DIALOG,   "DLG",   0 },
    { 122, 215, 55, 27, GAM4980_KEY_DOWNLOAD, "LOAD",  0 },
    { 181, 215, 55, 27, GAM4980_KEY_SPEAK,    "SPK",   0 },
    {   4, 246, 55, 27, GAM4980_KEY_HELP,     "HELP",  0 },
    {  63, 246, 55, 27, GAM4980_KEY_SEARCH,   "FIND",  0 },
    { 122, 246, 55, 27, GAM4980_KEY_INSERT,   "INS",   0 },
    { 181, 246, 55, 27, GAM4980_KEY_MODIFY,   "EDIT",  0 },
    {   4, 277, 44, 27, GAM4980_KEY_DELETE,   "DEL",   1 },
    {  51, 277, 44, 27, GAM4980_KEY_EXIT,     "EXIT",  0 },
    {  98, 277, 44, 27, GAM4980_KEY_ENTER,    "ENTER", 0 },
    { 145, 277, 44, 27, GAM4980_KEY_PAGE_UP,  "PGUP",  1 },
    { 192, 277, 44, 27, GAM4980_KEY_PAGE_DOWN,"PGDN",  1 },
};

static const ui_rect_t k_keyboard_page_button = { 104, SETTINGS_ROW_Y, 32, 22 };
static const ui_rect_t k_change_game_button = { 140, SETTINGS_ROW_Y, 28, 22 };
static const ui_rect_t k_input_panel_button = { 172, SETTINGS_ROW_Y, 28, 22 };
static const ui_rect_t k_settings_button = { 204, SETTINGS_ROW_Y, 28, 22 };
static const ui_rect_t k_settings_close = { 196, 26, 22, 22 };
static const ui_rect_t k_settings_tabs[SETTINGS_TAB_COUNT] = {
    { 24, 55, 68, 26 },
    { 94, 55, 58, 26 },
    { 154, 55, 62, 26 },
};
static const ui_rect_t k_settings_options[DISPLAY_SETTINGS_ITEM_COUNT] = {
    { 28, 88, 184, 32 },
    { 28, 124, 184, 32 },
    { 28, 160, 184, 32 },
    { 28, 196, 184, 32 },
    { 28, 232, 184, 32 },
};
static const char *const k_algorithm_names[SCALE_ALGORITHM_COUNT] = {
    "NEAREST", "BILINEAR", "NATIVE",
};
static const char *const k_lcd_theme_names[GAM4980_LCD_THEME_COUNT] = {
    "OFF", "GREEN", "BLUE", "YELLOW",
};
static const char *const k_settings_tab_names[SETTINGS_TAB_COUNT] = {
    "DISPLAY", "AUDIO", "GAME",
};
static const u8 k_audio_volume_values[] = {
    25u, 50u, 75u, 100u,
};
static const char *const k_audio_volume_names[] = {
    "25", "50", "75", "100",
};
#define AUDIO_VOLUME_LEVEL_COUNT \
    ((u32)(sizeof(k_audio_volume_values) / sizeof(k_audio_volume_values[0])))

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
static u16 *g_ghost_framebuffer;
static u8 *g_frame_queue;
static bda_handle_t g_frame;
static bda_handle_t g_draw;
static bda_handle_t g_draw_owner;
static void *g_draw_object;
static touch_event_t g_touch_queue[TOUCH_QUEUE_SIZE];
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
static int g_input_panel = INPUT_PANEL_GAMEPAD;
static int g_lcd_theme = GAM4980_LCD_THEME_OFF;
static int g_lcd_ghosting;
static int g_ghost_frame_valid;
static int g_ghost_fade_pending;
static int g_keyboard_page = KEYBOARD_PAGE_ALPHA;
static const touch_button_t *g_touch_button;
static u32 g_touch_hold_ticks;
static int g_touch_key_active;
static int g_touch_had_key;
static int g_settings_open;
static int g_settings_tab = SETTINGS_TAB_DISPLAY;
static int g_settings_selection;
static int g_settings_key_release_ticks;
static int g_session_action;
static u32 g_frame_queue_read;
static u32 g_frame_queue_write;
static u32 g_frame_queue_count;
static u32 g_frame_queue_drops;
static u32 g_frame_present_hold_ticks;
static s16 g_pcm[PCM_SAMPLE_COUNT];
static u32 g_audio_blocks_written;
static u32 g_audio_write_errors;
static u32 g_audio_padded_samples;
static int g_audio_open;
static int g_audio_enabled = 1;
static int g_audio_volume = 3;

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

static void audio_stream_stop(void)
{
    if (!g_audio_open)
        return;
    bda_audio_stop();
    g_audio_open = 0;
    log_hex_value("AUDIO BLOCKS=", g_audio_blocks_written);
    log_hex_value("AUDIO ERRORS=", g_audio_write_errors);
    log_hex_value("AUDIO PADDED=", g_audio_padded_samples);
    log_hex_value("AUDIO DROPS=", gam4980_audio_dropped());
    log_line("AUDIO STOP");
}

static void audio_discard_core(void)
{
    u32 available;

    while ((available = gam4980_audio_available()) != 0u) {
        if (available > PCM_SAMPLE_COUNT)
            available = PCM_SAMPLE_COUNT;
        if (gam4980_audio_read(g_pcm, available) != available)
            break;
    }
}

static void audio_apply_volume(u32 sample_count)
{
    u32 index;
    u32 volume = k_audio_volume_values[g_audio_volume];

    if (volume == 100u)
        return;
    for (index = 0; index < sample_count; ++index)
        g_pcm[index] = (s16)(((s32)g_pcm[index] * (s32)volume) / 100);
}

static void audio_stream_start(void)
{
    u32 block;

    if (!g_audio_enabled || g_audio_open)
        return;
    audio_discard_core();
    g_audio_blocks_written = 0;
    g_audio_write_errors = 0;
    g_audio_padded_samples = 0;
    log_line("AUDIO START");
    bda_audio_open_pcm(
        BDA_AUDIO_SAMPLE_RATE_22050,
        BDA_AUDIO_BITS_16,
        BDA_AUDIO_CHANNELS_MONO
    );
    g_audio_open = 1;

    bda_memset(g_pcm, 0, PCM_BYTE_COUNT);
    for (block = 0; block < PCM_PREFILL_BLOCK_COUNT; ++block) {
        int written;

        if (!bda_audio_ready())
            break;
        written = bda_audio_write(g_pcm, PCM_BYTE_COUNT);
        if (written != (int)PCM_BYTE_COUNT) {
            ++g_audio_write_errors;
            log_line("AUDIO PREFILL FAILED");
            audio_stream_stop();
            return;
        }
        ++g_audio_blocks_written;
    }
}

static void audio_stream_pump(void)
{
    u32 blocks = 0;

    while (g_audio_enabled && g_audio_open &&
           blocks < PCM_PUMP_BLOCK_LIMIT &&
           bda_audio_ready()) {
        u32 sample_count = gam4980_audio_available();
        int written;

        if (sample_count > PCM_SAMPLE_COUNT)
            sample_count = PCM_SAMPLE_COUNT;
        if (sample_count < PCM_SAMPLE_COUNT) {
            bda_memset(g_pcm, 0, PCM_BYTE_COUNT);
            g_audio_padded_samples += PCM_SAMPLE_COUNT - sample_count;
        }
        if (sample_count &&
            gam4980_audio_read(g_pcm, sample_count) != sample_count) {
            ++g_audio_write_errors;
            log_line("AUDIO CORE READ FAILED");
            audio_stream_stop();
            return;
        }
        audio_apply_volume(sample_count);
        written = bda_audio_write(g_pcm, PCM_BYTE_COUNT);
        if (written != (int)PCM_BYTE_COUNT) {
            ++g_audio_write_errors;
            log_line("AUDIO WRITE FAILED");
            audio_stream_stop();
            return;
        }
        ++g_audio_blocks_written;
        ++blocks;
    }
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
    if (valid_pointer(g_ghost_framebuffer))
        bda_free(g_ghost_framebuffer);
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
    g_ghost_framebuffer = 0;
    g_scaled_framebuffer = 0;
    g_screen_pixels = 0;
    g_frame_queue = 0;
}

static int allocate_buffers(void)
{
    bda_memset(&g_buffers, 0, sizeof(g_buffers));
    g_ghost_framebuffer = 0;
    g_scaled_framebuffer = 0;
    g_screen_pixels = 0;
    g_frame_queue = 0;

    g_buffers.ram = (u8 *)bda_alloc(GAM4980_RAM_SIZE);
    g_buffers.flash = (u8 *)bda_alloc(GAM4980_FLASH_SIZE);
    g_buffers.rom_8 = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.rom_e = (u8 *)bda_alloc(GAM4980_ROM_SIZE);
    g_buffers.framebuffer = (u16 *)bda_alloc(CORE_FRAME_BYTES);
    g_ghost_framebuffer = (u16 *)bda_alloc(CORE_FRAME_BYTES);
    g_scaled_framebuffer = (u16 *)bda_alloc(VIEW_FRAME_BYTES);
    g_screen_pixels = (u16 *)bda_alloc(SCREEN_FRAME_BYTES);
    g_frame_queue = (u8 *)bda_alloc(FRAME_QUEUE_BYTES);
    if (!valid_pointer(g_buffers.ram) || !valid_pointer(g_buffers.flash) ||
        !valid_pointer(g_buffers.rom_8) || !valid_pointer(g_buffers.rom_e) ||
        !valid_pointer(g_buffers.framebuffer) ||
        !valid_pointer(g_ghost_framebuffer) ||
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

static void load_ui_config(void)
{
    u8 data[10];
    int file;
    int size;
    u32 read_size;

    g_scale_algorithm = SCALE_BILINEAR;
    g_input_panel = INPUT_PANEL_GAMEPAD;
    g_lcd_theme = GAM4980_LCD_THEME_OFF;
    g_lcd_ghosting = 0;
    g_audio_enabled = 1;
    g_audio_volume = (int)AUDIO_VOLUME_LEVEL_COUNT - 1;
    file = bda_fs_fopen_raw(k_config_path, "rb");
    if (!bda_fs_file_is_valid(file))
        return;
    if (bda_fs_seek_raw(file, 0, BDA_SEEK_END) < 0) {
        (void)bda_fs_close_raw(file);
        return;
    }
    size = bda_fs_tell_raw(file);
    if (size < 8 || bda_fs_seek_raw(file, 0, BDA_SEEK_SET) < 0) {
        (void)bda_fs_close_raw(file);
        return;
    }
    read_size = (u32)size;
    if (read_size > sizeof(data))
        read_size = sizeof(data);
    if (read_exact(file, data, read_size) &&
        data[0] == 'G' && data[1] == '4' &&
        data[2] == 'S' && data[3] == '1') {
        if (data[4] < SCALE_ALGORITHM_COUNT)
            g_scale_algorithm = data[4];
        if (data[5] <= INPUT_PANEL_KEYBOARD)
            g_input_panel = data[5];
        if (data[6] < GAM4980_LCD_THEME_COUNT)
            g_lcd_theme = data[6];
        if (data[7] <= 1u)
            g_lcd_ghosting = data[7];
        if (read_size >= 9u && data[8] <= 1u)
            g_audio_enabled = data[8];
        if (read_size >= 10u && data[9] < AUDIO_VOLUME_LEVEL_COUNT)
            g_audio_volume = data[9];
    }
    (void)bda_fs_close_raw(file);
}

static void write_ui_config(void)
{
    u8 data[10] = { 'G', '4', 'S', '1', 0, 0, 0, 0, 0, 0 };
    int file;

    data[4] = (u8)g_scale_algorithm;
    data[5] = (u8)g_input_panel;
    data[6] = (u8)g_lcd_theme;
    data[7] = (u8)g_lcd_ghosting;
    data[8] = (u8)g_audio_enabled;
    data[9] = (u8)g_audio_volume;
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

    if (g_touch_key_active && g_touch_button == button) {
        border = rgb565(238, 177, 45);
        fill = rgb565(35, 91, 91);
    }

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

static void draw_keyboard_icon(int center_x, int center_y, u16 color)
{
    int column;

    fill_rect(center_x - 9, center_y - 7, 19, 2, color);
    fill_rect(center_x - 9, center_y + 5, 19, 2, color);
    fill_rect(center_x - 9, center_y - 5, 2, 10, color);
    fill_rect(center_x + 8, center_y - 5, 2, 10, color);
    for (column = 0; column < 5; ++column) {
        fill_rect(center_x - 6 + column * 3, center_y - 3, 2, 2, color);
        fill_rect(center_x - 6 + column * 3, center_y, 2, 2, color);
    }
    fill_rect(center_x - 4, center_y + 3, 9, 1, color);
}

static void draw_gamepad_icon(int center_x, int center_y, u16 color, u16 hole)
{
    fill_rect(center_x - 7, center_y - 5, 15, 9, color);
    fill_rect(center_x - 9, center_y - 2, 4, 8, color);
    fill_rect(center_x + 6, center_y - 2, 4, 8, color);
    fill_rect(center_x - 5, center_y - 1, 5, 2, hole);
    fill_rect(center_x - 3, center_y - 3, 2, 6, hole);
    fill_rect(center_x + 2, center_y - 1, 2, 2, hole);
    fill_rect(center_x + 5, center_y - 3, 2, 2, hole);
}

static void draw_folder_icon(int center_x, int center_y, u16 color, u16 hole)
{
    fill_rect(center_x - 8, center_y - 6, 8, 3, color);
    fill_rect(center_x - 8, center_y - 4, 17, 11, color);
    fill_rect(center_x - 6, center_y - 2, 13, 7, hole);
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

static void draw_lcd_swatch(int x, int y)
{
    u16 outline = rgb565(146, 164, 170);

    fill_rect(x, y, 22, 14, outline);
    fill_rect(x + 2, y + 2, 18, 10,
              gam4980_lcd_background_color());
    fill_rect(x + 5, y + 5, 12, 4,
              gam4980_lcd_foreground_color());
}

static void draw_toggle(int x, int y, int enabled)
{
    u16 outline = rgb565(146, 164, 170);
    u16 track = enabled ? rgb565(41, 178, 178) : rgb565(35, 51, 58);
    u16 knob = rgb565(238, 244, 239);

    fill_rect(x, y, 34, 16, outline);
    fill_rect(x + 2, y + 2, 30, 12, track);
    fill_rect(enabled ? x + 19 : x + 3, y + 3, 12, 10, knob);
}

static void draw_settings_row(void)
{
    u16 border = rgb565(55, 76, 84);
    u16 fill = rgb565(17, 31, 39);
    u16 text = rgb565(238, 244, 239);
    u16 accent = rgb565(41, 178, 178);

    fill_rect(8, SETTINGS_ROW_Y, 224, SETTINGS_ROW_HEIGHT, border);
    fill_rect(10, SETTINGS_ROW_Y + 2, 220, SETTINGS_ROW_HEIGHT - 4, fill);

    if (g_input_panel == INPUT_PANEL_KEYBOARD) {
        const char *page_label = g_keyboard_page == KEYBOARD_PAGE_ALPHA ?
            "FN" : "ABC";
        int page_width = label_width(page_label, 1);

        fill_rect(
            k_keyboard_page_button.x, k_keyboard_page_button.y,
            k_keyboard_page_button.width, k_keyboard_page_button.height,
            accent
        );
        fill_rect(
            k_keyboard_page_button.x + 2, k_keyboard_page_button.y + 2,
            k_keyboard_page_button.width - 4,
            k_keyboard_page_button.height - 4, fill
        );
        draw_text(
            k_keyboard_page_button.x +
                (k_keyboard_page_button.width - page_width) / 2,
            k_keyboard_page_button.y + 8, page_label, 1, text
        );
    }

    fill_rect(
        k_change_game_button.x, k_change_game_button.y,
        k_change_game_button.width, k_change_game_button.height, accent
    );
    fill_rect(
        k_change_game_button.x + 2, k_change_game_button.y + 2,
        k_change_game_button.width - 4,
        k_change_game_button.height - 4, fill
    );
    draw_folder_icon(
        k_change_game_button.x + k_change_game_button.width / 2,
        k_change_game_button.y + k_change_game_button.height / 2,
        text, fill
    );

    fill_rect(
        k_input_panel_button.x, k_input_panel_button.y,
        k_input_panel_button.width, k_input_panel_button.height, accent
    );
    fill_rect(
        k_input_panel_button.x + 2, k_input_panel_button.y + 2,
        k_input_panel_button.width - 4, k_input_panel_button.height - 4, fill
    );
    if (g_input_panel == INPUT_PANEL_GAMEPAD) {
        draw_keyboard_icon(
            k_input_panel_button.x + k_input_panel_button.width / 2,
            k_input_panel_button.y + k_input_panel_button.height / 2,
            text
        );
    } else {
        draw_gamepad_icon(
            k_input_panel_button.x + k_input_panel_button.width / 2,
            k_input_panel_button.y + k_input_panel_button.height / 2,
            text, fill
        );
    }

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
    const ui_rect_t panel = { 14, 18, 212, 284 };
    u16 panel_border = rgb565(238, 177, 45);
    u16 panel_fill = rgb565(14, 25, 32);
    u16 row_fill = rgb565(22, 34, 42);
    u16 row_border = rgb565(55, 76, 84);
    u16 focus = rgb565(41, 178, 178);
    u16 active_fill = rgb565(35, 91, 91);
    u16 text = rgb565(238, 244, 239);
    int title_width = label_width("SETTINGS", 2);
    int index;

    fill_rect(panel.x, panel.y, panel.width, panel.height, panel_border);
    fill_rect(panel.x + 2, panel.y + 2, panel.width - 4, panel.height - 4,
              panel_fill);
    draw_text((SCREEN_WIDTH - title_width) / 2, 29, "SETTINGS", 2, text);

    fill_rect(
        k_settings_close.x, k_settings_close.y,
        k_settings_close.width, k_settings_close.height, row_border
    );
    fill_rect(
        k_settings_close.x + 2, k_settings_close.y + 2,
        k_settings_close.width - 4, k_settings_close.height - 4, row_fill
    );
    draw_close_icon(&k_settings_close, text);

    for (index = 0; index < SETTINGS_TAB_COUNT; ++index) {
        const ui_rect_t *tab = &k_settings_tabs[index];
        const char *label = k_settings_tab_names[index];
        int width = label_width(label, 1);
        int active = index == g_settings_tab;

        fill_rect(tab->x, tab->y, tab->width, tab->height,
                  active ? focus : row_border);
        fill_rect(tab->x + 2, tab->y + 2, tab->width - 4, tab->height - 4,
                  active ? active_fill : row_fill);
        draw_text(tab->x + (tab->width - width) / 2,
                  tab->y + (tab->height - 7) / 2, label, 1, text);
    }

    if (g_settings_tab == SETTINGS_TAB_DISPLAY) {
        for (index = 0; index < SCALE_ALGORITHM_COUNT; ++index) {
            const ui_rect_t *row = &k_settings_options[index];
            u16 border = index == g_settings_selection ? focus : row_border;

            fill_rect(row->x, row->y, row->width, row->height, border);
            fill_rect(row->x + 2, row->y + 2,
                      row->width - 4, row->height - 4, row_fill);
            draw_radio(row->x + 18, row->y + row->height / 2,
                       index == g_scale_algorithm);
            draw_text(row->x + 38, row->y + (row->height - 14) / 2,
                      k_algorithm_names[index], 2, text);
        }

        {
            const ui_rect_t *row =
                &k_settings_options[DISPLAY_SETTINGS_LCD_THEME];
            const char *name = k_lcd_theme_names[g_lcd_theme];
            int width = label_width(name, 2);
            u16 border = DISPLAY_SETTINGS_LCD_THEME == g_settings_selection ?
                         focus : row_border;

            fill_rect(row->x, row->y, row->width, row->height, border);
            fill_rect(row->x + 2, row->y + 2,
                      row->width - 4, row->height - 4, row_fill);
            draw_lcd_swatch(row->x + 10, row->y + 9);
            draw_text(row->x + 40, row->y + 9, "COLOR", 2, text);
            draw_text(row->x + row->width - width - 10, row->y + 9,
                      name, 2, text);
        }

        {
            const ui_rect_t *row =
                &k_settings_options[DISPLAY_SETTINGS_LCD_GHOSTING];
            u16 border = DISPLAY_SETTINGS_LCD_GHOSTING ==
                         g_settings_selection ? focus : row_border;

            fill_rect(row->x, row->y, row->width, row->height, border);
            fill_rect(row->x + 2, row->y + 2,
                      row->width - 4, row->height - 4, row_fill);
            draw_text(row->x + 12, row->y + 9, "GHOST", 2, text);
            draw_toggle(row->x + row->width - 46, row->y + 8,
                        g_lcd_ghosting);
        }
    } else if (g_settings_tab == SETTINGS_TAB_AUDIO) {
        const ui_rect_t *enabled_row =
            &k_settings_options[AUDIO_SETTINGS_ENABLED];
        const ui_rect_t *volume_row =
            &k_settings_options[AUDIO_SETTINGS_VOLUME];
        const char *volume_name = k_audio_volume_names[g_audio_volume];
        int volume_width = label_width(volume_name, 2);
        u16 enabled_border = g_settings_selection ==
            AUDIO_SETTINGS_ENABLED ? focus : row_border;
        u16 volume_border = g_settings_selection ==
            AUDIO_SETTINGS_VOLUME ? focus : row_border;

        fill_rect(enabled_row->x, enabled_row->y,
                  enabled_row->width, enabled_row->height, enabled_border);
        fill_rect(enabled_row->x + 2, enabled_row->y + 2,
                  enabled_row->width - 4, enabled_row->height - 4, row_fill);
        draw_text(enabled_row->x + 12, enabled_row->y + 9,
                  "AUDIO", 2, text);
        draw_toggle(enabled_row->x + enabled_row->width - 46,
                    enabled_row->y + 8, g_audio_enabled);

        fill_rect(volume_row->x, volume_row->y,
                  volume_row->width, volume_row->height, volume_border);
        fill_rect(volume_row->x + 2, volume_row->y + 2,
                  volume_row->width - 4, volume_row->height - 4, row_fill);
        draw_text(volume_row->x + 12, volume_row->y + 9,
                  "VOLUME", 2, text);
        draw_text(volume_row->x + volume_row->width - volume_width - 12,
                  volume_row->y + 9, volume_name, 2, text);
    } else {
        const ui_rect_t *row = &k_settings_options[GAME_SETTINGS_RESTART];
        int width = label_width("RESTART GAME", 2);
        u16 border = g_settings_selection == GAME_SETTINGS_RESTART ?
                     focus : row_border;

        fill_rect(row->x, row->y, row->width, row->height, border);
        fill_rect(row->x + 2, row->y + 2, row->width - 4, row->height - 4,
                  row_fill);
        draw_text(row->x + (row->width - width) / 2,
                  row->y + (row->height - 14) / 2,
                  "RESTART GAME", 2, text);
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

static int rgb565_close(u16 first, u16 second)
{
    u32 first_red = first >> 11;
    u32 second_red = second >> 11;
    u32 first_green = (first >> 5) & 63u;
    u32 second_green = (second >> 5) & 63u;
    u32 first_blue = first & 31u;
    u32 second_blue = second & 31u;
    u32 red_delta = first_red > second_red ?
                    first_red - second_red : second_red - first_red;
    u32 green_delta = first_green > second_green ?
                      first_green - second_green : second_green - first_green;
    u32 blue_delta = first_blue > second_blue ?
                     first_blue - second_blue : second_blue - first_blue;

    return red_delta <= 1u && green_delta <= 3u && blue_delta <= 1u;
}

static void reset_ghost_frame(void)
{
    g_ghost_frame_valid = 0;
    g_ghost_fade_pending = 0;
}

static const u16 *prepare_lcd_source(const u16 *source, int advance_ghost)
{
    const u32 pixel_count = LCD_SOURCE_WIDTH * GAM4980_LCD_HEIGHT;
    u16 background;
    u32 index;
    int pending = 0;

    if (!g_lcd_ghosting || !g_ghost_framebuffer)
        return source;
    if (!g_ghost_frame_valid) {
        bda_memcpy(g_ghost_framebuffer, source, CORE_FRAME_BYTES);
        g_ghost_frame_valid = 1;
        g_ghost_fade_pending = 0;
        return g_ghost_framebuffer;
    }
    if (!advance_ghost)
        return g_ghost_framebuffer;

    background = gam4980_lcd_background_color();
    for (index = 0; index < pixel_count; ++index) {
        u16 target = source[index];
        u16 current = g_ghost_framebuffer[index];
        u16 next = target;

        if (target == background && current != target) {
            next = lerp_rgb565(current, target, 176u);
            if (rgb565_close(next, target))
                next = target;
        }
        g_ghost_framebuffer[index] = next;
        if (next != target)
            pending = 1;
    }
    g_ghost_fade_pending = pending;
    return g_ghost_framebuffer;
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

static void current_touch_buttons(
    const touch_button_t **buttons,
    u32 *button_count
) {
    if (g_input_panel == INPUT_PANEL_KEYBOARD) {
        if (g_keyboard_page == KEYBOARD_PAGE_FUNCTION) {
            *buttons = k_function_keyboard_buttons;
            *button_count = sizeof(k_function_keyboard_buttons) /
                sizeof(k_function_keyboard_buttons[0]);
        } else {
            *buttons = k_alpha_keyboard_buttons;
            *button_count = sizeof(k_alpha_keyboard_buttons) /
                sizeof(k_alpha_keyboard_buttons[0]);
        }
    } else {
        *buttons = k_gamepad_buttons;
        *button_count = sizeof(k_gamepad_buttons) /
            sizeof(k_gamepad_buttons[0]);
    }
}

static void render_game_screen(const u16 *source)
{
    u16 background = rgb565(10, 20, 27);
    u16 frame = rgb565(238, 177, 45);
    const touch_button_t *buttons;
    u32 button_count;
    u32 index;

    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, background);
    fill_rect(0, VIEW_Y - 3, SCREEN_WIDTH, VIEW_HEIGHT + 6,
              rgb565(36, 55, 63));
    fill_rect(0, VIEW_Y - 2, SCREEN_WIDTH, VIEW_HEIGHT + 4, frame);
    scale_lcd_to_view(source);
    copy_lcd_to_full_screen();
    draw_settings_row();
    current_touch_buttons(&buttons, &button_count);
    for (index = 0; index < button_count; ++index)
        draw_button(&buttons[index]);
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

static int present_screen(const u8 *packed_frame, int advance_ghost)
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
    source = prepare_lcd_source(source, advance_ghost);
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

static void release_touch_key(void)
{
    if (g_touch_key_active)
        g_full_redraw = 1;
    g_touch_button = 0;
    g_touch_hold_ticks = 0;
    g_touch_key_active = 0;
}

static const touch_button_t *find_touch_button(int x, int y)
{
    const touch_button_t *buttons;
    u32 button_count;
    u32 index;

    current_touch_buttons(&buttons, &button_count);
    for (index = 0; index < button_count; ++index) {
        if (point_in_button(x, y, &buttons[index]))
            return &buttons[index];
    }
    return 0;
}

static void press_touch_button(const touch_button_t *button)
{
    if (g_touch_key_active && g_touch_button == button)
        return;
    release_touch_key();
    clear_frame_queue();
    gam4980_key_down(button->key);
    g_touch_button = button;
    g_touch_hold_ticks = 0;
    g_touch_key_active = 1;
    g_touch_had_key = 1;
    g_full_redraw = 1;
}

static int settings_item_count(void)
{
    if (g_settings_tab == SETTINGS_TAB_DISPLAY)
        return DISPLAY_SETTINGS_ITEM_COUNT;
    if (g_settings_tab == SETTINGS_TAB_AUDIO)
        return AUDIO_SETTINGS_ITEM_COUNT;
    return GAME_SETTINGS_ITEM_COUNT;
}

static void select_settings_tab(int tab)
{
    if (tab < 0)
        tab = SETTINGS_TAB_COUNT - 1;
    else if (tab >= SETTINGS_TAB_COUNT)
        tab = 0;
    g_settings_tab = tab;
    g_settings_selection = tab == SETTINGS_TAB_DISPLAY ?
        g_scale_algorithm : 0;
    g_full_redraw = 1;
}

static void open_settings(void)
{
    release_touch_key();
    sync_previous_keys();
    clear_frame_queue();
    g_settings_tab = SETTINGS_TAB_DISPLAY;
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

static void request_session_action(int action)
{
    release_touch_key();
    sync_previous_keys();
    clear_frame_queue();
    g_settings_key_release_ticks = 0;
    g_settings_open = 0;
    g_session_action = action;
    g_close_requested = 1;
    log_line(action == SESSION_ACTION_RESTART ?
             "RESTART REQUESTED" : "CHANGE GAME REQUESTED");
}

static void confirm_change_game(void)
{
    int result;

    release_touch_key();
    sync_previous_keys();
    clear_frame_queue();
    g_touch_read = g_touch_write;
    audio_stream_stop();
    result = bda_confirm(k_window_title, "Select another game?");
    g_touch_read = g_touch_write;
    sync_previous_keys();
    if (result == BDA_DIALOG_RESULT_YES) {
        request_session_action(SESSION_ACTION_CHANGE_GAME);
    } else {
        log_line("GAME CHANGE NOT CONFIRMED");
        audio_stream_start();
        g_full_redraw = 1;
    }
}

static void cycle_lcd_theme(void)
{
    ++g_lcd_theme;
    if (g_lcd_theme >= GAM4980_LCD_THEME_COUNT)
        g_lcd_theme = GAM4980_LCD_THEME_OFF;
    gam4980_set_lcd_theme((u32)g_lcd_theme);
    clear_frame_queue();
    reset_ghost_frame();
    write_ui_config();
    log_line("LCD COLOR CHANGED");
    log_line(k_lcd_theme_names[g_lcd_theme]);
    g_full_redraw = 1;
}

static void toggle_lcd_ghosting(void)
{
    g_lcd_ghosting = !g_lcd_ghosting;
    clear_frame_queue();
    reset_ghost_frame();
    write_ui_config();
    log_line(g_lcd_ghosting ? "LCD GHOSTING ON" : "LCD GHOSTING OFF");
    g_full_redraw = 1;
}

static void toggle_audio_enabled(void)
{
    g_audio_enabled = !g_audio_enabled;
    if (g_audio_enabled) {
        audio_stream_start();
        log_line("AUDIO ENABLED");
    } else {
        audio_stream_stop();
        audio_discard_core();
        log_line("AUDIO DISABLED");
    }
    write_ui_config();
    g_full_redraw = 1;
}

static void cycle_audio_volume(void)
{
    ++g_audio_volume;
    if ((u32)g_audio_volume >= AUDIO_VOLUME_LEVEL_COUNT)
        g_audio_volume = 0;
    write_ui_config();
    log_line("AUDIO VOLUME CHANGED");
    log_line(k_audio_volume_names[g_audio_volume]);
    g_full_redraw = 1;
}

static void apply_settings_selection(void)
{
    if (g_settings_tab == SETTINGS_TAB_DISPLAY) {
        if (g_settings_selection >= 0 &&
            g_settings_selection < SCALE_ALGORITHM_COUNT) {
            if (g_scale_algorithm != g_settings_selection) {
                g_scale_algorithm = g_settings_selection;
                clear_frame_queue();
                write_ui_config();
                log_line("SCALING CHANGED");
                log_line(k_algorithm_names[g_scale_algorithm]);
                g_full_redraw = 1;
            }
        } else if (g_settings_selection == DISPLAY_SETTINGS_LCD_THEME) {
            cycle_lcd_theme();
        } else if (g_settings_selection ==
                   DISPLAY_SETTINGS_LCD_GHOSTING) {
            toggle_lcd_ghosting();
        }
    } else if (g_settings_tab == SETTINGS_TAB_AUDIO) {
        if (g_settings_selection == AUDIO_SETTINGS_ENABLED)
            toggle_audio_enabled();
        else if (g_settings_selection == AUDIO_SETTINGS_VOLUME)
            cycle_audio_volume();
    } else if (g_settings_selection == GAME_SETTINGS_RESTART) {
        request_session_action(SESSION_ACTION_RESTART);
    }
}

static void toggle_input_panel(void)
{
    release_touch_key();
    g_input_panel = g_input_panel == INPUT_PANEL_GAMEPAD ?
        INPUT_PANEL_KEYBOARD : INPUT_PANEL_GAMEPAD;
    clear_frame_queue();
    write_ui_config();
    g_full_redraw = 1;
}

static void toggle_keyboard_page(void)
{
    release_touch_key();
    g_keyboard_page = g_keyboard_page == KEYBOARD_PAGE_ALPHA ?
        KEYBOARD_PAGE_FUNCTION : KEYBOARD_PAGE_ALPHA;
    clear_frame_queue();
    g_full_redraw = 1;
}

static void queue_touch(u32 message, u32 packed)
{
    u32 next = (g_touch_write + 1u) % TOUCH_QUEUE_SIZE;
    if (next == g_touch_read) {
        if (message != BDA_MSG_TOUCH_RELEASE)
            return;
        g_touch_read = (g_touch_read + 1u) % TOUCH_QUEUE_SIZE;
    }
    g_touch_queue[g_touch_write].message = message;
    g_touch_queue[g_touch_write].packed = packed;
    g_touch_write = next;
}

static void handle_touch_release(int x, int y)
{
    const touch_button_t *button;
    u32 index;

    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    if (g_settings_open) {
        if (point_in_rect(x, y, &k_settings_close)) {
            close_settings();
            return;
        }
        for (index = 0; index < SETTINGS_TAB_COUNT; ++index) {
            if (point_in_rect(x, y, &k_settings_tabs[index])) {
                select_settings_tab((int)index);
                return;
            }
        }
        for (index = 0; index < (u32)settings_item_count(); ++index) {
            if (point_in_rect(x, y, &k_settings_options[index])) {
                g_settings_selection = (int)index;
                apply_settings_selection();
                return;
            }
        }
        return;
    }
    if (point_in_rect(x, y, &k_change_game_button)) {
        confirm_change_game();
        return;
    }
    if (g_input_panel == INPUT_PANEL_KEYBOARD &&
        point_in_rect(x, y, &k_keyboard_page_button)) {
        toggle_keyboard_page();
        return;
    }
    if (point_in_rect(x, y, &k_input_panel_button)) {
        toggle_input_panel();
        return;
    }
    if (point_in_rect(x, y, &k_settings_button)) {
        open_settings();
        return;
    }
    button = find_touch_button(x, y);
    if (button) {
        clear_frame_queue();
        gam4980_key_down(button->key);
    }
}

static void drain_touches(void)
{
    while (g_touch_read != g_touch_write) {
        touch_event_t event = g_touch_queue[g_touch_read];
        int x = (s32)(short)(event.packed & 0xffffu);
        int y = (s32)(short)(event.packed >> 16);

        g_touch_read = (g_touch_read + 1u) % TOUCH_QUEUE_SIZE;
        if (event.message == BDA_MSG_TOUCH_COORDINATE) {
            const touch_button_t *button = 0;
            if (!g_settings_open && x >= 0 && x < SCREEN_WIDTH &&
                y >= 0 && y < SCREEN_HEIGHT)
                button = find_touch_button(x, y);
            if (button)
                press_touch_button(button);
            else
                release_touch_key();
        } else if (event.message == BDA_MSG_TOUCH_RELEASE) {
            int had_key = g_touch_had_key;
            release_touch_key();
            g_touch_had_key = 0;
            if (!had_key)
                handle_touch_release(x, y);
        }
    }
}

static void poll_touch_repeat(u32 elapsed_ticks)
{
    if (!g_touch_key_active || !g_touch_button ||
        !g_touch_button->repeat)
        return;
    g_touch_hold_ticks += elapsed_ticks;
    if (g_touch_hold_ticks >= TOUCH_REPEAT_DELAY_TICKS) {
        clear_frame_queue();
        gam4980_key_down(g_touch_button->key);
        g_touch_hold_ticks = TOUCH_REPEAT_DELAY_TICKS -
            TOUCH_REPEAT_INTERVAL_TICKS +
            ((g_touch_hold_ticks - TOUCH_REPEAT_DELAY_TICKS) %
             TOUCH_REPEAT_INTERVAL_TICKS);
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
    int item_count;

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
    item_count = settings_item_count();

    if (pressed & (1u << 5)) {
        apply_settings_selection();
    } else if (pressed & (1u << 4)) {
        close_settings();
    } else if (pressed & (1u << 1)) {
        select_settings_tab(g_settings_tab - 1);
    } else if (pressed & (1u << 0)) {
        select_settings_tab(g_settings_tab + 1);
    } else if (pressed & (1u << 3)) {
        --g_settings_selection;
        if (g_settings_selection < 0)
            g_settings_selection = item_count - 1;
        g_full_redraw = 1;
    } else if (pressed & (1u << 2)) {
        ++g_settings_selection;
        if (g_settings_selection >= item_count)
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
    if (message == BDA_MSG_TOUCH_COORDINATE ||
        message == BDA_MSG_TOUCH_RELEASE) {
        queue_touch(message, lparam);
        return 1;
    }
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
    g_touch_button = 0;
    g_touch_hold_ticks = 0;
    g_touch_key_active = 0;
    g_touch_had_key = 0;
    g_detached = 0;
    g_previous_keys = 0;
    g_full_redraw = 1;
    g_escape_pending = 0;
    g_close_requested = 0;
    g_frame_count = 0;
    g_core_frame_phase = 0;
    g_core_break_logged = 0;
    g_keyboard_page = KEYBOARD_PAGE_ALPHA;
    g_settings_open = 0;
    g_settings_tab = SETTINGS_TAB_DISPLAY;
    g_settings_selection = g_scale_algorithm;
    g_settings_key_release_ticks = 0;
    g_session_action = SESSION_ACTION_EXIT;
    clear_frame_queue();
    reset_ghost_frame();
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
    audio_stream_start();
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
                    !present_screen(gam4980_packed_frame(), 0))
                    log_line("PRESENT FAILED");
            } else {
                const u8 *queued_frame;

                poll_touch_repeat(elapsed_ticks);
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
                    if (present_screen(queued_frame, 1)) {
                        pop_frame_queue();
                        g_frame_present_hold_ticks =
                            FRAME_PRESENT_HOLD_TICKS;
                    } else {
                        log_line("PRESENT FAILED");
                    }
                } else if (!queued_frame && g_full_redraw &&
                           !present_screen(gam4980_packed_frame(), 0)) {
                    log_line("PRESENT FAILED");
                } else if (!queued_frame && !g_frame_present_hold_ticks &&
                           g_lcd_ghosting && g_ghost_fade_pending) {
                    if (present_screen(gam4980_packed_frame(), 1)) {
                        g_frame_present_hold_ticks =
                            FRAME_PRESENT_HOLD_TICKS;
                    } else {
                        log_line("PRESENT FAILED");
                    }
                }
            }
        }
        if (g_audio_enabled && g_audio_open)
            audio_stream_pump();
        else
            audio_discard_core();
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
            audio_stream_stop();
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
    audio_stream_stop();
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
    int select_game = 1;

    (void)bda_fs_mkdir(k_data_dir);
    (void)bda_fs_mkdir(k_game_dir);
    reset_log();
    load_ui_config();
    log_line("START STANDALONE");
    g_file_selector.path[0] = 0;
    for (;;) {
        if (select_game) {
            char previous_game_path[BDA_FILE_SELECTOR_PATH_SIZE];
            int had_previous_game = g_file_selector.path[0] != 0;

            copy_text(previous_game_path, g_file_selector.path,
                      sizeof(previous_game_path));
            selector_result = bda_gui_select_file(
                &g_file_selector, k_game_selector_path, "gam", "Select GAM"
            );
            if (selector_result == BDA_FILE_SELECTOR_SELECTED) {
                log_line("GAME SELECTED");
                log_line(g_file_selector.path);
            } else if (had_previous_game) {
                copy_text(g_file_selector.path, previous_game_path,
                          sizeof(g_file_selector.path));
                if (selector_result == BDA_FILE_SELECTOR_CANCELLED) {
                    log_line("GAME CHANGE CANCELLED");
                } else {
                    log_line("GAME SELECTOR ERROR");
                    bda_msgbox(
                        k_window_title,
                        "Could not open the .gam file selector. The current game will restart."
                    );
                }
            } else {
                if (selector_result == BDA_FILE_SELECTOR_CANCELLED) {
                    log_line("GAME SELECTION CANCELLED");
                } else {
                    bda_msgbox(
                        k_window_title,
                        "Could not open the .gam file selector."
                    );
                    result = -1;
                }
                goto cleanup;
            }
        }
        select_game = 0;
        g_save_path[0] = 0;
        g_loaded_game_size = 0;
        if (!allocate_buffers()) {
            bda_msgbox(
                k_window_title,
                "Not enough memory (requires about 6.4 MiB)."
            );
            result = -2;
            goto cleanup;
        }
        if (!load_fixed_file(
                k_rom_8_path, g_buffers.rom_8, GAM4980_ROM_SIZE)) {
            bda_msgbox(
                k_window_title,
                "Missing or invalid 8.BIN in application data."
            );
            result = -3;
            goto cleanup;
        }
        if (!load_fixed_file(
                k_rom_e_path, g_buffers.rom_e, GAM4980_ROM_SIZE)) {
            bda_msgbox(
                k_window_title,
                "Missing or invalid E.BIN in application data."
            );
            result = -4;
            goto cleanup;
        }
        if (gam4980_init(&g_buffers) <= 0) {
            bda_msgbox(k_window_title, "Firmware initialization failed.");
            result = -5;
            goto cleanup;
        }
        gam4980_set_lcd_theme((u32)g_lcd_theme);
        if (load_game(g_file_selector.path) <= 0) {
            bda_msgbox(
                k_window_title,
                "The selected .gam file is invalid or unreadable."
            );
            result = -6;
            goto cleanup;
        }
        log_hex_value("GAME SIZE=", g_loaded_game_size);
        if (!run_window()) {
            bda_msgbox(
                k_window_title, "Could not create the emulator window."
            );
            result = -7;
            goto cleanup;
        }
        write_save();
        release_buffers();
        g_save_path[0] = 0;
        if (g_session_action == SESSION_ACTION_RESTART) {
            log_line("RESTARTING GAME");
            continue;
        }
        if (g_session_action == SESSION_ACTION_CHANGE_GAME) {
            log_line("OPENING GAME SELECTOR");
            select_game = 1;
            continue;
        }
        break;
    }

cleanup:
    release_buffers();
    log_line("END");
    return result;
}

/* Single translation unit: the upstream-derived core includes s6502.c. */
#include "gam4980_core.c"
