/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_dongle_display_status

#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <dongle_display/status.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/usb.h>
#include <zmk/events/battery_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define DD_PERIPHERAL 1
#include <zmk/split/bluetooth/peripheral.h>
#include <drivers/behavior.h>
#else
#define DD_PERIPHERAL 0
#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT <= DD_MAX_PERIPHERALS,
             "Increase ZMK_DONGLE_DISPLAY_MAX_PERIPHERALS on central and display");
#endif
#endif

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC)
#include <zmk/split/bluetooth/service.h>
BUILD_ASSERT(sizeof(DEVICE_DT_NAME(DT_NODELABEL(dd_status))) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "Status behavior name must fit ZMK's 9-byte split behavior field");
#endif

ZMK_EVENT_IMPL(dd_status_changed);

static struct k_spinlock lock;
static struct dd_status status = {.local_node = DD_NO_LOCAL_NODE};
#if DD_PERIPHERAL
static int64_t node_received[DD_MAX_PERIPHERALS + 1];
#endif
#if !DD_PERIPHERAL
static bool battery_seen[DD_MAX_PERIPHERALS];
static uint8_t battery_levels[DD_MAX_PERIPHERALS];
#endif

struct dd_status dd_status_get(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct dd_status copy = status;
    k_spin_unlock(&lock, key);
    return copy;
}

static void notify(void) { raise_dd_status_changed((struct dd_status_changed){}); }

#if DD_PERIPHERAL && IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC)
/* Never call LVGL or raise ZMK events from the Bluetooth RX thread. */
static void notify_work_cb(struct k_work *work) { notify(); }
static K_WORK_DEFINE(notify_work, notify_work_cb);

static int receive_status(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct dd_status previous = status;
    bool valid = dd_status_decode(&status, binding->param1, binding->param2);
    bool changed = memcmp(&previous, &status, sizeof(status)) != 0;
    if (valid) {
        node_received[(uint8_t)binding->param1] = k_uptime_get();
    }
    k_spin_unlock(&lock, key);
    if (!valid) {
        return -EINVAL;
    }
    if (changed) {
        k_work_submit(&notify_work);
    }
    return 0;
}

static const struct behavior_driver_api receiver_api = {.binding_pressed = receive_status};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &receiver_api);
#endif

static struct dd_node local_battery(bool central) {
    struct dd_node node = {.flags = DD_NODE_CONNECTED};
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    if (!central || IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)) {
        node.flags |= DD_NODE_BATTERY;
        int level = zmk_battery_state_of_charge();
        if (level >= 0 && level <= 100) {
            node.flags |= DD_NODE_KNOWN;
            node.battery = level;
        }
    }
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    if (zmk_usb_is_powered()) {
        node.flags |= DD_NODE_USB;
    }
#endif
    return node;
}

