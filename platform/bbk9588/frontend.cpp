#include "bda_dialogs.h"
#include "bda_graphics.h"
#include "bda_input.h"
#include "bda_time.h"
#include "bda_window.h"

#include "comm.h"
#include "cpu.h"
#include "nc2000.h"
#include "platform/bbk9588/core_stubs.h"
#include "platform/bbk9588/diagnostic_log.h"
#include "platform/bbk9588/jit_mips32.h"
#include "platform/bbk9588/sound_bbk.h"
#include "settings.h"
#include "sound.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern WqxRom nc2k_rom;

static const int screen_width = 240;
static const int screen_height = 320;
static const int lcd_view_height = 120;
static const int lcd_x = (screen_width - SCREEN_WIDTH) / 2;
static const int lcd_y = (lcd_view_height - SCREEN_HEIGHT) / 2;
static const int exit_button_x = 208;
static const int exit_button_y = 8;
static const int exit_button_width = 24;
static const int exit_button_height = 24;
static const uint32_t render_interval_ms = 50u;
static const uint32_t diagnostic_interval_ms = 5000u;
static const int escape_hold_ticks = 32;
static const int close_pump_limit = 128;

struct KeyDefinition {
    int x;
    int y;
    int width;
    int height;
    int row;
    int column;
    const char *label;
};

#define KEY10(index, y, row, column, label) \
    { (index) * 24, (y), 24, 32, (row), (column), (label) }

static const KeyDefinition keys[] = {
    KEY10(0,128,0,4,"Q"), KEY10(1,128,1,4,"W"),
    KEY10(2,128,2,4,"E"), KEY10(3,128,3,4,"R"),
    KEY10(4,128,4,4,"T"), KEY10(5,128,5,4,"Y"),
    KEY10(6,128,6,4,"U"), KEY10(7,128,7,4,"I"),
    KEY10(8,128,0,3,"O"), KEY10(9,128,4,3,"P"),

    KEY10(0,160,0,5,"A"), KEY10(1,160,1,5,"S"),
    KEY10(2,160,2,5,"D"), KEY10(3,160,3,5,"F"),
    KEY10(4,160,4,5,"G"), KEY10(5,160,5,5,"H"),
    KEY10(6,160,6,5,"J"), KEY10(7,160,7,5,"K"),
    KEY10(8,160,1,3,"L"), KEY10(9,160,5,3,"ENT"),

    KEY10(0,192,0,6,"Z"), KEY10(1,192,1,6,"X"),
    KEY10(2,192,2,6,"C"), KEY10(3,192,3,6,"V"),
    KEY10(4,192,4,6,"B"), KEY10(5,192,5,6,"N"),
    KEY10(6,192,6,6,"M"), KEY10(7,192,5,2,"SAY"),
    KEY10(8,192,6,3,"PDN"), KEY10(9,192,6,7,"SPC"),

    KEY10(0,224,0,7,"?"), KEY10(1,224,1,7,"SHF"),
    KEY10(2,224,2,7,"IM"), KEY10(3,224,3,7,"ESC"),
    KEY10(4,224,4,7,"SYM"), KEY10(5,224,5,7,"DOT"),
    KEY10(6,224,7,7,"<"), KEY10(7,224,2,3,"^"),
    KEY10(8,224,3,3,"v"), KEY10(9,224,7,3,">"),

    {0,256,34,32,0,2,"F1"}, {34,256,34,32,1,2,"F2"},
    {68,256,34,32,2,2,"F3"}, {102,256,34,32,3,2,"F4"},
    {136,256,34,32,7,6,"PUP"}, {170,256,34,32,4,2,"CLK"},
    {204,256,36,32,0,0,"PWR"},

    {0,288,34,32,3,1,"DICT"}, {34,288,34,32,4,1,"CARD"},
    {68,288,34,32,5,1,"CALC"}, {102,288,34,32,2,1,"TRIP"},
    {136,288,34,32,1,1,"DATA"}, {170,288,34,32,0,1,"TIME"},
    {204,288,36,32,6,1,"NET"}
};

static uint16_t frame_pixels[screen_width * screen_height]
    __attribute__((aligned(4)));
