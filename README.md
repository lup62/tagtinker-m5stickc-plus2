# TagTinker for M5StickC Plus2

An infrared **electronic shelf label (ESL) research toolkit** for the
**M5StickC Plus2**, ported from the Flipper Zero app
[`i12bp8/TagTinker`](https://github.com/i12bp8/TagTinker). It transmits the same
infrared ESL protocol (reverse-engineered by
[furrtek](https://www.furrtek.org/?a=esl)) using the Stick's built-in IR LED and
the ESP32 **RMT** peripheral, so timing is hardware-accurate even with Wi-Fi running.

> **Strictly for educational research and for displaying art on ESL hardware you
> own.** Do not use it to alter shelf labels in shops or interfere with anyone
> else's infrastructure.

<p align="center"><img alt="TagTinker" src="https://raw.githubusercontent.com/i12bp8/TagTinker/refs/heads/main/tagtinkerdemo.jpg" width="420"></p>

## What it does

- **Text to a tag** — type a message, pick a page, send. Auto-scaled 5×7 font,
  optional invert and accent (red/yellow) colour.
- **Images to a tag** — prepare a 1-bit (or 2-plane accent) BMP in the built-in
  Image Prep tool, upload it to the Stick, and send it. Any BMP is rescaled to
  the target tag on the fly.
- **Ping / Flash** — wake a saved tag and blink its LED to confirm it hears you.
- **Broadcast** — flip every DM tag in range to a page, or show the diagnostic
  screen. No barcode needed.
- **IR LED test** — 3 s of carrier you can see through a phone camera.
- **Targets** — save up to 16 tags by barcode; the model, size and colour are
  decoded automatically, with a manual size override for unknown types.
- **SmartTAG Color 2.6** — full support, including the landscape glass mapping
  and the 400× wake burst.

Everything is driven two ways:

1. **On the Stick** — a menu on the 135×240 screen (3 buttons).
2. **From any phone/laptop** — the Stick hosts a Wi-Fi access point and a full
   web control panel at `http://192.168.4.1`, including the TagTinker **Image
   Prep** tool with a one-click *Send to M5*.

## Flash it (easiest: your browser)

1. Open **`flasher.html`** (in this package) in **Chrome or Edge** on a desktop.
   You can just double-click it, or serve the folder — it needs to sit next to
   `tagtinker-m5stickc-plus2-full.bin`.
2. Plug the M5StickC Plus2 in with a **data** USB-C cable.
3. Click **Connect & Flash**, pick the `CP210x` / `USB-Serial` port, wait ~30 s.

### Flash from the command line

```bash
pip install esptool
# merged image, written at offset 0:
esptool.py --chip esp32 --baud 921600 write_flash 0x0 \
    firmware/tagtinker-m5stickc-plus2-full.bin
```

Or the three parts separately:

```bash
esptool.py --chip esp32 --baud 921600 write_flash \
  0x1000  firmware/bootloader.bin \
  0x8000  firmware/partitions.bin \
  0x10000 firmware/firmware.bin
```

## First run

The screen shows the Wi-Fi network name (`TagTinker-XXXX`), the password
(default **`tagtinker`**) and the panel URL. Join that Wi-Fi, open
`http://192.168.4.1`, add a target by its 17-character barcode, and send.

You can also point the Stick at your home Wi-Fi from **Settings → Wi-Fi** in the
panel, after which it's reachable on your LAN too.

## Buttons

| Button | Action |
|--------|--------|
| Front "M5" (A) | Select / confirm |
| Side (B) | Next item |
| Top right (PWR) | Back · hold ~6 s to power off |

## Hardware notes

- The **internal IR LED is on GPIO 19** (default). It is low-power — expect a
  range of a few cm to ~30 cm depending on the tag. For more range, wire an
  external IR LED (with a transistor driver) to a free GPIO and set the pin in
  **Settings → IR LED GPIO**; toggle *Invert* if your driver is active-low.
- 8 MB flash: firmware ~1.4 MB, the rest is a LittleFS image library.

## Build from source

Requires [PlatformIO](https://platformio.org/).

```bash
pio run              # build  -> .pio/build/m5stick-c-plus2/firmware.bin
pio run -t upload    # build + flash over USB
pio device monitor   # serial log @115200
```

`tools/embed_web.py` bundles `web/panel.html` and `web/prep_src.html`
(+`web/m5_inject.html`) into `src/web_assets.h` automatically before each build.

### Layout

```
src/
  main.cpp        setup/loop
  proto.{h,c}     ESL protocol (verbatim from the Flipper app — frames, CRC, RLE)
  ir.{h,cpp}      PP4 IR transmitter on the ESP32 RMT peripheral
  tx.{h,cpp}      transmit engine / job runner (port of the Flipper transmit scene)
  store.{h,cpp}   targets, image library, texts, settings (LittleFS + NVS)
  ui.{h,cpp}      on-device menu UI (M5GFX)
  web.{h,cpp}     Wi-Fi AP + web control panel + JSON API
  tt_font.h       5×7 text renderer (from the Flipper app)
web/              HTML sources for the web UI
tools/embed_web.py
```

## Meme pack

This fork ships a small library of 1-bit meme images plus a list of recent texts,
baked into LittleFS so the on-device gallery and the *Send Text* list start
populated. See [`memes/`](memes) for the generators and
[`docs/M5-port-notes.md`](docs/M5-port-notes.md) for how they are flashed
(`pio run -t uploadfs`).

| | | |
|:-:|:-:|:-:|
| ![owo](docs/preview/owo.png) | ![stonks](docs/preview/stonks.png) | ![doge](docs/preview/doge.png) |
| ![this is fine](docs/preview/thisisfine.png) | ![doge drawn](docs/preview/dogedraw.png) | ![brrr](docs/preview/brrr.png) |

All of the above are **original drawings** made for this project. Two extra
templates (Drake, Trollface) are **not bundled** — they are third-party,
copyrighted images; generate them locally for personal use with
[`memes/convert_web_memes.py`](memes/convert_web_memes.py) from your own downloads.

## Credits

- Protocol reverse engineering: **furrtek** — <https://www.furrtek.org/?a=esl>
- Original Flipper Zero app and Image Prep tool: **i12bp8/TagTinker**
- NFC tag decoding (original project): **7h30th3r0n3**

## License

GPL-3.0-only, matching the upstream project.