#if !DD_PERIPHERAL
static struct dd_status collect_status(void) {
    struct dd_status next = {.live = true, .local_node = 0};
    next.nodes[0] = local_battery(true);
    next.local = next.nodes[0];
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    enum zmk_transport transport = selected.transport;
    if (transport == ZMK_TRANSPORT_NONE) {
        transport = zmk_endpoint_get_preferred_transport();
    }
    if (transport == ZMK_TRANSPORT_USB) {
        next.host_flags |= DD_HOST_SELECTED_USB;
    } else if (transport == ZMK_TRANSPORT_BLE) {
        next.host_flags |= DD_HOST_SELECTED_BLE;
    }
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_hid_ready()) {
        next.host_flags |= DD_HOST_USB_READY;
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    next.profile = zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_connected()) {
        next.host_flags |= DD_HOST_BLE_CONNECTED;
    }
    if (!zmk_ble_active_profile_is_open()) {
        next.host_flags |= DD_HOST_BLE_BONDED;
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    next.peripheral_count = ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT;
    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
    /* Query, do not replace the transport's status callback (ZMK owns it). */
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (!transport->api->get_available_source_ids ||
            (transport->api->get_status && !transport->api->get_status().enabled)) {
            continue;
        }
        int count = transport->api->get_available_source_ids(sources);
        for (int i = 0; i < count; i++) {
            if (sources[i] < next.peripheral_count) {
                next.nodes[sources[i] + 1].flags = DD_NODE_CONNECTED | DD_NODE_BATTERY;
            }
        }
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (int i = 0; i < next.peripheral_count; i++) {
        if (!(next.nodes[i + 1].flags & DD_NODE_CONNECTED)) {
            battery_seen[i] = false;
        } else if (battery_seen[i]) {
            next.nodes[i + 1].flags |= DD_NODE_KNOWN;
            next.nodes[i + 1].battery = battery_levels[i];
        }
    }
    k_spin_unlock(&lock, key);
#endif
    return next;
}

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC)
static uint8_t relay_source, relay_node;
static bool relay_active, relay_again;
static void relay_step(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(relay_work, relay_step);

static void relay_step(struct k_work *work) {
    struct dd_status next = dd_status_get();
    while (relay_source < next.peripheral_count &&
           (!(CONFIG_ZMK_DONGLE_DISPLAY_STATUS_RECEIVERS & BIT(relay_source)) ||
            !(next.nodes[relay_source + 1].flags & DD_NODE_CONNECTED))) {
        relay_source++;
        relay_node = 0;
    }
    if (relay_source >= next.peripheral_count) {
        relay_active = false;
        if (relay_again) {
            relay_again = false;
            relay_source = relay_node = 0;
            relay_active = true;
            k_work_reschedule(&relay_work, K_MSEC(20));
        }
        return;
    }
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(dd_status)),
    };
    dd_status_encode(&next, relay_node, relay_source + 1, &binding.param1, &binding.param2);
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};
    int err = zmk_split_central_invoke_behavior(relay_source, &binding, event, true);
    if (err < 0) {
        k_work_reschedule(&relay_work, K_SECONDS(1));
        return;
    }
    if (++relay_node > next.peripheral_count) {
        relay_source++;
        relay_node = 0;
    }
    /* Do not flood ZMK's small behavior queue or evict normal key behaviors.
     * A packet is 20 bytes, fitting the default BLE ATT payload without MTU negotiation. */
    k_work_reschedule(&relay_work, K_MSEC(20));
}

static void relay(void) {
    if (relay_active) {
        relay_again = true;
        return;
    }
    relay_source = relay_node = 0;
    relay_active = true;
    k_work_reschedule(&relay_work, K_NO_WAIT);
}
#endif
#endif

static void poll(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll);

static void poll(struct k_work *work) {
    struct dd_status previous = dd_status_get();
    struct dd_status next;
#if DD_PERIPHERAL
    struct dd_node local = local_battery(false);
    bool connected = zmk_split_bt_peripheral_is_connected();
    if (!connected) {
        local.flags &= ~DD_NODE_CONNECTED;
    }
    int64_t now = k_uptime_get();
    int64_t expiry = 2 * CONFIG_ZMK_DONGLE_DISPLAY_STATUS_REFRESH_SECONDS * MSEC_PER_SEC;
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (!connected || now - node_received[0] > expiry) {
        dd_status_disconnect(&status);
    }
    for (int node = 1; node <= status.peripheral_count; node++) {
        if (now - node_received[node] > expiry) {
            status.nodes[node] = (struct dd_node){0};
        }
    }
    status.local = local;
    if (status.local_node != DD_NO_LOCAL_NODE) {
        status.nodes[status.local_node] = local;
    }
    next = status;
    k_spin_unlock(&lock, key);
#else
    next = collect_status();
    k_spinlock_key_t key = k_spin_lock(&lock);
    status = next;
    k_spin_unlock(&lock, key);
#endif
    bool changed = memcmp(&previous, &next, sizeof(next)) != 0;
    if (changed) {
        notify();
    }
#if !DD_PERIPHERAL && IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC)
    static int64_t last_sent;
    int64_t now = k_uptime_get();
    if (changed || now - last_sent >=
                       CONFIG_ZMK_DONGLE_DISPLAY_STATUS_REFRESH_SECONDS * MSEC_PER_SEC) {
        relay();
        last_sent = now;
    }
#endif
    /* One local sample/sec, no radio traffic unless changed or heartbeat due. */
    k_work_reschedule(&poll_work, K_SECONDS(1));
}

#if !DD_PERIPHERAL && IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
static int battery_event(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    if (ev && ev->source < DD_MAX_PERIPHERALS && ev->state_of_charge <= 100) {
        k_spinlock_key_t key = k_spin_lock(&lock);
        battery_seen[ev->source] = true;
        battery_levels[ev->source] = ev->state_of_charge;
        k_spin_unlock(&lock, key);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(dd_battery, battery_event);
ZMK_SUBSCRIPTION(dd_battery, zmk_peripheral_battery_state_changed);
#endif

static int status_init(void) {
    k_work_reschedule(&poll_work, K_SECONDS(1));
    return 0;
}
SYS_INIT(status_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
