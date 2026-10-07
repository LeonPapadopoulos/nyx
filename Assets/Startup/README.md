# Startup artwork

Place PNG or JPEG artwork in this directory to customize the Nyx startup banner.
The first eight images in filename order form two gently drifting rows. Landscape
images work best; artwork is cropped to fill each tile. With no images here, the
banner uses the existing skybox faces, or muted gradient tiles if those are absent.

The banner runs independently of Vulkan while the editor initializes. Its labels
report actual startup stages; the moving line indicates activity, not percentage
completion. Press Escape while the banner is focused to hide it. Initialization
continues normally. In classic mode it fades away when initialization finishes,
without imposing a minimum display time. The main editor then opens.

## Selecting the startup animation

Edit `Startup.ini` beside this README:

```ini
[Startup]
Mode=classic
```

- `classic`: the original scrolling banner (default).
- `lightning`: a descending precursor, white-hot amber discharge with branching
  arcs, an expanding shockwave and embers. Irregular banner fragments assemble
  in the aftermath before settling into the usual scrolling artwork.
- `reality-cut`: seven rapid blue-white blades split a snapshot of the desktop
  with short, glaring flashes. Pieces recoil in depth and then fully recombine
  at 2.16 seconds. After a brief still hold, a finishing impact lands at 2.32
  seconds with a radial flash, pressure ring and short shake. The intact desktop
  dissolves into the banner. Glass edges and foreshortening use a lightweight
  projection; the pieces stay together after recombination.
- `rift`: the desktop sinks into a storm-dark, steel-blue sky while ice-blue
  filaments gather into a point of light. On ignition a glass-like web of radial
  and concentric fractures spreads from that point and long cracks race across
  the sky behind white-hot tips. Pulses of light run outward through the whole
  network, quickening as a rumble builds; the world dims for a breath, then the
  sky shatters at 1.84 seconds: a starburst flash, a hard shake and a shockwave
  ripple. Gold-rimmed shards blast toward the viewer and reveal a molten, amber
  breach of splintered crystal and smoke, sprayed with embers. The banner rises
  out of the glow as everything else fades.
- `assemble`: the desktop breaks into small tiles. Tiles of matching brightness
  fly along gentle arcs and build the banner from its centre outward, taking on
  the banner's colours as they land; the remaining tiles drift away into a dark
  backdrop. This is a first, functional version with minimal visual tuning.
- `assemble_v2`: the same effect played within the banner's own rectangle:
  the window is exactly the banner's size, so the desktop behind the banner
  rearranges itself into the banner. `assemble-v2` is accepted too.
- `cinematic`: compatibility alias for `lightning`.
- `chasm`: a separate desktop-collapse sequence. Jagged cracks race across the
  selected monitor's work area, solidify, and carry moving blue-white highlights.
  An impact breaks the surface from the upper center outward; desktop shards and
  small debris rotate, shrink, darken and fall into opaque blackness. The Nyx
  banner appears afterward. This first version reveals blackness, not a live
  view of the editor underneath.

All cinematic effects last 3.4 seconds. A fast startup waits for the intro to finish;
Escape hides it and removes that remaining wait. Artwork then scrolls normally
for any remaining initialization time. These are silent procedural effects.

Missing settings or an unrecognized mode fall back to classic. Settings are read
on each launch. All modes use the same PNG/JPEG artwork in this directory.

The preview accepts explicit mode flags to override the file for one run:

```powershell
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --lightning
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --reality-cut
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --chasm
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --rift
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --assemble
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --assemble-v2
.\Build\Windows\Binaries\Debug\NyxStartupPreview.exe --classic
```

Reality cut, chasm, rift and the assemble modes start from a snapshot of the
desktop behind the banner window. The snapshot stays in memory, is discarded
when the intro ends and is never written to disk.

How the banner and its intros are built, how to add an intro, and the preview
tool's benchmark and frame-export options are described in `Startup/README.md`
at the root of the project.
