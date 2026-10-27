#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <dongle_display/status.h>

#include "wpm_status.h"

LV_IMG_DECLARE(sym_speedometer);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct wpm_status_state
{
    int wpm;
    char layer[DD_LAYER_NAME_LENGTH + 1];
};

static struct wpm_status_state get_state(const zmk_event_t *_eh)
{
    struct dd_widgets widgets = dd_status_get().widgets;
    struct wpm_status_state state = {.wpm = widgets.wpm};
    memcpy(state.layer, widgets.layer_name, sizeof(state.layer));
    return state;
}

static void set_wpm(struct zmk_widget_wpm_status *widget, struct wpm_status_state state)
{
    // NULL check for layer name before strstr to prevent crash
    if (state.layer[0] != '\0' &&
        strstr(CONFIG_ZMK_DONGLE_DISPLAY_WPM_DISABLED_LAYERS, state.layer) != NULL) {
        lv_label_set_text(widget->wpm_label, "-");
        return;
    }

    char wpm_text[12];
    snprintf(wpm_text, sizeof(wpm_text), "%i", state.wpm);
    lv_label_set_text(widget->wpm_label, wpm_text);
}

static void wpm_status_update_cb(struct wpm_status_state state)
{
    struct zmk_widget_wpm_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        set_wpm(widget, state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state,
                            wpm_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, dd_status_changed);

int zmk_widget_wpm_status_init(struct zmk_widget_wpm_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t *speedometer = lv_img_create(widget->obj);
    lv_obj_align(speedometer, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_img_set_src(speedometer, &sym_speedometer);

    widget->wpm_label = lv_label_create(widget->obj);
    lv_obj_align_to(widget->wpm_label, speedometer, LV_ALIGN_OUT_RIGHT_MID, 2, 1);

    sys_slist_append(&widgets, &widget->node);

    widget_wpm_status_init();
    return 0;
}

lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget)
{
    return widget->obj;
}
