# Dongle Display

This fork provides central **and peripheral** status screens for 128x32 and
128x64 OLEDs. A peripheral screen can show the central's USB/Bluetooth host
status and the battery/connection status of every half, even when the central
is a USB-powered dongle with no display or battery.

## Original widget screen (default)

The original screen, icon assets, widget positions and animations are preserved
on both central and peripheral OLEDs. USB/Bluetooth output indicators, battery
icons and percentages, Bongo Cat, modifiers, lock indicators, layer names and WPM
use the same widgets as before. A peripheral reads the central's relayed state,
not an incomplete local copy of its keymap or host connection.

All existing `CONFIG_ZMK_DONGLE_DISPLAY_*` widget options still apply. Nothing is
automatically disabled based on resolution. For a 128x32 screen, you can keep
the usual smaller selection, for example:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_BONGO_CAT=n
CONFIG_ZMK_DONGLE_DISPLAY_MODIFIERS=n
```

Both widgets remain available with `=y`, including on a peripheral. Enable WPM
with `CONFIG_ZMK_DONGLE_DISPLAY_WPM=y`. Lock indicators accompany the modifiers
widget, and the Mac/Windows modifier icon selection is unchanged.

Battery rows retain their original top-right position and pairing order: central
first only if it has an enabled battery, then all peripherals. A battery-less
central has no invented percentage; its connectivity appears in the host output
widget. `0%` remains a valid reading, `?` means unknown, and `--` means a remote
half is disconnected/stale. Local battery sampling remains authoritative even
after the split disconnects. More battery rows than fit are paginated every five
seconds without changing the rest of the screen.

## Optional compact diagnostic screen

Set `CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SCREEN=y` to use a text-only diagnostic
screen instead of the original widgets. It uses eight-pixel rows:

```text
>USB+  BT1-
C+ USB
P1+ 75%
P2+ 90%
```

`>` marks the selected (or preferred, while disconnected) output. `+` means
connected/USB HID ready, `-` disconnected, and `BTn?` an unbonded host profile.
`C` is the central; `P1`, `P2`, etc. are ZMK split source IDs in **pairing
order**, not fixed left/right labels. `?` is an unknown battery reading; `0%`
is a valid empty battery, not a disconnect. A battery-less central shows `USB`
when powered, never an invented percentage. USB after a battery percentage
indicates local USB power, not a charger IC's charging/charge-complete state.
ZMK's standard remote battery service does not expose remote charging state.

Both resolutions show the central and two peripherals at once. More peripherals
are paginated every five seconds; set `CONFIG_ZMK_DONGLE_DISPLAY_MAX_PERIPHERALS`
to the same value (up to 14) on central and display halves. The screen reads its
dimensions from `zephyr,display`; no resolution-specific firmware edits are
needed beyond the existing display devicetree. Leave
`CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SCREEN=n` (the default) for the original widgets.

## Screen on a peripheral, headless central

Add the module to **both** builds. Use these extra shields:

```yaml
include:
  - board: nice_nano//zmk
    shield: my_keyboard_central_dongle dongle_display_relay
  - board: nice_nano//zmk
    shield: my_keyboard_peripheral_left dongle_display
  - board: nice_nano//zmk
    shield: my_keyboard_peripheral_right
```

The normal keyboard shields still define the matrix, split roles and display
hardware. The relay shield adds **no** display, LVGL, I2C, battery hardware or
new Bluetooth connection. It carries status in an internal behavior over ZMK's
existing encrypted central-to-peripheral split connection. No ZMK patch is
required. This implementation targets the post-transport-refactor ZMK version
used by `rafaelromao/keyboards` (`9ebbeff0a8b69a42f14aec022cdf16c7a107b9e0`,
Zephyr 4.1 / LVGL 9); it does not claim compatibility with ZMK v0.3.

Central configuration (for a USB-only, battery-less receiver):

```ini
CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=2
CONFIG_ZMK_BATTERY=n
CONFIG_ZMK_BATTERY_REPORTING=n
CONFIG_BT_BAS=n
CONFIG_ZMK_DISPLAY=n
CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC=y
CONFIG_ZMK_DONGLE_DISPLAY_STATUS_RECEIVERS=0x1
```

`STATUS_RECEIVERS` is a bit mask of split source IDs with screens: `0x1` for
the first paired half, `0x2` for the second, `0x3` for both. Pair the display
half first or change the mask; other halves need no display module. The central
collects connectivity and battery status for **all** halves, not just receivers.
The relay shield enables remote battery fetching. This module supplies ZMK's
battery event definitions when local reporting is disabled, without starting
local battery sampling. For a battery-powered central, keep normal battery
reporting and enable `CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY=y` instead.

Display peripheral configuration:

```ini
CONFIG_ZMK_DISPLAY=y
CONFIG_ZMK_BATTERY_REPORTING=y
CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SYNC=y
CONFIG_LV_Z_VDB_SIZE=32
```

Host/battery changes are coalesced within one second. Typing and layer changes
are collected after a 20 ms coalescing window and queued separately, so modifier
or WPM changes do not retransmit every battery. The central relays the highest
layer and its name (the same 12-character label limit as the original widget),
explicit modifiers, host lock indicators and WPM. Bongo Cat animates locally
from WPM, not from a radio packet for every animation frame. The relay enables
WPM and, by default, host lock-indicator collection on the headless central,
without enabling a display or local battery sampling there.

Widget snapshots are four paced 20-byte behavior packets. They carry a sequence
number and are applied atomically only after the full snapshot arrives; dropped
or mixed chunks do not show half-updated layer names. Intermediate snapshots
are coalesced while a transmission is in progress. This adds radio traffic while
typing compared to battery-only relay; battery-life improvement is not measured.
Flash matching module versions on the central and display half.

A complete heartbeat every 30 seconds
recovers from dropped writes and split discovery not having finished at initial
connection. `CONFIG_ZMK_DONGLE_DISPLAY_STATUS_REFRESH_SECONDS` changes this
interval (use the same setting on both devices). There is no periodic radio
traffic from display rendering or pagination. Once ZMK reports a split
disconnect (after BLE supervision detects it), the screen updates within one
second; missing status expires after two heartbeat intervals. Local
battery remains available while disconnected. Normal display blanking and
peripheral deep sleep are preserved; status updates do not count as key activity.

## Tests

Run `bash tests/run.sh` with a host C compiler. It checks the actual packet
encoder/decoder and status formatting with ASan/UBSan, including all supported
source IDs, zero/unknown batteries, battery-less central, malformed packets and
reconnection. Widget tests cover modifier/WPM/lock state, layer-label chunks,
partial/dropped/mixed frames, sequence rollover and disconnect reset.
Firmware/physical BLE and OLED tests are separate.

## Usage

To use this module, first add it to your `config/west.yml` by adding a new entry to `remotes` and `projects`:

```yaml west.yml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: rafaelromao
      url-base: https://github.com/rafaelromao
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-dongle-display
      remote: rafaelromao
      revision: main # publish the fork's changes before using a remote build
  self:
    path: config