static uint8_t lcd_pixels[SCREEN_WIDTH * SCREEN_HEIGHT / 8 * 2];
static uint8_t next_lcd_pixels[SCREEN_WIDTH * SCREEN_HEIGHT / 8 * 2];
static bda_gui_framebuffer_t framebuffer;
static bda_handle_t frame;
static int detached;
static int active_touch_key = -1;
static int touch_down;
static int touch_release_pending;
static int exit_touch_tracking;
static int exit_touch_inside;
static int exit_click_pending;
static bool applied_keys[8][8];
static int rendered_touch_key = -2;
static int rendered_exit_pressed = -1;
static int lcd_initialized;
static int rendered_grey_mode;
static uint32_t escape_start_tick;
static int escape_down;

static const uint8_t digit_font[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
};

static const uint8_t alpha_font[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

static const uint8_t glyph_question[5] = {0x02,0x01,0x51,0x09,0x06};
static const uint8_t glyph_left[5] = {0x04,0x0e,0x15,0x04,0x04};
static const uint8_t glyph_up[5] = {0x04,0x02,0x1f,0x02,0x04};
static const uint8_t glyph_down[5] = {0x04,0x08,0x1f,0x08,0x04};
static const uint8_t glyph_right[5] = {0x04,0x04,0x15,0x0e,0x04};

static uint16_t rgb565(unsigned red, unsigned green, unsigned blue)
{
    return (uint16_t)(((red & 0xf8u) << 8) | ((green & 0xfcu) << 3) | (blue >> 3));
}

static const uint8_t *glyph_for(char character)
{
    if (character >= 'A' && character <= 'Z') return alpha_font[character - 'A'];
    if (character >= '0' && character <= '9') return digit_font[character - '0'];
    if (character == '?') return glyph_question;
    if (character == '<') return glyph_left;
    if (character == '^') return glyph_up;
    if (character == 'v') return glyph_down;
    if (character == '>') return glyph_right;
    return 0;
}

static void draw_character(int x, int y, char character, uint16_t color)
{
    const uint8_t *glyph = glyph_for(character);
    if (!glyph) return;
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if ((glyph[column] & (1u << row)) != 0u) {
                int px = x + column;
                int py = y + row;
                if (px >= 0 && px < screen_width && py >= 0 && py < screen_height)
                    frame_pixels[py * screen_width + px] = color;
            }
        }
    }
}

static void draw_label(const KeyDefinition &key, uint16_t color)
{
    int length = (int)strlen(key.label);
    int x = key.x + (key.width - (length * 6 - 1)) / 2;
    int y = key.y + (key.height - 7) / 2;
    for (int index = 0; index < length; ++index) {
        draw_character(x + index * 6, y, key.label[index], color);
    }
}

static void fill_rect(int x, int y, int width, int height, uint16_t color)
{
    for (int row = 0; row < height; ++row) {
        uint16_t *target = &frame_pixels[(y + row) * screen_width + x];
        for (int column = 0; column < width; ++column) target[column] = color;
    }
}

static int exit_button_contains(int x, int y)
{
    return x >= exit_button_x && x < exit_button_x + exit_button_width &&
           y >= exit_button_y && y < exit_button_y + exit_button_height;
}

static void draw_exit_button(int pressed)
{
    uint16_t border = rgb565(38, 18, 18);
    uint16_t normal = rgb565(132, 45, 45);
    uint16_t active = rgb565(198, 62, 62);
    uint16_t mark = rgb565(255, 245, 242);
    fill_rect(exit_button_x, exit_button_y, exit_button_width,
              exit_button_height, border);
    fill_rect(exit_button_x + 1, exit_button_y + 1,
              exit_button_width - 2, exit_button_height - 2,
              pressed ? active : normal);
    for (int offset = 0; offset < 10; ++offset) {
        int left = exit_button_x + 7 + offset;
        int right = exit_button_x + 16 - offset;
        int y = exit_button_y + 7 + offset;
        frame_pixels[y * screen_width + left] = mark;
        frame_pixels[y * screen_width + left + 1] = mark;
        frame_pixels[y * screen_width + right] = mark;
        frame_pixels[y * screen_width + right + 1] = mark;
    }
}

static int key_at(int x, int y)
{
    for (unsigned index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const KeyDefinition &key = keys[index];
        if (x >= key.x && x < key.x + key.width &&
            y >= key.y && y < key.y + key.height) return (int)index;
    }
    return -1;
}

