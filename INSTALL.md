# Installing Flinch

Flinch is tested on After Effects 2026 for Windows. It is expected to work on
After Effects 2022 or later; see [Compatibility](#compatibility).

## Windows

1. Download `Flinch-windows.zip` from the release and unzip it.
2. Close After Effects.
3. Create this folder (adjust the year to your version of After Effects):
   `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Flinch`
4. Copy `Flinch.aex` into it. Windows will ask for administrator permission.
5. Open After Effects and apply **Effect › Flinch › Flinch** to a layer.

If you built Flinch from source, `scripts/install.ps1` does steps 3–4 for you.

## macOS (Apple Silicon and Intel)

1. Download `Flinch-macos.zip` from the release and unzip it. You get `Flinch.plugin`.
2. Close After Effects.
3. Open Terminal in the folder that contains `Flinch.plugin` and run:

   ```sh
   xattr -dr com.apple.quarantine Flinch.plugin
   sudo mkdir -p "/Applications/Adobe After Effects 2026/Plug-ins/Flinch"
   sudo cp -R Flinch.plugin "/Applications/Adobe After Effects 2026/Plug-ins/Flinch/"
   ```

   The first command removes the "downloaded from the internet" flag; without
   it macOS may refuse to load the plugin, because the build is only ad-hoc
   signed (not notarized). Adjust the year to your version of After Effects.
4. Open After Effects and apply **Effect › Flinch › Flinch** to a layer.

If you have the repository, `sh scripts/install-mac.sh /path/to/Flinch.plugin`
does step 3 for you.

## Uninstalling

Delete the `Flinch` folder inside the After Effects `Plug-ins` folder.

## GPU or CPU

Flinch renders on the GPU when the project does (*File › Project Settings ›
Video Rendering and Effects*: Mercury GPU Acceleration with CUDA, OpenCL or
Metal). Set **Render Using** to **CPU Only** to force the CPU. With
"Mercury Software Only", or if the GPU kernels cannot load, Flinch uses the CPU
automatically.

## Compatibility

| | Status |
|---|---|
| After Effects 2026, Windows x64 | Tested |
| After Effects 2022–2025, Windows x64 | Expected to work, not tested |
| After Effects 2022+, macOS (Apple Silicon / Intel) | Builds and passes tests in CI; not tested inside After Effects yet |
| Older than After Effects 2022 | Not supported (no Multi-Frame Rendering; untested) |

Requirements: 64-bit After Effects; macOS 11 or later.
