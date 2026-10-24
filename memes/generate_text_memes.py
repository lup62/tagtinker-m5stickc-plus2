import os, sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

W, H = 296, 128  # common 2.9" ESL size; rescaled to the real tag on send

def font(sz):
    for name in ("arialbd.ttf", "arial.ttf", "segoeuib.ttf"):
        try:
            return ImageFont.truetype(name, sz)
        except OSError:
            pass
    return ImageFont.load_default()

def center(d, text, f, y, img_w=W):
    bb = d.textbbox((0, 0), text, font=f)
    w = bb[2] - bb[0]
    d.text(((img_w - w) / 2 - bb[0], y), text, fill=0, font=f)

def new():
    im = Image.new("1", (W, H), 1)  # 1 = white background
    return im, ImageDraw.Draw(im)

def save(im, name):
    im.save(os.path.join(out, name))
    print("wrote", name)

# 1) OwO
im, d = new()
d.rectangle([2, 2, W - 3, H - 3], outline=0, width=3)
center(d, "OwO", font(72), 6)
center(d, "what's this?", font(26), 90)
save(im, "owo.bmp")

# 2) STONKS
im, d = new()
center(d, "STONKS", font(60), 10)
center(d, "only up", font(26), 86)
# rising arrow
d.line([(30, 110), (120, 70), (180, 90), (266, 24)], fill=0, width=4)
d.polygon([(266, 24), (248, 30), (258, 44)], fill=0)
save(im, "stonks.bmp")

# 3) doge
im, d = new()
center(d, "WOW", font(44), 4)
center(d, "such IR", font(34), 52)
center(d, "very tag", font(30), 94)
save(im, "doge.bmp")

# 4) brrr
im, d = new()
d.rectangle([0, 0, W - 1, H - 1], fill=0)          # inverted panel
im2 = Image.new("1", (W, H), 0)
d = ImageDraw.Draw(im2)
def center_w(text, f, y):
    bb = d.textbbox((0, 0), text, font=f)
    w = bb[2] - bb[0]
    d.text(((W - w) / 2 - bb[0], y), text, fill=1, font=f)  # white text
center_w("IR LED", font(46), 10)
center_w("GO BRRR", font(46), 66)
save(im2, "brrr.bmp")

print("done ->", out)
