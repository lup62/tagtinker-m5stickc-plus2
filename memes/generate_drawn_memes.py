import os, sys
from PIL import Image, ImageDraw, ImageFont

imgdir = sys.argv[1]
prevdir = sys.argv[2]
os.makedirs(imgdir, exist_ok=True)
os.makedirs(prevdir, exist_ok=True)
W, H = 296, 128

def font(sz, bold=True, comic=False):
    names = (["comicbd.ttf", "comic.ttf"] if comic else []) + \
            (["arialbd.ttf"] if bold else ["arial.ttf"]) + ["arial.ttf", "segoeui.ttf"]
    for n in names:
        try:
            return ImageFont.truetype(n, sz)
        except OSError:
            pass
    return ImageFont.load_default()

def center(d, text, f, y, fill=0, w=W, x0=0):
    bb = d.textbbox((0, 0), text, font=f)
    tw = bb[2] - bb[0]
    d.text((x0 + (w - tw) / 2 - bb[0], y), text, fill=fill, font=f)

def flames(d, y_base, x0, x1, n, h, fill=0):
    """Row of flame-ish triangles pointing up along y_base."""
    step = (x1 - x0) / n
    for i in range(n):
        bx = x0 + i * step
        hh = h if i % 2 == 0 else int(h * 0.6)
        d.polygon([(bx, y_base), (bx + step / 2, y_base - hh), (bx + step, y_base)],
                  outline=fill, width=2)
        # little inner tongue
        d.line([(bx + step / 2, y_base), (bx + step / 2, y_base - hh * 0.55)], fill=fill, width=2)

# ---------------------------------------------------------------- this is fine
im = Image.new("1", (W, H), 1)
d = ImageDraw.Draw(im)
center(d, "THIS IS FINE", font(30), 2)

# dog with little hat, seated at a table
# hat
d.ellipse([120, 44, 178, 54], outline=0, width=3)        # brim
d.rectangle([134, 28, 164, 46], outline=0, width=3)      # crown
# head
d.ellipse([126, 50, 172, 92], outline=0, width=3)
# eyes (resigned)
d.line([136, 66, 144, 66], fill=0, width=3)
d.line([154, 66, 162, 66], fill=0, width=3)
# snout + nose + flat mouth
d.ellipse([140, 72, 158, 86], outline=0, width=2)
d.ellipse([146, 74, 152, 79], fill=0)
d.line([143, 84, 155, 84], fill=0, width=2)
# body
d.polygon([(130, 90), (168, 90), (176, 116), (122, 116)], outline=0, width=3)
# table line + mug
d.line([60, 116, 236, 116], fill=0, width=3)
d.rectangle([84, 104, 100, 116], outline=0, width=2)     # mug
d.arc([98, 105, 108, 115], -80, 80, width=2)             # handle
# flames all around the bottom and lower sides
flames(d, 127, 0, 296, 12, 16)
flames(d, 116, 0, 60, 3, 22)
flames(d, 116, 236, 296, 3, 22)
im.save(os.path.join(imgdir, "thisisfine.bmp"))
im.convert("L").resize((888, 384), Image.NEAREST).save(os.path.join(prevdir, "thisisfine.png"))

# ---------------------------------------------------------------- drawn doge
im = Image.new("1", (W, H), 1)
d = ImageDraw.Draw(im)
cx = 150
# ears (pointy, filled outline)
d.polygon([(108, 44), (86, 4), (134, 30)], outline=0, width=3)
d.polygon([(192, 44), (214, 4), (166, 30)], outline=0, width=3)
d.polygon([(104, 40), (94, 16), (124, 30)], fill=0)       # inner ear
d.polygon([(196, 40), (206, 16), (176, 30)], fill=0)
# head (shiba: wide cheeks) via overlapping ellipses
d.ellipse([96, 24, 204, 120], outline=0, width=3)
# cheek fluff bumps
for bx in range(96, 150, 16):
    d.arc([bx, 96, bx + 20, 124], 20, 160, width=2)
for bx in range(150, 204, 16):
    d.arc([bx, 96, bx + 20, 124], 20, 160, width=2)
# eyebrow patches (doge signature)
d.ellipse([116, 46, 138, 60], outline=0, width=2)
d.ellipse([162, 46, 184, 60], outline=0, width=2)
# eyes
d.ellipse([122, 60, 134, 72], fill=0)
d.ellipse([166, 60, 178, 72], fill=0)
# muzzle
d.ellipse([128, 74, 172, 112], outline=0, width=2)
# nose
d.polygon([(142, 80), (158, 80), (150, 90)], fill=0)
# mouth: the doge 'w' smile
d.line([150, 90, 150, 98], fill=0, width=2)
d.arc([132, 92, 150, 110], 300, 60, width=3)
d.arc([150, 92, 168, 110], 120, 240, width=3)
# scattered comic text
fc = font(16, comic=True)
d.text((6, 4), "wow", fill=0, font=fc)
d.text((6, 104), "such ir", fill=0, font=fc)
d.text((196, 106), "much meme", fill=0, font=font(14, comic=True))
d.text((234, 6), "very tag", fill=0, font=font(14, comic=True))
im.save(os.path.join(imgdir, "dogedraw.bmp"))
im.convert("L").resize((888, 384), Image.NEAREST).save(os.path.join(prevdir, "dogedraw.png"))

print("ok")