static void draw_keyboard()
{
    uint16_t background = rgb565(32, 40, 44);
    uint16_t normal = rgb565(76, 91, 98);
    uint16_t active = rgb565(45, 145, 180);
    uint16_t border = rgb565(15, 20, 22);
    uint16_t text = rgb565(245, 248, 245);
    fill_rect(0, lcd_view_height, screen_width,
              screen_height - lcd_view_height, background);
    for (unsigned index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const KeyDefinition &key = keys[index];
        fill_rect(key.x, key.y, key.width, key.height, border);
        fill_rect(key.x + 1, key.y + 1, key.width - 2, key.height - 2,
                  (int)index == active_touch_key ? active : normal);
        draw_label(key, text);
    }
}

static int draw_lcd()
{
    static const uint16_t colors[4] = {
        0xded8u, 0xad72u, 0x632cu, 0x0001u
    };
    bool grey = is_grey_mode();
    size_t bytes = grey ? sizeof(lcd_pixels) : sizeof(lcd_pixels) / 2u;
    if (!CopyLcdBuffer(next_lcd_pixels))
        memset(next_lcd_pixels, 0, sizeof(next_lcd_pixels));
    if (lcd_initialized && rendered_grey_mode == (int)grey &&
        memcmp(lcd_pixels, next_lcd_pixels, bytes) == 0) return 0;

    memcpy(lcd_pixels, next_lcd_pixels, bytes);
    if (!lcd_initialized) {
        fill_rect(0, 0, screen_width, lcd_view_height, rgb565(12, 16, 17));
        fill_rect(lcd_x - 2, lcd_y - 2, SCREEN_WIDTH + 4,
                  SCREEN_HEIGHT + 4, rgb565(55, 65, 68));
    }
    for (int source_y = 0; source_y < SCREEN_HEIGHT; ++source_y) {
        for (int source_x = 0; source_x < SCREEN_WIDTH; ++source_x) {
            int level;
            if (grey) {
                int offset = source_y * (SCREEN_WIDTH / 4) + source_x / 4;
                level = (lcd_pixels[offset] >> (6 - (source_x & 3) * 2)) & 3;
            } else {
                int offset = source_y * (SCREEN_WIDTH / 8) + source_x / 8;
                level = (lcd_pixels[offset] & (1u << (7 - (source_x & 7)))) ? 3 : 0;
            }
            frame_pixels[(lcd_y + source_y) * screen_width + lcd_x + source_x] =
                colors[level];
        }
    }
    lcd_initialized = 1;
    rendered_grey_mode = grey;
    return 1;
}

static void render_frame(int force)
{
    int changed = draw_lcd();
    int exit_pressed = exit_touch_tracking && exit_touch_inside;
    if (rendered_exit_pressed != exit_pressed) {
        draw_exit_button(exit_pressed);
        rendered_exit_pressed = exit_pressed;
        changed = 1;
    }
    if (rendered_touch_key != active_touch_key) {
        draw_keyboard();
        rendered_touch_key = active_touch_key;
        changed = 1;
    }
    if (force || changed)
        (void)bda_gui_framebuffer_present_rgb565(&framebuffer, frame_pixels);
}

static void poll_touch()
{
    bda_gui_raw_event_t event;
    int move_pending = 0;
    for (int count = 0; count < 8; ++count) {
        if (bda_gui_raw_event_fetch(&event) < 0) break;
        if (event.code == BDA_INPUT_EVENT_TOUCH_DOWN) {
            uint16_t x, y;
            touch_down = 1;
            bda_gui_touch_position(&x, &y);
            exit_touch_tracking = exit_button_contains(x, y);
            exit_touch_inside = exit_touch_tracking;
            active_touch_key = exit_touch_tracking ? -1 : key_at(x, y);
        } else if (event.code == BDA_INPUT_EVENT_TOUCH_MOVE) {
            if (touch_down) move_pending = 1;
        } else if (event.code == BDA_INPUT_EVENT_TOUCH_UP) {
            uint16_t x, y;
            bda_gui_touch_position(&x, &y);
            touch_down = 0;
            if (exit_touch_tracking) {
                if (exit_button_contains(x, y)) exit_click_pending = 1;
                exit_touch_tracking = 0;
                exit_touch_inside = 0;
                active_touch_key = -1;
            } else {
                // A slow emulation slice can contain both DOWN and UP. Keep the
                // selected key asserted through one slice so short taps are not lost.
                if (active_touch_key >= 0) touch_release_pending = 1;
            }
            move_pending = 0;
            break;
        }
    }
    if (move_pending) {
        uint16_t x, y;
        bda_gui_touch_position(&x, &y);
        if (exit_touch_tracking)
            exit_touch_inside = exit_button_contains(x, y);
        else
            active_touch_key = key_at(x, y);
    }
}

