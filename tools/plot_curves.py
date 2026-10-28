"""Plots the twitch_curves CSV as SVG (standard library only).

Usage: python tools/plot_curves.py curves.csv output.svg
"""
import csv
import sys

TRACKS = [
    ("blur_env", "Blur — envelope", "#4f7cff"),
    ("blur_x", "Blur — size X (px)", "#4f7cff"),
    ("light_gain", "Light — gain", "#e0a000"),
    ("scale_factor", "Scale — sampling factor", "#1aa37a"),
    ("slide_x", "Slide — offset X (px)", "#d9480f"),
    ("slide_y", "Slide — offset Y (px)", "#d9480f"),
    ("time_offset", "Time — offset (frames)", "#8e44ad"),
]


def main(src, dst):
    with open(src, newline="") as f:
        rows = list(csv.DictReader(f))
    frames = [int(r["frame"]) for r in rows]
    width, track_h, gap, left = 960, 90, 34, 70
    height = len(TRACKS) * (track_h + gap) + 20
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'font-family="Segoe UI, sans-serif" font-size="12">',
           f'<rect width="100%" height="100%" fill="#fff"/>']
    plot_w = width - left - 20
    x_of = lambda fr: left + plot_w * (fr - frames[0]) / max(1, frames[-1] - frames[0])
    for k, (col, label, color) in enumerate(TRACKS):
        top = 20 + k * (track_h + gap)
        vals = [float(r[col]) for r in rows]
        lo, hi = min(vals), max(vals)
        if hi - lo < 1e-9:
            lo, hi = lo - 1, hi + 1
        y_of = lambda v: top + track_h - track_h * (v - lo) / (hi - lo)
        out.append(f'<text x="{left}" y="{top - 6}" fill="#333">{label}</text>')
        out.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{track_h}" '
                   f'fill="none" stroke="#ddd"/>')
        out.append(f'<text x="{left - 6}" y="{top + 10}" text-anchor="end" fill="#888">{hi:g}</text>')
        out.append(f'<text x="{left - 6}" y="{top + track_h}" text-anchor="end" fill="#888">{lo:g}</text>')
        for fr in frames:
            if fr % 30 == 0:
                out.append(f'<line x1="{x_of(fr):.1f}" y1="{top}" x2="{x_of(fr):.1f}" '
                           f'y2="{top + track_h}" stroke="#f0f0f0"/>')
        pts = " ".join(f"{x_of(fr):.1f},{y_of(v):.1f}" for fr, v in zip(frames, vals))
        out.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="1.5"/>')
        for fr, v in zip(frames, vals):
            out.append(f'<circle cx="{x_of(fr):.1f}" cy="{y_of(v):.1f}" r="1.6" fill="{color}"/>')
    out.append("</svg>")
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
