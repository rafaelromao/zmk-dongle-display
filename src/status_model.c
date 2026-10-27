/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <dongle_display/model.h>

#define DD_PROTOCOL_VERSION 1u

void dd_status_encode(const struct dd_status *status, uint8_t node, uint8_t recipient,
                      uint32_t *param1, uint32_t *param2) {
    *param1 = (DD_PROTOCOL_VERSION << 24) | ((uint32_t)status->peripheral_count << 16) |
              ((uint32_t)recipient << 8) | node;
    *param2 = ((uint32_t)status->host_flags << 24) | ((uint32_t)status->profile << 16) |
              ((uint32_t)status->nodes[node].battery << 8) | status->nodes[node].flags;
}

bool dd_status_decode(struct dd_status *status, uint32_t param1, uint32_t param2) {
    uint8_t version = param1 >> 24;
    uint8_t count = param1 >> 16;
    uint8_t recipient = param1 >> 8;
    uint8_t node = param1;
    uint8_t flags = param2;
    uint8_t battery = param2 >> 8;
    uint8_t host_flags = param2 >> 24;
    if (version != DD_PROTOCOL_VERSION || count > DD_MAX_PERIPHERALS || count == 0 ||
        node > count || recipient == 0 || recipient > count || (flags & ~0x0fu) ||
        (host_flags & ~0x1fu) || ((flags & DD_NODE_KNOWN) && battery > 100) ||
        ((flags & DD_NODE_KNOWN) && !(flags & DD_NODE_BATTERY))) {
        return false;
    }
    if (status->peripheral_count != count || status->local_node != recipient) {
        memset(status->nodes, 0, sizeof(status->nodes));
    }
    status->peripheral_count = count;
    status->local_node = recipient;
    status->nodes[node] = (struct dd_node){.flags = flags, .battery = battery};
    /* Local sampling is authoritative (including USB power). BAS cannot send
     * charging state, and a relayed copy of our own charge may already be old. */
    if (status->local.flags) {
        status->nodes[recipient] = status->local;
    }
    status->host_flags = host_flags;
    status->profile = param2 >> 16;
    status->live = true;
    return true;
}

void dd_status_disconnect(struct dd_status *status) {
    struct dd_node local = {0};
    if (status->local_node <= status->peripheral_count) {
        local = status->nodes[status->local_node];
        local.flags &= ~DD_NODE_CONNECTED;
    }
    memset(status->nodes, 0, sizeof(status->nodes));
    if (status->local_node <= status->peripheral_count) {
        status->nodes[status->local_node] = local;
    }
    status->host_flags = 0;
    memset(&status->widgets, 0, sizeof(status->widgets));
    status->live = false;
}

void dd_widgets_encode(const struct dd_widgets *widgets, uint8_t sequence, uint8_t packet,
                       uint32_t *param1, uint32_t *param2) {
    *param1 = (DD_WIDGET_PROTOCOL_VERSION << 24) | ((uint32_t)packet << 16) |
              ((uint32_t)sequence << 8);
    *param2 = 0;
    if (packet == 0) {
        uint8_t length = 0;
        while (length < DD_LAYER_NAME_LENGTH && widgets->layer_name[length]) {
            length++;
        }
        *param1 |= widgets->layer;
        *param2 = widgets->modifiers | ((uint32_t)widgets->wpm << 8) |
                  ((uint32_t)widgets->indicators << 16) | ((uint32_t)length << 24);
    } else if (packet < DD_WIDGET_PACKETS) {
        for (int i = 0; i < 4; i++) {
            *param2 |= (uint32_t)(uint8_t)widgets->layer_name[(packet - 1) * 4 + i] << (i * 8);
        }
    }
}

bool dd_widgets_decode(struct dd_widgets *widgets, struct dd_widget_rx *rx,
                       uint32_t param1, uint32_t param2) {
    uint8_t packet = param1 >> 16;
    uint8_t sequence = param1 >> 8;
    if ((param1 >> 24) != DD_WIDGET_PROTOCOL_VERSION || packet >= DD_WIDGET_PACKETS) {
        return false;
    }
    if (packet == 0) {
        uint8_t length = param2 >> 24;
        if (length > DD_LAYER_NAME_LENGTH) {
            return false;
        }
        *rx = (struct dd_widget_rx){
            .pending = {.layer = param1, .modifiers = param2, .wpm = param2 >> 8,
                        .indicators = param2 >> 16},
            .sequence = sequence, .received = 1, .name_length = length,
        };
    } else {
        if (!rx->received || sequence != rx->sequence || (param1 & 0xff)) {
            return false;
        }
        for (int i = 0; i < 4; i++) {
            rx->pending.layer_name[(packet - 1) * 4 + i] = param2 >> (i * 8);
        }
        rx->received |= 1u << packet;
    }
    if (rx->received == (1u << DD_WIDGET_PACKETS) - 1) {
        rx->pending.layer_name[rx->name_length] = '\0';
        *widgets = rx->pending;
        rx->received = 0;
    }
    return true;
}

void dd_status_format_host(const struct dd_status *status, char *text, size_t size) {
    if (!status->live) {
        snprintf(text, size, "Central offline");
        return;
    }
    snprintf(text, size, "%cUSB%c %cBT%u%c",
             status->host_flags & DD_HOST_SELECTED_USB ? '>' : ' ',
             status->host_flags & DD_HOST_USB_READY ? '+' : '-',
             status->host_flags & DD_HOST_SELECTED_BLE ? '>' : ' ', status->profile + 1,
             status->host_flags & DD_HOST_BLE_CONNECTED ? '+' :
             (status->host_flags & DD_HOST_BLE_BONDED ? '-' : '?'));
}

void dd_status_format_node(const struct dd_status *status, uint8_t node, char *text, size_t size) {
    if (node != DD_NO_LOCAL_NODE && node > status->peripheral_count) {
        snprintf(text, size, "--");
        return;
    }
    struct dd_node value = node == DD_NO_LOCAL_NODE ? status->local : status->nodes[node];
    char name[6];
    if (node == DD_NO_LOCAL_NODE) {
        snprintf(name, sizeof(name), "Local");
    } else if (node == 0) {
        snprintf(name, sizeof(name), "C");
    } else {
        snprintf(name, sizeof(name), "P%u", node);
    }
    char battery[6];
    bool is_local = node == status->local_node || node == DD_NO_LOCAL_NODE;
    if (!(value.flags & DD_NODE_CONNECTED) && !is_local) {
        snprintf(battery, sizeof(battery), "--");
    } else if (!(value.flags & DD_NODE_BATTERY)) {
        snprintf(battery, sizeof(battery), "%s", value.flags & DD_NODE_USB ? "USB" : "n/a");
    } else if (!(value.flags & DD_NODE_KNOWN)) {
        snprintf(battery, sizeof(battery), "?");
    } else {
        snprintf(battery, sizeof(battery), "%u%%", value.battery);
    }
    snprintf(text, size, "%s%c %s%s", name, value.flags & DD_NODE_CONNECTED ? '+' : '-',
             battery, value.flags & DD_NODE_USB && value.flags & DD_NODE_BATTERY ? " USB" : "");
}