static void clear_input_after_dialog(void)
{
    bda_gui_raw_event_t ignored;
    for (int count = 0; count < 16; ++count) {
        if (bda_gui_raw_event_fetch(&ignored) < 0) break;
    }
    bbk_release_all_keys();
    memset(applied_keys, 0, sizeof(applied_keys));
    touch_down = 0;
    touch_release_pending = 0;
    active_touch_key = -1;
    exit_touch_tracking = 0;
    exit_touch_inside = 0;
}

static void apply_inputs(int *exit_requested)
{
    bool desired[8][8];
    bda_gui_input_packet_t packet;
    memset(desired, 0, sizeof(desired));
    poll_touch();
    if (active_touch_key >= 0) {
        const KeyDefinition &key = keys[active_touch_key];
        desired[key.row][key.column] = true;
    }

    (void)bda_gui_input_packet(&packet);
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_UP)) desired[2][3] = true;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_DOWN)) desired[3][3] = true;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_LEFT)) desired[7][7] = true;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_RIGHT)) desired[7][3] = true;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_ENTER)) desired[5][3] = true;

    int escape = bda_gui_input_packet_key_pressed(&packet, BDA_KEY_ESCAPE);
    if (escape) {
        uint32_t now = bda_gui_tick_count_25ms();
        if (!escape_down) { escape_down = 1; escape_start_tick = now; }
        if (bda_gui_tick_elapsed_25ms(escape_start_tick, now) >= escape_hold_ticks &&
            !*exit_requested) {
            bbk_diag_log_printf("EXIT_REQUEST source=long_escape");
            *exit_requested = 1;
        }
        else desired[3][7] = true;
    } else escape_down = 0;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            if (desired[row][column] != applied_keys[row][column]) {
                bbk_set_matrix_key(row, column, desired[row][column]);
                applied_keys[row][column] = desired[row][column];
            }
        }
    }
    if (touch_release_pending) {
        active_touch_key = -1;
        touch_release_pending = 0;
    }
    if (exit_click_pending) {
        exit_click_pending = 0;
        bbk_release_all_keys();
        memset(applied_keys, 0, sizeof(applied_keys));
        bbk_diag_log_set_phase("exit-dialog");
        bbk_diag_log_printf("EXIT_DIALOG_OPEN");
        int result = bda_confirm("NC2000", "Exit NC2000?");
        clear_input_after_dialog();
        bbk_diag_log_printf("EXIT_DIALOG_RESULT result=%d", result);
        if (result == BDA_DIALOG_RESULT_YES) {
            bbk_diag_log_printf("EXIT_REQUEST source=dialog_yes");
            *exit_requested = 1;
        } else {
            bbk_diag_log_set_phase("run");
            rendered_touch_key = -2;
            rendered_exit_pressed = -1;
            render_frame(1);
        }
    }
}

static int app_window_proc(bda_handle_t handle, u32 message, u32 wparam, u32 lparam)
{
    if (message == BDA_MSG_DRAW_CONTEXT_DETACH) detached = 1;
    return bda_gui_default_proc(handle, message, wparam, lparam);
}

static int open_window()
{
    bda_frame_desc_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.title = "NC2000";
    descriptor.wndproc = app_window_proc;
    descriptor.height = screen_width;
    descriptor.width = screen_height;
    frame = bda_gui_register_frame_desc(&descriptor);
    if (!frame || (s32)frame == -1) return 0;
    (void)bda_gui_frame_activate(frame, 0x100u);
    if (bda_gui_framebuffer_acquire(&framebuffer) != 0) return 0;
    return 1;
}

static void close_window()
{
    bda_gui_message_t message;
    memset(&message, 0, sizeof(message));
    if (!frame) return;
    (void)bda_gui_frame_stop(frame);
    (void)bda_gui_frame_release(frame);
    for (int count = 0; !detached && count < close_pump_limit; ++count) {
        if (!bda_gui_event_pump_frame_once(&message, frame)) break;
        bda_sys_delay(1u);
    }
    bda_gui_close_frame(frame);
    frame = 0;
}

