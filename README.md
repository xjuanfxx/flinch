# Flinch

Flinch is an After Effects effect that adds short, random, involuntary-looking
jolts to a layer: blur, color, light, scale, slide and time "twitches" that fire
on their own and fade in and out over a few frames.

> **Educational project.** Flinch was built to learn how this kind of effect
> works and how to write a modern After Effects plugin. It is an independent
> implementation and is **not affiliated with, endorsed by or derived from the
> code of** Video Copilot or its *Twitch* plugin. See [Disclaimer](#disclaimer).

## Features

- Six operators, each with its own amount, rate and seed:
  **Blur**, **Color**, **Light**, **Scale**, **Slide** (with RGB split) and **Time**.
- Deterministic randomness: the same seed always gives the same result, so
  renders are repeatable.
- Motion blur for Slide and Scale with constant cost per pixel, regardless of
  how far the content moves.
- 8, 16 and 32-bit (float) color, SmartFX, multi-frame rendering.
- Windows (x64) and macOS (universal: Apple Silicon + Intel).

## Project layout

| Path | Contents |
|---|---|
| `core/include/flinch_core.h` | Timing and randomness: when each twitch fires and how strong it is. No After Effects dependency. |
| `core/include/flinch_render.h` | Pixel processing (zoom, slide, blur, light, color) on float images. No After Effects dependency. |
| `plugin/` | The After Effects adapter: parameters, PiPL resource, SmartFX pre-render/render. |
| `tests/` | Unit tests for the core and the renderer. |
| `tools/` | `flinch_curves` (dump per-frame state as CSV), `plot_curves.py` (plot it as SVG), `bench_render` (performance). |
| `analysis/` | Behaviour notes used as the reference for the implementation. |
| `third_party/ae_sdk/` | The parts of the Adobe After Effects SDK needed to build (see [Disclaimer](#disclaimer)). |
| `scripts/` | Install scripts for Windows and macOS. |

## Building

Requirements: CMake 3.20+ and a C++17 compiler
(Visual Studio 2022 on Windows, Xcode command line tools on macOS).

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output:

- Windows: `build/Release/Flinch.aex`
- macOS: `build/Flinch.plugin` (universal binary)

Every push is also built and tested on GitHub Actions for both platforms; the
plugins can be downloaded from the run's artifacts.

## Installing

Prebuilt plugins are attached to each [release](../../releases); the
step-by-step guide (including manual install and compatibility) is in
[INSTALL.md](INSTALL.md). From a source build:

**Windows** (asks for administrator rights, since the plug-ins folder is in
Program Files):

```powershell
pwsh -ExecutionPolicy Bypass -File scripts/install.ps1
```

**macOS**:

```sh
sh scripts/install-mac.sh /path/to/Flinch.plugin
```

Then restart After Effects and apply **Effect › Flinch › Flinch**.

## Disclaimer

- **Purpose.** This project is for educational and personal use: studying how
  a "twitch"-style effect behaves and practising After Effects plugin
  development. It is provided as is, without warranty of any kind.
- **No affiliation.** *Twitch* is a product of Video Copilot. *After Effects*
  is a trademark of Adobe. Flinch is not affiliated with or endorsed by either
  company.
- **No third-party code.** Flinch does not contain code, assets or binaries
  from Video Copilot's Twitch. The behaviour notes in `analysis/` describe how
  the original effect *behaves* (timing, ranges, formulas); the implementation
  was written from those notes, not copied from the original. The original
  plugin binaries are never committed to this repository.
- **Adobe SDK.** `third_party/ae_sdk/` contains files from the Adobe After
  Effects SDK, which remain under Adobe's license terms. They are included only
  so the project can be built; if this repository is ever made public, they
  must be removed and fetched separately by each developer.
- **Use responsibly.** If you own a licence for the original plugin, respect
  its terms. Do not use this project to circumvent anyone's licensing.
