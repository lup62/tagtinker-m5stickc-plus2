import os, sys
from PIL import Image, ImageOps

dl, imgdir, prevdir = sys.argv[1], sys.argv[2], sys.argv[3]

def fit(im, maxside):
    w, h = im.size
    s = maxside / max(w, h)
    return im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)

def save(bw, name):
    bw.save(os.path.join(imgdir, name + ".bmp"))
    bw.convert("L").resize((bw.width * 3, bw.height * 3), Image.NEAREST) \
      .save(os.path.join(prevdir, name + ".png"))
    print(f"{name+'.bmp':16} {bw.size} bpp1")

# --- Trollface: line art -> white bg, hard threshold (no dither) ---
tf = Image.open(os.path.join(dl, "trollface.png")).convert("RGBA")
bg = Image.new("RGBA", tf.size, (255, 255, 255, 255))
tf = Image.alpha_composite(bg, tf).convert("L")
print("trollface src", tf.size)
tf = fit(tf, 288)
tf_bw = tf.point(lambda p: 255 if p > 150 else 0).convert("1", dither=Image.Dither.NONE)
save(tf_bw, "trollface")

# --- Drake: photographic. Crop to the left half of each panel (where Drake
#     actually is; the right half is blank caption space), boost contrast,
#     then Floyd-Steinberg dither. ---
dk = Image.open(os.path.join(dl, "drake.jpg")).convert("L")
print("drake src", dk.size)
w, h = dk.size
dk = dk.crop((0, 0, int(w * 0.52), h))     # keep the two Drake poses
dk = ImageOps.autocontrast(dk, cutoff=2)
dk = fit(dk, 320)
dk_bw = dk.convert("1")                      # default Floyd-Steinberg dithering
save(dk_bw, "drake")

print("done")