static long file_size(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    size = ftell(file);
    fclose(file);
    return size;
}

static int rom_set_is_valid(const char *nand_path)
{
    size_t length = strlen(nand_path);
    if (length < 5u || strcmp(nand_path + length - 5u, ".nand") != 0 ||
        length + 1u >= 384u) return 0;

    char companion[384];
    size_t base_length = length - 5u;
    memcpy(companion, nand_path, base_length);
    memcpy(companion + base_length, ".nand0", 7u);
    if (file_size(nand_path) < 65536l * 528l || file_size(companion) <= 0)
        return 0;
    memcpy(companion + base_length, ".nor", 5u);
    return file_size(companion) >= 512l * 1024l;
}

static int configure_rom_paths()
{
    bda_file_selector_t selector;
    const char *nand_path = "B:\\NC2000\\35.nand";
    if (!rom_set_is_valid(nand_path)) nand_path = "A:\\NC2000\\35.nand";
    if (!rom_set_is_valid(nand_path)) {
        memset(&selector, 0, sizeof(selector));
        int selected = bda_gui_select_file(&selector, "A:\\NC2000\\", "nand",
                                           "Select NC2000 NAND");
        if (selected != BDA_FILE_SELECTOR_SELECTED) return 0;
        nand_path = selector.path;
    }
    size_t length = strlen(nand_path);
    if (length <= 5u) return 0;
    char base[384];
    strncpy(base, nand_path, sizeof(base) - 1u);
    base[sizeof(base) - 1u] = 0;
    length = strlen(base);
    if (length >= 5u && base[length - 5u] == '.') base[length - 5u] = 0;

    nc2k_rom.nandFlashPath = nand_path;
    nc2k_rom.nand0Path = base;
    nc2k_rom.nand0Path += ".nand0";
    nc2k_rom.norFlashPath = base;
    nc2k_rom.norFlashPath += ".nor";
    nc2k_rom.statesPath = base;
    nc2k_rom.statesPath += ".state";

    if (file_size(nc2k_rom.nandFlashPath.c_str()) < 65536l * 528l ||
        file_size(nc2k_rom.nand0Path.c_str()) <= 0 ||
        file_size(nc2k_rom.norFlashPath.c_str()) < 512l * 1024l) {
        bda_msgbox("NC2000", "Need matching .nand, .nand0 and .nor files");
        return 0;
    }
    return 1;
}

static void configure_emulator()
{
    nc1020mode = false;
    nc2000mode = true;
    nc3000mode = false;
    pc1000mode = false;
    nc1020tw_mode = false;
    cpu_version = CPU_HANDYPSP;
    cpu_loop_version = CPU_RUN3;
    io_version = IO_V2;
    enable_load_state = false;
    save_flash_on_exit = true;
    save_state_on_exit = false;
    enable_auto_time_sync = false;
    enable_keepon = true;
    // UI/input still runs every 4 ms; this only amortizes CPU/JIT and timer
    // bookkeeping.  2048 guest cycles is 0.4 ms on an NC2000.
    cpu_batch = 2048;
    init_parameters();
}

