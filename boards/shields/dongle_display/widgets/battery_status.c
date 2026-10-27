/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"
#include <dongle_display/status.h>

#define BATTERY_ROWS MIN(DD_MAX_PERIPHERALS + 1, DT_PROP(DT_CHOSEN(zephyr_display), height) / 10)

#define BUFFER_SIZE LV_CANVAS_BUF_SIZE(5, 8, LV_COLOR_FORMAT_GET_BPP(LV_COLOR_FORMAT_L8), LV_DRAW_BUF_STRIDE_ALIGN)

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
} battery_objects[BATTERY_ROWS];
    
static lv_color_t battery_image_buffer[BATTERY_ROWS][BUFFER_SIZE];
static uint8_t page;

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    if (usb_present) {
        rect_fill_dsc.bg_opa = LV_OPA_TRANSP;
        rect_fill_dsc.border_color = lv_color_white();
        rect_fill_dsc.border_width = 1;
    }

    lv_canvas_set_px(canvas, 0, 0, lv_color_white(), LV_OPA_COVER);
    lv_canvas_set_px(canvas, 4, 0, lv_color_white(), LV_OPA_COVER);

    lv_area_t rect_coords;
    bool rect_draw = true;
    
    if (level <= 10 || usb_present) {
        rect_coords = (lv_area_t){1, 2, 3, 6};
    } else if (level <= 30) {
        rect_coords = (lv_area_t){1, 2, 3, 5};
    } else if (level <= 50) {
        rect_coords = (lv_area_t){1, 2, 3, 4};
    } else if (level <= 70) {
        rect_coords = (lv_area_t){1, 2, 3, 3};
    } else if (level <= 90) {
        rect_coords = (lv_area_t){1, 2, 3, 2};
    } else {
        rect_draw = false;
    }

    if (rect_draw) {
        lv_draw_rect(&layer, &rect_fill_dsc, &rect_coords);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void battery_status_update_cb(struct dd_status state) {
    static struct dd_node previous[BATTERY_ROWS];
    static bool visible[BATTERY_ROWS], was_local[BATTERY_ROWS];
    uint8_t sources[DD_MAX_PERIPHERALS + 1];
    unsigned count = 0;
    /* Keep the original order/positions: optional central battery, then halves
     * in split-source order. A USB-only central has no battery row. */
    if (state.nodes[0].flags & DD_NODE_BATTERY) {
        sources[count++] = 0;
    }
    if (state.local_node == DD_NO_LOCAL_NODE && (state.local.flags & DD_NODE_BATTERY)) {
        sources[count++] = DD_NO_LOCAL_NODE;
    } else {
        for (int node = 1; node <= state.peripheral_count; node++) {
            sources[count++] = node;
        }
    }
    unsigned pages = (count + BATTERY_ROWS - 1) / BATTERY_ROWS;
    page %= pages ? pages : 1;
    for (unsigned row = 0; row < BATTERY_ROWS; row++) {
        lv_obj_t *symbol = battery_objects[row].symbol;
        lv_obj_t *label = battery_objects[row].label;
        unsigned index = page * BATTERY_ROWS + row;
        if (index >= count) {
            lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            visible[row] = false;
            continue;
        }
        uint8_t source = sources[index];
        struct dd_node node = source == DD_NO_LOCAL_NODE ? state.local : state.nodes[source];
        bool known = node.flags & DD_NODE_KNOWN;
        bool connected = node.flags & DD_NODE_CONNECTED;
        /* The local half still has a valid battery after the split drops. */
        bool local = source == DD_NO_LOCAL_NODE || source == state.local_node;
        if (visible[row] && was_local[row] == local &&
            !memcmp(&previous[row], &node, sizeof(node))) {
            continue;
        }
        previous[row] = node;
        was_local[row] = local;
        visible[row] = true;
        draw_battery(symbol, known ? node.battery : 0, node.flags & DD_NODE_USB);
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        if (!connected && !local) {
            lv_label_set_text(label, "  --  ");
        } else if (!known) {
            lv_label_set_text(label, "   ?  ");
        } else {
            lv_label_set_text_fmt(label, "%4u%% ", node.battery);
        }
    }
}

static struct dd_status battery_status_get_state(const zmk_event_t *eh) {
    return dd_status_get();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct dd_status,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, dd_status_changed);

static void change_page(lv_timer_t *timer) {
    page++;
    battery_status_update_cb(dd_status_get());
}

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    for (int i = 0; i < BATTERY_ROWS; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], 5, 8, LV_COLOR_FORMAT_L8);

        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * 10);
        lv_obj_align_to(battery_label, image_canvas, LV_ALIGN_OUT_LEFT_MID, 0, 0);

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        
        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();
    lv_timer_create(change_page, 5000, NULL);

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
