# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_DONGLE_DISPLAY_STATUS)
    zephyr_library()
    zephyr_library_include_directories(include ${CMAKE_SOURCE_DIR}/include)
    zephyr_library_sources(src/status_model.c src/status.c)
    # ZMK gates both local AND remote battery event definitions on local
    # reporting. A battery-less central still needs the remote event type.
    if(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING AND NOT CONFIG_ZMK_BATTERY_REPORTING)
        target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/src/events/battery_state_changed.c)
    endif()
    if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SCREEN)
        zephyr_library_sources(src/status_screen.c)
    endif()
endif()
