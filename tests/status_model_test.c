/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dongle_display/model.h>

static struct dd_status fixture(void) {
    struct dd_status status = {
        .peripheral_count = DD_MAX_PERIPHERALS,
        .local_node = 0,
        .live = true,
        .host_flags = DD_HOST_USB_READY | DD_HOST_SELECTED_USB | DD_HOST_BLE_BONDED,
        .profile = 4,
        .nodes = {
            {.flags = DD_NODE_CONNECTED | DD_NODE_USB},
            {.flags = DD_NODE_CONNECTED | DD_NODE_BATTERY | DD_NODE_KNOWN, .battery = 75},
            {.flags = DD_NODE_CONNECTED | DD_NODE_BATTERY | DD_NODE_KNOWN, .battery = 0},
        },
    };
    for (int node = 3; node <= DD_MAX_PERIPHERALS; node++) {
        status.nodes[node] = (struct dd_node){
            .flags = DD_NODE_CONNECTED | DD_NODE_BATTERY | DD_NODE_KNOWN,
            .battery = node * 7 % 101,
        };
    }
    return status;
}

static void round_trip(void) {
    struct dd_status sender = fixture();
    struct dd_status receiver = {.local_node = DD_NO_LOCAL_NODE};
    uint32_t p1, p2;
    for (int node = 0; node <= DD_MAX_PERIPHERALS; node++) {
        dd_status_encode(&sender, node, 1, &p1, &p2);
        assert(dd_status_decode(&receiver, p1, p2));
        assert(memcmp(&sender.nodes[node], &receiver.nodes[node], sizeof(struct dd_node)) == 0);
    }
    assert(receiver.local_node == 1 && receiver.live);
    assert(receiver.host_flags == sender.host_flags && receiver.profile == 4);
    assert(receiver.peripheral_count == DD_MAX_PERIPHERALS);
}

static void malformed_packets_do_not_mutate_state(void) {
    struct dd_status sender = fixture();
    struct dd_status original = {.local_node = DD_NO_LOCAL_NODE};
    uint32_t p1, p2;
    dd_status_encode(&sender, 1, 1, &p1, &p2);
    assert(p1 == (0x01000101u | (DD_MAX_PERIPHERALS << 16)));
    assert(p2 == 0x0d044b07u); /* host flags, profile 5, 75%, connected/known battery */
    uint32_t bad_headers[] = {
        p1 ^ (3u << 24), /* incompatible version */
        (p1 & ~0xff0000u) | ((DD_MAX_PERIPHERALS + 1u) << 16),
        p1 & ~0xff0000u, /* zero count */
        (p1 & ~0xffu) | 255u, /* node out of bounds */
        p1 & ~0xff00u, /* central cannot be a recipient */
        (p1 & ~0xff00u) | (255u << 8),
    };
    for (size_t i = 0; i < sizeof(bad_headers) / sizeof(bad_headers[0]); i++) {
        struct dd_status state = original;
        assert(!dd_status_decode(&state, bad_headers[i], p2));
        assert(memcmp(&state, &original, sizeof(state)) == 0);
    }
    uint32_t bad_payloads[] = {p2 | 0x80u, p2 | (0x80u << 24),
                               (p2 & ~0xff00u) | (101u << 8), p2 & ~DD_NODE_BATTERY};
    for (size_t i = 0; i < sizeof(bad_payloads) / sizeof(bad_payloads[0]); i++) {
        struct dd_status state = original;
        assert(!dd_status_decode(&state, p1, bad_payloads[i]));
        assert(memcmp(&state, &original, sizeof(state)) == 0);
    }
}

static void text_and_disconnect(void) {
    struct dd_status status = fixture();
    char text[32];
    dd_status_format_host(&status, text, sizeof(text));
    assert(strcmp(text, ">USB+  BT5-") == 0);
    dd_status_format_node(&status, 0, text, sizeof(text));
    assert(strcmp(text, "C+ USB") == 0); /* no fictitious dongle battery */
    dd_status_format_node(&status, 2, text, sizeof(text));
    assert(strcmp(text, "P2+ 0%") == 0); /* zero is NOT disconnected */
    status.nodes[2].flags &= ~DD_NODE_KNOWN;
    dd_status_format_node(&status, 2, text, sizeof(text));
    assert(strcmp(text, "P2+ ?") == 0);
    status.local_node = 1;
    dd_status_disconnect(&status);
    assert(!status.live && status.host_flags == 0);
    assert(status.nodes[1].battery == 75);
    assert(!(status.nodes[1].flags & DD_NODE_CONNECTED));
    dd_status_format_node(&status, 1, text, sizeof(text));
    assert(strcmp(text, "P1- 75%") == 0); /* local battery survives link loss */
    dd_status_format_host(&status, text, sizeof(text));
    assert(strcmp(text, "Central offline") == 0);
    dd_status_format_node(&status, 2, text, sizeof(text));
    assert(strcmp(text, "P2- --") == 0);
    status.nodes[0] = (struct dd_node){
        .flags = DD_NODE_CONNECTED | DD_NODE_BATTERY | DD_NODE_KNOWN | DD_NODE_USB, .battery = 100};
    dd_status_format_node(&status, 0, text, sizeof(text));
    assert(strcmp(text, "C+ 100% USB") == 0);
    char tiny[2];
    dd_status_format_node(&status, 0, tiny, sizeof(tiny));
    assert(tiny[1] == '\0');
}

static void reconnect_and_all_sources(void) {
    struct dd_status sender = fixture();
    struct dd_status receiver = {.local_node = DD_NO_LOCAL_NODE};
    uint32_t p1, p2;
    for (int source = 1; source <= DD_MAX_PERIPHERALS; source++) {
        for (int node = 0; node <= DD_MAX_PERIPHERALS; node++) {
            dd_status_encode(&sender, node, source, &p1, &p2);
            assert(dd_status_decode(&receiver, p1, p2));
        }
        assert(receiver.local_node == source);
        dd_status_disconnect(&receiver);
        dd_status_encode(&sender, 0, source, &p1, &p2);
        assert(dd_status_decode(&receiver, p1, p2));
        assert(receiver.live);
    }
}

static void local_sampling_wins_over_relayed_copy(void) {
    struct dd_status sender = fixture();
    struct dd_status receiver = {
        .local_node = DD_NO_LOCAL_NODE,
        .local = {.flags = DD_NODE_CONNECTED | DD_NODE_BATTERY | DD_NODE_KNOWN | DD_NODE_USB,
                  .battery = 99},
    };
    uint32_t p1, p2;
    dd_status_encode(&sender, 1, 1, &p1, &p2);
    assert(dd_status_decode(&receiver, p1, p2));
    assert(receiver.nodes[1].battery == 99);
    assert(receiver.nodes[1].flags & DD_NODE_USB);
    dd_status_encode(&sender, 2, 1, &p1, &p2);
    assert(dd_status_decode(&receiver, p1, p2));
    assert(receiver.nodes[2].battery == 0);
    assert(receiver.nodes[1].battery == 99);
}

int main(void) {
    round_trip();
    malformed_packets_do_not_mutate_state();
    text_and_disconnect();
    reconnect_and_all_sources();
    local_sampling_wins_over_relayed_copy();
    puts("PASS status protocol, all sources, reconnect, invalid data, battery and host text");
}
