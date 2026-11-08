# Bring-up rig: LilyGO T-Display-S3 Touch

A way to run the real firmware on real silicon before the Waveshare 7B arrives. It is
**not** a second supported target: the panel is 320x170, eleven times fewer pixels than
the station's 1024x600, so the approved layout cannot be shown as designed.

## It cannot weigh down the product

This matters more than the rig does, so it is stated first:

- `board_tdisplay.cpp` and `testrig.cpp` are excluded from `[env]` by `build_src_filter`
  and wrapped in `#ifdef BOARD_TDISPLAY_S3`. Only `env:tdisplay` compiles them.
- **No production file branches on `BOARD_TDISPLAY_S3`.** The console reaches the rest
  of the firmware only through public APIs production already has: it seeds by writing
  rows with `storage::append_pain()` and restarting, so `history::begin()` loads them
  on boot, which is why `history` needs no back door.
- The single piece of shared code the rig's existence adds is `board::has_sd_slot()`,
  which this board answers false to because its SD pins are the LCD bus. It is
  **7 bytes** in the station image.
- Checked, not assumed: the station ELF contains no symbol and the station binary no
  string from either rig file.

Rather than redesign the UI for a small screen (which would tell us nothing about the
real one), LVGL still renders the real, unmodified 1024x600 screen into a canvas in
PSRAM. The panel then shows a *view* of that canvas.

```
pio run -e tdisplay -t upload && pio device monitor -e tdisplay
```

## The two views

| View | What you see | Switch to it |
|---|---|---|
| **Scaled** (default) | The whole 1024x600 screen, box-filtered down to 290x170. Text is unreadable; this view is for layout, balance and colour. | triple tap, or KEY |
| **1:1** | A 320x170 window of the canvas at true pixel size. **Drag to pan.** | triple tap, or KEY |

- **Triple tap** anywhere switches views.
- **KEY** (the button next to USB-C) switches views without the taps reaching the UI.
- **BOOT** steps the 1:1 window through the six tiles that cover the whole screen, so
  you can walk the entire layout at full detail.

In the scaled view taps pass straight through on PRESSED, exactly as on the real board,
so a triple tap on a pain button *also logs three taps*. Use KEY to switch views
without touching the UI. Harmless on a test rig, and the analysis counts a burst as one
episode anyway.

In the 1:1 view a tap only fires on release, so that a drag can pan instead of pressing
whatever is under your finger.

## What is real and what is not

| Real on this board | Not real |
|---|---|
| The whole UI, drawn by the shipping LVGL code at 1024x600 | Pixel-for-pixel judgement of the layout *as a whole* (you see it scaled, or a window of it) |
| Touch: pain buttons, the 10-second date hold, the setup wizard, the on-screen keyboard | Viewing angle, glass, physical button reach, how it feels at arm's length |
| Wi-Fi, NTP, Open-Meteo forecast and geocoding | GT911 touch and its palm/glitch filtering (CST816 reports a single contact) |
| Pain logging, the analysis, long-term trends | microSD (logs go to LittleFS) |
| The web UI, at **full fidelity** — it is just HTML in your browser | The BME280 (pressure comes from the forecast API, a path the firmware already supports) |
| OTA with rollback, the AP fallback, sunrise/sunset dimming | The RGB panel timing and the IO expander |

The web page is the part that loses nothing: open `http://<device name>.local` and it is
exactly what will run on the real unit.

## Filling the graphs with data

Waiting three days to see what the history graphs look like is impractical, so the rig
build has a small serial console (`pio device monitor -e tdisplay`):

| Command | What it does |
|---|---|
| `seed [days]` | Writes plausible pain taps over the last `[days]` days (default 3) and restarts to load them |
| `clear` | Deletes all pain taps and restarts |
| `status` | How much data there is |
| `help` | The list |

`seed` makes the taps the spec describes rather than noise: irregular use, quiet days,
bursts the analysis should count as one episode, and one deliberate mis-tap. Levels are
shaped by the **real** pressure history already on the device, so pain follows pressure
falls and the two graphs visibly relate.

**Seeded taps are real rows in `/pain.csv`.** Run `clear` before a unit is used for
real, or they become part of somebody's pain history.

## Hardware notes

Board: ESP32-S3R8, 16 MB flash, 8 MB PSRAM, ST7789 170x320 on an 8-bit i80 bus, CST816
touch at I2C 0x15. All of it is in `firmware/src/board_tdisplay.cpp`, which replaces
`board.cpp` for this env (`build_src_filter = +<*> -<board.cpp>`).

The panel keeps its **native portrait** scan order, with the column gap of 35 set on the
controller, and the landscape rotation is done in the presenter. That keeps MADCTL out
of it: there is one `ROT_BASE` constant for how the panel is mounted (touch follows it
automatically, since the touch mapping is derived from the same flag), and the
user-facing 180-degree flip on the Maintenance screen composes with it.

Colour order is `LCD_RGB_ELEMENT_ORDER_RGB`. LovyanGFX's `rgb_order = false` for this
panel reads as BGR, but that is the opposite polarity to esp_lcd's `rgb_ele_order`, so
copying it across swaps red and blue. The symptom is subtle because green is the
untouched channel: a green-to-red ramp appears green-to-blue, and only the ends move.

A BME280 wired to **GPIO 18 (SDA) / 17 (SCL)** — the touch bus — would be found at 0x76
or 0x77 as usual and logged for real.

## Memory

| | |
|---|---|
| 1024x600 canvas | 1.23 MB PSRAM |
| LVGL draw buffer | 123 KB PSRAM |
| Panel frame | 109 KB internal (DMA), PSRAM if internal is full |
