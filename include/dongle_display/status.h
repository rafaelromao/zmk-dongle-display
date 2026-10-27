/* SPDX-License-Identifier: MIT */
#pragma once

#include <dongle_display/model.h>
#include <zmk/event_manager.h>

struct dd_status_changed {};
ZMK_EVENT_DECLARE(dd_status_changed);

struct dd_status dd_status_get(void);