extern "C" __attribute__((section(".text.bda_main"))) int bda_main(void)
{
    int exit_requested = 0;
    uint32_t next_slice;
    uint32_t next_render;
    uint32_t next_diagnostic;

    bbk_diag_log_initialize();
    bbk_diag_log_printf("BDA_START");
    bbk_diag_log_set_phase("rom-config");
    if (!configure_rom_paths()) {
        bbk_diag_log_printf("ROM_CONFIG_CANCELLED");
        bbk_diag_log_shutdown();
        return 0;
    }
    bbk_diag_log_printf("ROM_PATHS nand=%s nand0=%s nor=%s",
                        nc2k_rom.nandFlashPath.c_str(),
                        nc2k_rom.nand0Path.c_str(),
                        nc2k_rom.norFlashPath.c_str());
    configure_emulator();
    bbk_diag_log_printf("EMULATOR_CONFIG cpu_batch=%u", cpu_batch);
    memset(frame_pixels, 0, sizeof(frame_pixels));
    memset(applied_keys, 0, sizeof(applied_keys));
    detached = 0;
    active_touch_key = -1;
    touch_down = 0;
    touch_release_pending = 0;
    exit_touch_tracking = 0;
    exit_touch_inside = 0;
    exit_click_pending = 0;
    escape_down = 0;
    rendered_touch_key = -2;
    rendered_exit_pressed = -1;
    lcd_initialized = 0;
    rendered_grey_mode = 0;
    bbk_release_all_keys();
    bbk_diag_log_set_phase("window-open");
    if (!open_window()) {
        bbk_diag_log_printf("WINDOW_OPEN_FAILED");
        bda_msgbox("NC2000", "Framebuffer or window initialization failed");
        close_window();
        bbk_diag_log_shutdown();
        return 0;
    }
    bbk_diag_log_printf("WINDOW_OPEN_DONE");

    bbk_diag_log_set_phase("audio-init");
    bbk_diag_log_printf("AUDIO_INIT_BEGIN");
    init_audio();
    bbk_diag_log_printf("AUDIO_INIT_DONE");
    bbk_diag_log_set_phase("load");
    bbk_diag_log_printf("LOAD_BEGIN");
    LoadNC2k();
    bbk_diag_log_printf("LOAD_DONE pc=%04X", (unsigned)mPC & 0xffffu);
    bda_gui_millisecond_timer_start();
    next_slice = bda_gui_millisecond_count();
    next_render = next_slice;
    next_diagnostic = next_slice + diagnostic_interval_ms;
    bbk_diag_log_set_phase("run");
    bbk_diag_log_heartbeat(next_slice);
    render_frame(1);

    while (!exit_requested && !detached) {
        RunTimeSlice(4u);
        apply_inputs(&exit_requested);
        bbk_audio_service();
        uint32_t now = bda_gui_millisecond_count();
        if ((int32_t)(now - next_diagnostic) >= 0 &&
            !bbk_audio_speech_active()) {
            bbk_diag_log_heartbeat(now);
            next_diagnostic += diagnostic_interval_ms;
            if ((int32_t)(now - next_diagnostic) > (int32_t)diagnostic_interval_ms)
                next_diagnostic = now + diagnostic_interval_ms;
        }
        if ((int32_t)(now - next_render) >= 0) {
            render_frame(0);
            next_render += render_interval_ms;
            if ((int32_t)(now - next_render) > 150)
                next_render = now + render_interval_ms;
        }
        next_slice += 4u;
        while ((int32_t)(bda_gui_millisecond_count() - next_slice) < 0)
            bda_sys_delay(1u);
        now = bda_gui_millisecond_count();
        if ((int32_t)(now - next_slice) > 100) next_slice = now;
    }

    bbk_diag_log_set_phase("exit");
    bbk_diag_log_printf("RUN_LOOP_END requested=%d detached=%d",
                        exit_requested, detached);
    bbk_release_all_keys();
    bda_gui_millisecond_timer_stop();
    bbk_diag_log_set_phase("save");
    bbk_diag_log_printf("SAVE_BEGIN");
    SaveNC2kIfNeed();
    bbk_diag_log_printf("SAVE_DONE");
    bbk_diag_log_set_phase("audio-stop");
    bbk_diag_log_printf("AUDIO_SHUTDOWN_BEGIN");
    shutdown_audio();
    bbk_diag_log_printf("AUDIO_SHUTDOWN_DONE");
    bbk_diag_log_set_phase("jit-stop");
    bbk_diag_log_printf("JIT_SHUTDOWN_BEGIN compiled=%u calls=%u fallback=%u resets=%u replacements=%u cache_fallback=%u",
                        (unsigned)bbk_jit_blocks_compiled,
                        (unsigned)bbk_jit_block_calls,
                        (unsigned)bbk_jit_fallback_ops,
                        (unsigned)bbk_jit_cache_resets,
                        (unsigned)bbk_jit_block_replacements,
                        (unsigned)bbk_jit_cache_fallback_blocks);
    bbk_jit_shutdown();
    bbk_diag_log_printf("JIT_SHUTDOWN_DONE");
    bbk_diag_log_set_phase("window-close");
    bbk_diag_log_printf("WINDOW_CLOSE_BEGIN");
    close_window();
    bbk_diag_log_printf("WINDOW_CLOSE_DONE");
    bbk_diag_log_shutdown();
    return 0;
}
