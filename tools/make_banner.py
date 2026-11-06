"""Makes the animated README banner by running Flinch itself over the static one.

The logo and wordmark are separated from the background, rendered frame by
frame with render_frames (Slide with RGB split, Scale and Blur) and composited
back over the background. The tagline stays still.

Usage: python tools/make_banner.py <render_frames executable> [output.webp]
Needs Pillow and numpy.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "docs", "brand", "flinch-banner.png")
OUTPUT = os.path.join(ROOT, "docs", "brand", "flinch-banner.webp")
OUT_WIDTH = 1200
FPS = 25
FRAMES = 100  # 4 seconds, looped
MARGIN = 110  # source pixels of background rebuilt around the wordmark
TAGLINE = (620, 860)  # the tagline (top row, left column in source pixels) stays still

# Flinch settings for the banner.
FLINCH_ARGS = [
    "--fps", str(FPS), "--frames", str(FRAMES), "--speed", "1",
    "--ease-in", "0", "--ease-out", "2",
    "--tw", "slide", "1.4", "--amt", "slide", "3", "--split", "70", "--slide-mb", "60",
    "--tw", "scale", "0.5", "--amt", "scale", "12", "--scale-mb", "60",
    "--tw", "blur", "0.6", "--amt", "blur", "14",
]


def write_rgba(path, rgba):
    h, w = rgba.shape[:2]
    with open(path, "wb") as f:
        f.write(f"FLRGBA {w} {h}\n".encode())
        f.write(np.ascontiguousarray(rgba, dtype=np.uint8).tobytes())


def read_rgba(path):
    with open(path, "rb") as f:
        _, w, h = f.readline().split()
        data = np.frombuffer(f.read(), dtype=np.uint8)
    return data.reshape(int(h), int(w), 4)


def fill_holes(img, hole):
    """Fills `hole` pixels from their surroundings (push-pull pyramid)."""
    levels = [(img * ~hole[..., None], (~hole).astype(np.float64))]
    while min(levels[-1][1].shape) > 2:
        c, w = levels[-1]
        h2, w2 = c.shape[0] // 2 * 2, c.shape[1] // 2 * 2
        c = c[:h2, :w2].reshape(h2 // 2, 2, w2 // 2, 2, 3).sum((1, 3))
        w = w[:h2, :w2].reshape(h2 // 2, 2, w2 // 2, 2).sum((1, 3))
        levels.append((c, w))
    filled = levels[-1][0] / np.maximum(levels[-1][1], 1e-9)[..., None]
    for c, w in reversed(levels[:-1]):
        up = np.repeat(np.repeat(filled, 2, 0), 2, 1)
        up = np.pad(up, ((0, c.shape[0] - up.shape[0]), (0, c.shape[1] - up.shape[1]), (0, 0)), "edge")
        known = w > 0
        filled = np.where(known[..., None], c / np.maximum(w, 1e-9)[..., None], up)
    # Smooth out the blocks of the pyramid, keeping the known pixels.
    for radius in (16, 8, 4, 2):
        for _ in range(4):
            filled = np.where(hole[..., None], box_blur(filled, radius), img)
    return filled


def box_blur(img, r):
    """Mean over a (2r+1)² window, edges clamped."""
    for axis in (0, 1):
        pad = [(0, 0)] * img.ndim
        pad[axis] = (r + 1, r)
        c = np.cumsum(np.pad(img, pad, "edge"), axis=axis)
        n = img.shape[axis]
        img = (np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=axis) - np.take(c, range(0, n), axis=axis)) / (2 * r + 1)
    return img


def main(render_frames, output=OUTPUT):
    render_frames = os.path.abspath(render_frames)
    src = np.asarray(Image.open(SOURCE).convert("RGB")).astype(np.float64)
    h, w = src.shape[:2]

    # Foreground: anything clearly brighter or more saturated than the dark background.
    diff = np.abs(src - np.median(src[:200], axis=(0, 1))).sum(2)
    fg = diff > 40
    fg[TAGLINE[0]:, TAGLINE[1]:] = False
    grown = fg.copy()  # include the anti-aliased edges
    for dy in (-2, -1, 0, 1, 2):
        for dx in (-2, -1, 0, 1, 2):
            grown |= np.roll(np.roll(fg, dy, 0), dx, 1)
    # Rebuild the background in a wide margin around the wordmark too, so it is smooth
    # wherever the moving letters can uncover it (not around the logo: its tile is
    # part of the background).
    around = box_blur(grown[..., None].astype(np.float64), MARGIN)[..., 0] > 0
    around[TAGLINE[0]:, TAGLINE[1]:] = False
    around[:, :TAGLINE[1]] = False
    background = fill_holes(src, around | grown)

    # Straight-alpha foreground layer: alpha from how far each pixel is from the background.
    alpha = np.clip(np.abs(src - background).sum(2) / 60.0, 0, 1) * grown
    layer = np.dstack([src, alpha * 255]).round().astype(np.uint8)

    with tempfile.TemporaryDirectory() as tmp:
        write_rgba(os.path.join(tmp, "in.rgba"), layer)
        subprocess.run([render_frames, os.path.join(tmp, "in.rgba"), os.path.join(tmp, "f")] + FLINCH_ARGS,
                       check=True)
        frames, durations = [], []
        out_h = round(h * OUT_WIDTH / w)
        last = None
        for i in range(FRAMES):
            f = read_rgba(os.path.join(tmp, f"f{i:04d}.rgba")).astype(np.float64)
            a = f[..., 3:] / 255.0
            rgb = f[..., :3] * a + background * (1 - a)
            frame = Image.fromarray(rgb.round().clip(0, 255).astype(np.uint8)).resize(
                (OUT_WIDTH, out_h), Image.LANCZOS)
            if last is not None and np.array_equal(np.asarray(frame), np.asarray(last)):
                durations[-1] += 1000 // FPS  # hold identical frames instead of repeating them
                continue
            frames.append(frame)
            durations.append(1000 // FPS)
            last = frame

    # Animated WebP: full color (a GIF palette bands the dark gradients) and small.
    frames[0].save(output, save_all=True, append_images=frames[1:], duration=durations, loop=0,
                   quality=92, method=6)
    print(f"{output}: {len(frames)} frames, {os.path.getsize(output) // 1024} KB")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(*sys.argv[1:])