```

Next, replace the built-in status screen by adding `dongle_display` to your `build.yaml`:

```yaml build.yaml
---
include:
  - board: xiao_ble
    shield: sweep_dongle dongle_display
```

This shield assumes that the [dongle](https://zmk.dev/docs/development/hardware-integration/dongle) is already set up and functioning with the built-in status screen.
For setup examples, refer to the shields in my [`zmk-config`](https://github.com/englmaxi/zmk-config/tree/main/boards/shields).
- If you are using the larger 1.3" OLED, replace `solomon,ssd1306fb` with `sinowealth,sh1106` and set `segment-offset = <2>`.
- If you are using a nice!nano, replace `xiao_i2c` with `pro_micro_i2c`.
- If you are using the smaller 0.91" OLED, replace `height = <64>;` by `height = <32>;`

## Widgets

The following are the default screen's original widgets, supported on central
and peripheral displays (`CONFIG_ZMK_DONGLE_DISPLAY_STATUS_SCREEN=n`):
- active hid indicators (CLCK, NLCK, SLCK)
- active modifiers
- bongo cat
- highest layer name
- output status
- peripheral battery levels
- WPM meter

## Configuration

To also display the battery level of the dongle/central device, use the following configuration property:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY=y
```

If you want to use MacOS modifier symbols instead of the Windows modifier symbols, use the following configuration property:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS=y
```

### Layer name display

To set the text alignment for the layer name:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_LAYER_TEXT_ALIGN="left"    # default, or "center", "right"
```


To customize the width of the layer name label (before it starts scrolling), use:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_LAYER_NAME_SCROLL_WIDTH=50  # default is 50 pixels
```

When the layer name exceeds this width, it will scroll horizontally in a circular pattern.

If you set it to >50 then it's better to align text to the right

```ini
CONFIG_ZMK_DONGLE_DISPLAY_LAYER_TEXT_ALIGN="right"
```

### WPM meter
If you want to enable the WPM meter:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_WPM=y
```

If you want the widget not to measure WPM on specific layers

```ini
CONFIG_ZMK_DONGLE_DISPLAY_WPM_DISABLED_LAYERS="layers" # comma separated
```

## Smaller OLEDs, with 128x32 pixels

All widgets remain available on 128x32. To use the original screen's smaller
configuration, exclude the widgets you do not want:

```ini
CONFIG_ZMK_DONGLE_DISPLAY_BONGO_CAT=n
CONFIG_ZMK_DONGLE_DISPLAY_MODIFIERS=n
CONFIG_ZMK_DONGLE_DISPLAY_LAYER=n
```

It is also necessary to reduce the buffer size, otherwise the I2C communication can become unstable:

```ini
CONFIG_LV_Z_VDB_SIZE=32
```

## Demo
![output](https://github.com/englmaxi/zmk-config/assets/43675074/8d268f23-1a4f-44c3-817e-c36dc96a1f8b)
![mods](https://github.com/englmaxi/zmk-config/assets/43675074/af9ec3f5-8f61-4629-abed-14ba0047f0bd)

## Dongle Designs
- [case1](/cases)
- [case2](/cases)
- [Cyberdeck](https://github.com/rafaelromao/keyboards/tree/main/stls/Dongle) by @rafaelromao
- [Dongle PCB](https://github.com/spe2/zmk_dongle_hardware) by @spe2
- [Macintosh](https://makerworld.com/en/models/403660) by @rain2813
- [Redox](https://makerworld.com/en/models/242951) by @rurounikexin
- [sai44 Dongle](https://github.com/leafflat/sai44/tree/main/STL/Dongle) by @leafflat
- [ZMK Display Dongle](https://makerworld.com/en/models/496738) by @yingeling
- [ZMK Dongle for 128X64 I2C OLED Display SSD1306 12864](https://www.printables.com/model/1512201-zmk-dongle-for-128x64-i2c-oled-display-ssd1306-128) by @choovick
- [ZMK Nice Nano 128x64 OLED Dongle](https://www.printables.com/model/1207682-zmk-nice-nano-128x64-oled-dongle) by @James_909973
- [Dilemma DIY with 128x32 OLED](https://github.com/rafaelromao/keyboards/tree/main/src/keyboards/bastardkb/dilemma/boards/shields/dilemma)


## Alternatives
- [Prospector](https://github.com/carrefinho/prospector) by @carrefinho
- [YADS](https://github.com/janpfischer/zmk-dongle-screen) by @janpfischer
