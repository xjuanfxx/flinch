"""Makes the animated README banner by running Flinch itself over the wordmark.

tools/banner_art.py draws the banner as layers; the wordmark layer is rendered
frame by frame with render_frames (Slide with RGB split, Scale and Blur) and
composited between the background and the still layer (frame marks, labels,
signal line and tagline).

Usage: python tools/make_banner.py <render_frames executable> [output.webp]
Needs Pillow and numpy, and the fonts listed in banner_art.py.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

import banner_art

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "docs", "brand", "flinch-banner.webp")
OUT_WIDTH = 1200
FPS = 25
FRAMES = 100  # 4 seconds, looped

# Flinch settings for the banner.
FLINCH_ARGS = [
    "--fps", str(FPS), "--frames", str(FRAMES), "--speed", "1",
    "--ease-in", "0", "--ease-out", "2",
    "--tw", "slide", "1", "--amt", "slide", "2.5", "--split", "30", "--slide-mb", "60",
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


def main(render_frames, output=OUTPUT):
    render_frames = os.path.abspath(render_frames)
    background, moving, still = banner_art.layers()
    w, h = background.size
    out_size = (OUT_WIDTH, round(h * OUT_WIDTH / w))

    with tempfile.TemporaryDirectory() as tmp:
        write_rgba(os.path.join(tmp, "in.rgba"), np.asarray(moving))
        subprocess.run([render_frames, os.path.join(tmp, "in.rgba"), os.path.join(tmp, "f")] + FLINCH_ARGS,
                       check=True)
        frames, durations = [], []
        last = None
        for i in range(FRAMES):
            word = Image.fromarray(read_rgba(os.path.join(tmp, f"f{i:04d}.rgba")), "RGBA")
            frame = banner_art.flatten(background, word, still).resize(out_size, Image.LANCZOS)
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
