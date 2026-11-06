"""Draws the README banner as separate layers, so make_banner.py can run Flinch
over the wordmark alone.

Layers, all at SIZE: `background` (RGB), `moving` (RGBA: the wordmark, which
Flinch animates) and `still` (RGBA: frame marks, labels, the signal line and the
tagline). `python tools/banner_art.py` writes the flattened still to
docs/brand/flinch-banner.png.

Fonts (SIL Open Font License, from Google Fonts): Inter Black and JetBrains Mono.
They are looked up in the system and user font folders, or in FLINCH_FONTS.
Needs Pillow and numpy.
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON = os.path.join(ROOT, "docs", "brand", "flinch-icon-1024.png")
STILL = os.path.join(ROOT, "docs", "brand", "flinch-banner.png")
SIZE = (2400, 1024)

INK = (14, 15, 21)
CREAM = (245, 242, 234)
MUTED = (142, 145, 166)
LIME = (184, 255, 46)
VIOLET = (123, 92, 255)

WORD_CENTER_Y = 430
WORD_SIZE = 330
SLANT = 0.2  # forward lean, as in the F of the icon
SLICE = (0.47, 0.6)  # band of the wordmark, as fractions of its height, that is jolted
SLICE_SHIFT = 30
SPLIT = 9  # px of lime/violet showing on each side of the jolted band
SIGNAL_Y = 690
INSET = 72


def font(name, size):
    dirs = [os.environ.get("FLINCH_FONTS", ""),
            os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts"),
            os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts"),
            os.path.expanduser("~/Library/Fonts"), "/Library/Fonts", "/usr/share/fonts/truetype"]
    for d in dirs:
        path = os.path.join(d, name)
        if d and os.path.exists(path):
            return ImageFont.truetype(path, size)
    raise SystemExit(f"font {name} not found; install it or set FLINCH_FONTS")


def spaced(draw, xy, text, fnt, fill, tracking, anchor="l"):
    """Draws `text` with extra letter spacing; anchor is l, m or r on the baseline."""
    widths = [draw.textlength(c, font=fnt) for c in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x = xy[0] - {"l": 0, "m": total / 2, "r": total}[anchor]
    for c, w in zip(text, widths):
        draw.text((x, xy[1]), c, font=fnt, fill=fill, anchor="ls")
        x += w + tracking
    return total


def icon(size):
    """The app icon, cropped to its rounded tile."""
    img = Image.open(ICON).convert("RGBA")
    s = img.width / 512  # flinch-icon.svg: the tile is at 16,16 and 480 wide
    return img.crop((round(16 * s), round(16 * s), round(496 * s), round(496 * s))).resize((size, size), Image.LANCZOS)


def background():
    w, h = SIZE
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    base = np.array(INK, dtype=np.float64)
    # A soft violet glow behind the wordmark and a faint lift towards the top left.
    glow = np.exp(-(((x - w / 2) / 820) ** 2 + ((y - WORD_CENTER_Y) / 330) ** 2))
    lift = np.clip(1 - (x / w + y / h) / 1.4, 0, 1)
    rgb = base + glow[..., None] * np.array([22, 14, 52]) + lift[..., None] * 7
    # Vignette.
    v = ((x - w / 2) / (w * 0.62)) ** 2 + ((y - h / 2) / (h * 0.75)) ** 2
    rgb *= 1 - 0.35 * np.clip(v, 0, 1)[..., None]
    return Image.fromarray(rgb.round().clip(0, 255).astype(np.uint8))


def wordmark():
    """FLINCH, leaning forward, with one band of it jolted sideways and split."""
    fnt = font("Inter-Black.ttf", WORD_SIZE)
    text = "FLINCH"
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    tracking = 6
    width = sum(probe.textlength(c, font=fnt) for c in text) + tracking * (len(text) - 1)
    top, bottom = fnt.getbbox("FLINCH", anchor="ls")[1], 0
    cap = bottom - top
    pad = 120
    mask = Image.new("L", (int(width + 2 * pad + cap * SLANT), cap + 2 * pad), 0)
    spaced(ImageDraw.Draw(mask), (pad, pad + cap), text, fnt, 255, tracking)
    # Lean the letters forward around their baseline.
    mask = mask.transform(mask.size, Image.AFFINE, (1, SLANT, -SLANT * (pad + cap), 0, 1, 0),
                          resample=Image.BICUBIC)
    m = np.asarray(mask).astype(np.float64) / 255

    y0, y1 = (pad + int(cap * f) for f in SLICE)
    band = np.zeros_like(m)
    band[y0:y1] = np.roll(m[y0:y1], SLICE_SHIFT, axis=1)
    body = m.copy()
    body[y0:y1] = 0
    # The jolted band carries the split: lime trailing, violet leading.
    lime = np.zeros_like(m)
    violet = np.zeros_like(m)
    lime[y0:y1] = np.roll(m[y0:y1], SLICE_SHIFT - SPLIT, axis=1)
    violet[y0:y1] = np.roll(m[y0:y1], SLICE_SHIFT + SPLIT, axis=1)

    rgb = np.zeros(m.shape + (3,))
    alpha = np.zeros_like(m)
    for layer, color in ((lime, LIME), (violet, VIOLET), (body, CREAM), (band, CREAM)):
        a = layer
        rgb = rgb * (1 - a[..., None]) + np.array(color) * a[..., None]
        alpha = alpha * (1 - a) + a
    rgb = np.where(alpha[..., None] > 0, rgb / np.maximum(alpha, 1e-6)[..., None], 0)
    out = np.dstack([rgb, alpha * 255]).round().clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA"), pad


def moving():
    layer = Image.new("RGBA", SIZE, (0, 0, 0, 0))
    word, pad = wordmark()
    x = (SIZE[0] - word.width) // 2 + 10
    y = WORD_CENTER_Y - word.height // 2
    layer.alpha_composite(word, (x, y))
    return layer


def signal(draw, w):
    """A flat trace that jumps once: the flinch, drawn as a startle on a timeline."""
    cx = w // 2 + 40
    pts = []
    for x in range(INSET, w - INSET + 1, 4):
        d = (x - cx) / 30
        # A sharp spike up, a recoil below the line and a small settle.
        off = 100 * np.exp(-(d * 1.5) ** 2) - 44 * np.exp(-((d - 1.3) * 1.7) ** 2) \
            + 12 * np.exp(-((d - 2.6) * 1.6) ** 2)
        pts.append((x, SIGNAL_Y - off))
    dim = CREAM + (70,)
    lo, hi = cx - 90, cx + 130  # the lit part of the trace
    draw.line([p for p in pts if p[0] <= lo], fill=dim, width=3)
    draw.line([p for p in pts if p[0] >= hi], fill=dim, width=3)
    draw.line([p for p in pts if lo <= p[0] <= hi], fill=LIME + (255,), width=5, joint="curve")
    # Keyframes along the trace.
    for x in (INSET + 300, cx - 420, cx + 420, w - INSET - 300):
        r = 9
        draw.polygon([(x, SIGNAL_Y - r), (x + r, SIGNAL_Y), (x, SIGNAL_Y + r), (x - r, SIGNAL_Y)], fill=CREAM + (150,))
    r, x = 12, cx - 130  # the keyframe that sets the jolt off
    draw.polygon([(x, SIGNAL_Y - r), (x + r, SIGNAL_Y), (x, SIGNAL_Y + r), (x - r, SIGNAL_Y)], fill=LIME + (255,))


def still():
    w, h = SIZE
    layer = Image.new("RGBA", SIZE, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    mono = font("JetBrainsMono-Regular.ttf", 26)
    mono_b = font("JetBrainsMono-Bold.ttf", 26)
    tag = font("JetBrainsMono-Regular.ttf", 40)

    # Viewfinder corners.
    arm, c = 46, CREAM + (90,)
    for (x, y, sx, sy) in ((INSET, INSET, 1, 1), (w - INSET, INSET, -1, 1),
                           (INSET, h - INSET, 1, -1), (w - INSET, h - INSET, -1, -1)):
        d.line([(x, y + sy * arm), (x, y), (x + sx * arm, y)], fill=c, width=3)

    top, bottom = INSET + 66, h - INSET - 26
    layer.alpha_composite(icon(64), (INSET + 34, top - 46))
    x = INSET + 34 + 64 + 22
    x += spaced(d, (x, top), "FLINCH", mono_b, CREAM, 6)
    spaced(d, (x + 22, top), "/  AFTER EFFECTS PLUG-IN", mono, MUTED, 6)
    spaced(d, (w - INSET - 34, top), "CPU  ·  CUDA  ·  OPENCL  ·  METAL", mono, MUTED, 6, "r")
    spaced(d, (INSET + 34, bottom), "8 / 16 / 32 BPC  ·  SMARTFX  ·  MFR", mono, MUTED, 6)
    spaced(d, (w - INSET - 34, bottom), "WINDOWS  ·  MACOS", mono, MUTED, 6, "r")

    signal(d, w)
    spaced(d, (w // 2, SIGNAL_Y + 124), "RANDOM JOLTS FOR AFTER EFFECTS", tag, (196, 198, 214), 14, "m")
    return layer


def layers():
    return background(), moving(), still()


def flatten(bg, *rgba):
    out = bg.convert("RGBA")
    for layer in rgba:
        out.alpha_composite(layer)
    return out.convert("RGB")


if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else STILL
    flatten(*layers()).save(out, optimize=True)
    print(out)
