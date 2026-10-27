/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DD_MAX_PERIPHERALS
#define DD_MAX_PERIPHERALS CONFIG_ZMK_DONGLE_DISPLAY_MAX_PERIPHERALS
#endif

#define DD_NO_LOCAL_NODE UINT8_MAX
#define DD_NODE_CONNECTED (1u << 0)
#define DD_NODE_BATTERY (1u << 1)
#define DD_NODE_KNOWN (1u << 2)
#define DD_NODE_USB (1u << 3)
#define DD_HOST_USB_READY (1u << 0)
#define DD_HOST_BLE_CONNECTED (1u << 1)
#define DD_HOST_BLE_BONDED (1u << 2)
#define DD_HOST_SELECTED_USB (1u << 3)
#define DD_HOST_SELECTED_BLE (1u << 4)
#define DD_WIDGET_PROTOCOL_VERSION 2u
#define DD_LAYER_NAME_LENGTH 12
#define DD_WIDGET_PACKETS 4

struct dd_widgets {
    uint8_t layer;
    uint8_t modifiers;
    uint8_t indicators;
    uint8_t wpm;
    char layer_name[DD_LAYER_NAME_LENGTH + 1];
};

/* A complete frame is committed atomically. Lost/mixed chunks never expose a
 * partially updated layer label to the display queue. */
struct dd_widget_rx {
    struct dd_widgets pending;
    uint8_t sequence;
    uint8_t received;
    uint8_t name_length;
};

struct dd_node {
    uint8_t flags;
    uint8_t battery;
};

struct dd_status {
    struct dd_widgets widgets;
    struct dd_node nodes[DD_MAX_PERIPHERALS + 1]; /* central, then split source IDs + 1 */
    struct dd_node local; /* usable even before the central assigns a source ID */
    uint8_t peripheral_count;
    uint8_t local_node;
    uint8_t host_flags;
    uint8_t profile; /* zero-based */
    bool live;
};

/* Each packet updates one node and host status. Numeric parameters, not native
 * struct bytes, go through ZMK's existing encrypted run-behavior transport. */
void dd_status_encode(const struct dd_status *status, uint8_t node, uint8_t recipient,
                      uint32_t *param1, uint32_t *param2);
bool dd_status_decode(struct dd_status *status, uint32_t param1, uint32_t param2);
void dd_status_disconnect(struct dd_status *status);
void dd_status_format_host(const struct dd_status *status, char *text, size_t size);
void dd_status_format_node(const struct dd_status *status, uint8_t node, char *text, size_t size);
void dd_widgets_encode(const struct dd_widgets *widgets, uint8_t sequence, uint8_t packet,
                       uint32_t *param1, uint32_t *param2);
bool dd_widgets_decode(struct dd_widgets *widgets, struct dd_widget_rx *rx,
                       uint32_t param1, uint32_t param2);
