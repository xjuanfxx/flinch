# How Flinch behaves

Reference for the timing and operator model implemented in
`core/include/flinch_core.h` and `core/include/flinch_render.h`. It describes
what the effect does, so the code and the tests can be checked against it.

## 1. Time

- Everything is computed **in frames**: `frame = current_time / time_step`,
  `fps = time_scale / time_step`.
- Each frame is evaluated at its **center**: `t = frame + 0.5`.

## 2. Global parameters

| Parameter | Default | Slider range | Purpose |
|---|---|---|---|
| Amount | 100 | 0–500 | global multiplier `A = Amount/100` for every intensity |
| Speed | 5 | 0–500 | multiplies the rate of every operator; ≤ 0 means no twitches |
| Ease In | 1 | 0–5 (max 100) | attack length, in frames (+1) |
| Ease Out | 1 | 0–5 (max 100) | decay length, in frames (+1) |
| Randomize Minimum | 0 | 0–100 | minimum random intensity `R = value/100` |
| Seed | 0 | 0–999 | global seed, **added** to each operator's unique seed |
| Border | Mirror | Mirror / Tile / Ignore / Expand | edge handling when displacing |

Every operator has an `Enable` switch, `Amount` (default 50, slider 0–100),
`Twitches [sec]` (default 1, slider 0–50) and `Unique Seed` (0–999).

## 3. When a twitch happens

1. **Slot length** per operator, in whole frames:
   `N = max(1, floor(fps / (TwitchesSec × Speed)))`.
   Because `N` is an integer the real rate steps (24 fps at 5 tw/s → N = 4 →
   6 tw/s). Animating `TwitchesSec` or `Speed` changes `N` and reshuffles the
   pattern.
2. **One event per slot**: slot `i = floor(t / N)` holds one event at
   `(i + r_i) × N`, with `r_i ∈ [0,1]` a deterministic random value of
   (i, seed). It is a jittered grid, not a Poisson process.
3. The previous event (≤ t) and the next one (> t) are found among slots
   i-1, i and i+1.

### Random Pattern

`random01(slot, key)` returns a deterministic value in [0,1]:

- **Modern** (default): a stateless splitmix64 hash of (slot, key). Constant
  cost at any frame.
- **Twitch Compatible**: the MSVC `rand()` LCG
  (`x = x·214013 + 2531011; r = (x >> 16) & 0x7FFF`) seeded with the key and
  advanced `|slot| + 5` steps (slots below -4 are clamped). It follows the
  pattern of the original Twitch effect; its cost grows with the slot index.

Each operator uses several independent random streams: the base seed
`UniqueSeed + Seed` plus a fixed offset per stream (see `flinch::stream`).

## 4. Envelope

With `prev` and `next` the surrounding events:

```
a   = (t - prev) / (EaseOut + 1)      // decay after the event
b   = (next - t) / (EaseIn  + 1)      // attack before the event
env = max(0, 1 - a, 1 - b)
```

Linear ramps (a triangle) in frames. Nearby twitches combine with **max**.
Because `t` is the frame center and events fall at continuous times, the peak
rarely reaches exactly 1.

## 5. Random intensity

Per slot: `rnd = R + (1 - R) × u`, with `u = random01(i, seed + k)`.
Base intensity of an operator: `x = A × OpAmount × env × rnd`.

`rnd` comes from the slot of `t`, so a decay that crosses into the next slot
changes amplitude halfway through its tail.

## 6. Operators

Render order: geometry (Scale, then Slide) → Blur → Light → Color.

### Blur
- `x = A × BlurAmount × env × rnd`, in pixels.
- Mix `m = min(1, x × 0.1)` drives Tint (`lerp(white, BlurTint, m)`),
  Opacity (`m × BlurOpacity`) and Boost (`1 + (Boost/100 − 1) × m`).
- `Blur Aspect` ∈ [-1,1] → `θ = (Aspect+1)/2 × 90°`;
  `blurX = √2·cos θ·x`, `blurY = √2·sin θ·x`.

### Color
- Strength `s = A × ColorAmount × env × rnd / 100`.
- Target color = `Colorize` + per-channel jitter
  `ColorRandomize/100 × (2u − 1)`, clamped to [0,1].

### Light
- `x = A × LightAmount × env × rnd × 0.03`.
- Brighter: gain `1 + x`; Darker: `1 / (1 + x)`; Both: picks one per slot.

### Scale
- `x = A × ScaleAmount × env × rnd / 100`; zoom in by `1 + x` (at most ×4).
- Origin = `Scale Origin` + `(u − 0.5) × OriginRandomize% × width/height`.
- Motion blur: the state is also computed at `t − 1`; `Scale Motion Blur`
  blends both ends. Crossing a peak forces the end to 1, crossing a valley to 0,
  so the blur covers the whole travel.

### Slide
- `Tendency` ∈ [-1,1] → `base = −0.5 − Tendency/2`; `v` is a random value
  pushed away from 0.5 by `R`; `d = v + base`.
- Displacement = `2 × d × A × SlideAmount% × env`, in % of the layer size.
- Direction: `angle = Direction + (u − 0.5) × Spread × 360°`, After Effects
  compass convention (0° vertical, 90° horizontal):
  `(sin angle · width, −cos angle · height)`. Spread defaults to 1, so the
  direction is fully random unless Spread is lowered.
- RGB Split blends the common offset with a per-channel one (own magnitude and
  angle): `(1 − split) × common + split × channel`.
- Motion blur as in Scale, rendered as a constant-cost trail.

### Time
- `x = A × TimeAmount × env × rnd`; frame offset `trunc(x × fps / 100)`
  (Amount 100 → up to one second).
- Time Direction: Both (random sign per slot), Forward Only, Backward Only.

### Show Twitch Only
Frames with no active twitch are transparent; the others render normally.
