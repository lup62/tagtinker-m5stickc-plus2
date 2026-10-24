# Meme pack (on-device image library + recent texts)

Small add-on to the M5StickC Plus2 port: a set of 1-bit meme images and a list
of "recent texts" that ship inside the device's LittleFS, so the image gallery
and the *Send Text* list are pre-populated.

- Images live in [`../data/img/*.bmp`](../data/img) — 1-bit BMP, rescaled to the
  target tag on send.
- Texts live in [`../data/texts.txt`](../data/texts.txt) — one per line, ASCII
  only (the on-device font is ASCII), ≤63 chars each, ≤10 lines.

Both are flashed onto the device with:

```bash
pio run -t uploadfs --upload-port COMx      # writes the LittleFS image
```

## Generators

All scripts need [Pillow](https://python-pillow.org/) (`pip install pillow`).

| Script | What it makes | Usage |
|--------|---------------|-------|
| `generate_text_memes.py`  | `owo` `stonks` `doge` `brrr` (bold text / simple line art) | `python memes/generate_text_memes.py data/img` |
| `generate_drawn_memes.py` | `thisisfine` `dogedraw` (hand-drawn line art) | `python memes/generate_drawn_memes.py data/img <preview_dir>` |
| `convert_web_memes.py`    | converts downloaded `drake.jpg` / `trollface.png` to 1-bit BMP | `python memes/convert_web_memes.py <download_dir> data/img <preview_dir>` |

`convert_web_memes.py` expects the two source files in `<download_dir>`:
`drake.jpg` (imgflip "Drake Hotline Bling" template) and `trollface.png`
(the original Trollface). It crops/auto-contrasts Drake and dithers it
(Floyd–Steinberg), and hard-thresholds the Trollface line art.

## Copyright note

`drake.bmp` and `trollface.bmp` are **not included** in this repository. They are
derived from third-party, copyrighted images (the Drake Hotline Bling template /
Trollface by Carlos Ramirez). Generate them locally for **personal use** with
`convert_web_memes.py` from your own downloads — do not redistribute them. Every
image that *is* bundled here is an original drawing made for this project.

This directory is an addition to a personal fork of the M5 port — all upstream
credits and the GPL-3.0 license in the root `README.md` / `LICENSE` are unchanged.
