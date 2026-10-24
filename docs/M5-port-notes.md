# M5StickC Plus2 — bring-up & content-loading notes

Field notes from flashing and populating a board. Complements the root
`README.md`.

## Hardware seen on this unit

`esptool flash-id` reports an **ESP32-PICO-V3-02 (rev v3.1)**, Wi-Fi/BT, dual
core, embedded PSRAM, **8 MB** flash — consistent with the M5StickC Plus2.

```bash
python -m esptool --chip esp32 --port COMx flash-id
```

Use this first to find which serial port the board answers on (on Windows it is
a `COMx`; a second COM port that does not respond is some other device).

## "Black screen after flashing" — how to triage

The on-screen splash (`TagTinker` / `starting...`) is drawn to the **display**,
not to serial, so its absence from the serial log means nothing. Decide between
a boot crash and a display/backlight problem from the **boot log**:

1. Reset the board and capture UART0 @115200 (toggle DTR/RTS to reset, then read
   for a few seconds — a plain pyserial script works and avoids an interactive
   monitor).
2. Read the log:
   - Repeated `ets ... / rst:0x... / Guru Meditation / Backtrace` → **crash
     loop**. Grab the full backtrace.
   - ROM boot once, then app log lines and **no** second `ets.../rst:` → the
     firmware is **running**; a black screen is then a display/backlight issue.
   - `[...][E][Preferences.cpp] begin(): nvs_open failed: NOT_FOUND` on first
     boot is **benign** (the NVS namespace is created on first write).

On this unit the boot was healthy (no crash), so the initial black screen was a
first-flash/power transient and cleared after a power-cycle. If a genuine black
screen recurs with a healthy boot, suspect **M5Unified board detection /
backlight** (the Plus2 backlight is on GPIO27; power-hold on GPIO4).

## Flashing

```bash
# merged image (bootloader + partitions + app) at offset 0:
python -m esptool --chip esp32 --baud 921600 write_flash 0x0 tagtinker-m5stickc-plus2-full.bin

# or from source:
pio run -t upload
pio device monitor -b 115200
```

## Loading texts + the image library without the web panel

The *recent texts* and the *image gallery* live on LittleFS. You can bake them in
from the repo instead of adding them one by one through the web UI:

1. Put content under `data/`:
   - `data/texts.txt` — one meme/text per line (ASCII, ≤63 chars, ≤10 lines).
   - `data/img/*.bmp` — 1-bit BMPs (see [`../memes`](../memes)).
2. Flash the filesystem:
   ```bash
   pio run -t uploadfs --upload-port COMx
   ```
   This writes the LittleFS partition (≈ `0x670000` on the default 8 MB layout)
   and **replaces** its contents. Settings in NVS are untouched.
3. (Optional) verify the files really landed by reading the partition back:
   ```bash
   python -m esptool --chip esp32 --port COMx --baud 1500000 \
       read-flash 0x670000 0x180000 fs.bin
   # then grep fs.bin for your filenames / text strings
   ```

## Using it

Join the Wi-Fi AP `TagTinker-XXXX` (default password `tagtinker`), open
`http://192.168.4.1`. The texts and images show up immediately in the panel; to
*send* one to a tag you still need that tag's 17-character barcode (added as a
target). The board has no camera and the IR path is transmit-only, so there is
no on-device barcode scan — read the barcode with a phone app and paste it.
