/* SPDX-License-Identifier: MIT */
#include <lvgl.h>
#include <zephyr/devicetree.h>
#include <zmk/display.h>
#include <dongle_display/status.h>

#define ROW_HEIGHT 8
#define SCREEN_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define BODY_ROWS (SCREEN_HEIGHT / ROW_HEIGHT - 1)
BUILD_ASSERT(SCREEN_WIDTH >= 128 && SCREEN_HEIGHT >= 32, "Status screen needs at least 128x32");

static lv_obj_t *rows[BODY_ROWS + 1];
static uint8_t page;

static void render(struct dd_status state) {
    char text[32];
    dd_status_format_host(&state, text, sizeof(text));
    lv_label_set_text(rows[0], text);
    unsigned pages = (state.peripheral_count + 1 + BODY_ROWS - 1) / BODY_ROWS;
    page %= pages ? pages : 1;
    for (int row = 0; row < BODY_ROWS; row++) {
        unsigned node = page * BODY_ROWS + row;
        if (state.local_node == DD_NO_LOCAL_NODE && row == 0) {
            dd_status_format_node(&state, DD_NO_LOCAL_NODE, text, sizeof(text));
        } else if (node <= state.peripheral_count) {
            dd_status_format_node(&state, node, text, sizeof(text));
        } else {
            text[0] = '\0';
        }
        lv_label_set_text(rows[row + 1], text);
    }
}

static struct dd_status get_state(const zmk_event_t *eh) { return dd_status_get(); }
ZMK_DISPLAY_WIDGET_LISTENER(dd_screen, struct dd_status, render, get_state)
ZMK_SUBSCRIPTION(dd_screen, dd_status_changed);

#if CONFIG_ZMK_DONGLE_DISPLAY_MAX_PERIPHERALS + 1 > BODY_ROWS
static void change_page(lv_timer_t *timer) {
    page++;
    render(dd_status_get());
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    for (int row = 0; row <= BODY_ROWS; row++) {
        rows[row] = lv_label_create(screen);
        lv_obj_remove_style_all(rows[row]);
        lv_obj_set_style_text_font(rows[row], &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(rows[row], lv_color_white(), 0);
        lv_obj_set_style_text_letter_space(rows[row], 0, 0);
        lv_obj_set_pos(rows[row], 0, row * ROW_HEIGHT);
        lv_obj_set_width(rows[row], SCREEN_WIDTH);
        lv_label_set_long_mode(rows[row], LV_LABEL_LONG_CLIP);
    }
    dd_screen_init();
#if CONFIG_ZMK_DONGLE_DISPLAY_MAX_PERIPHERALS + 1 > BODY_ROWS
    lv_timer_create(change_page, 5000, NULL);
#endif
    return screen;
}
