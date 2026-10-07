# Startup module

The banner shown while the editor starts, and its intro animations. The module
is self-contained: it needs only Windows and GDI+, and nothing from the engine.
The engine uses it solely through `include/WindowsStartupBanner.h`:
`WindowsWindow.cpp` creates a `WindowsStartupBanner` with the artwork folder,
reports initialization stages with `SetStatus`, and destroys it when the editor
is ready.

Choosing an intro and supplying artwork is described in
`Assets/Startup/README.md`; this file covers how it works.

## Layout

```
Startup/
  CMakeLists.txt                     NyxStartup library and NyxStartupPreview tool
  include/WindowsStartupBanner.h     the public interface (and EStartupBannerMode)
  src/
    WindowsStartupBanner.cpp         window, frame timing, artwork, Startup.ini
    StartupBannerIntro.*             runs the intro chosen in Startup.ini
    StartupBannerFracture.*          chasm
    StartupBannerSoftwareIntro.*     connects CPU-drawn intros to GDI+
    SoftwareIntros/                  CPU-drawn intros (own library, see below)
  Preview/main.cpp                   NyxStartupPreview
```

`NyxStartup` is a static library linked into `NyxEngine`. `NyxStartupPreview`
compiles the same sources itself with `NYX_STARTUP_PREVIEW` defined, which adds
the benchmark and frame-export diagnostics; it borrows only the engine's
`Paths.cpp` to find the project's `Assets` folder.

## The banner window

The intro occupies a 1040x720 logical-pixel area, scaled for system DPI and reduced
to fit the selected monitor's work area when necessary. The completed banner is
640x360 within that area; `assemble_v2` instead plays within the banner's own
rectangle. Effects use per-pixel alpha via `UpdateLayeredWindow`;
classic retains its original drawing path. No fullscreen overlay is used.

Chasm is the exception: it spans the selected monitor's work area so the cracks
can cross the desktop. It leaves the taskbar area alone. Its opaque black backing
is intentional, preventing the real desktop from showing through the hole. Escape
hides this window immediately. Large displays cost more to render in this CPU implementation.

Reality cut, chasm, rift and assemble capture their rectangle once, immediately before showing the intro.
The capture stays in memory and is discarded after the reveal. It does not change
other windows or write desktop imagery to disk. Moving/video content briefly
freezes in the captured shards. Capture failure falls back to fracturing the
banner artwork. The intro cannot be dragged until the capture effect finishes.

Implementation: `WindowsStartupBanner.cpp`. This first version is Windows-only,
uses GDI+ (no extra downloaded dependency), and uses the system DPI for its size.

## CPU-drawn intros

Rift and assemble are drawn on the CPU instead of with GDI+: rift needs additive
light (glow, bloom, fire), which GDI+ cannot blend, and assemble moves tens of
thousands of tiles every frame. Both live in `src/SoftwareIntros/`:

- `SoftwareIntro.h` defines `ISoftwareIntro`, the small interface every such
  intro implements (prepare from the desktop and banner, render a frame, release).
- `StartupBannerRiftRenderer.cpp` and `StartupBannerAssembleRenderer.cpp` are the
  effects. They have no Windows dependencies.
- `StartupBannerSoftwareIntro.cpp` (one folder up) connects any of them to GDI+,
  and `StartupBannerIntro::Prepare` picks the renderer for the configured mode.

To add another CPU-drawn intro: implement `ISoftwareIntro` in `src/SoftwareIntros/`,
add the file to that folder's `CMakeLists.txt`, add a mode to
`EStartupBannerMode`, and create the renderer in `StartupBannerIntro::Prepare`.

The `SoftwareIntros` library is compiled with optimizations in every
configuration, Debug included, so the intros keep their frame rate while you
debug the editor. Rift spreads its full-frame passes over at most four threads
(half the cores, one on machines with fewer than four) and uses SSE2 on x64.
Both prepare their imagery once before the window appears, then release the
capture and everything derived from it when the intro ends.

## Preview tool

To review the animation without initializing Vulkan, run `NyxStartupPreview`; it
is built alongside the editor. Double-click the executable from any working directory:
it finds the project's Assets folder by searching upward from the executable,
just like the editor. Alternatively, pass an artwork directory as its first
argument. Use `--print-artwork-path` to print the default resolved directory
without opening a window. The preview closes automatically after 21 seconds;
its stage labels are marked as previews. Normal startup has no such delay.

Artwork is cropped and resized once for the display DPI; subsequent frames reuse
the small cached tiles and drawing buffer. Animation targets 60 FPS using a
deadline-based timer, including drawing time in each frame's budget.

Run `NyxStartupPreview.exe --benchmark` for a headless CPU drawing benchmark at
100% and 200% scale using this folder's images. An optional second argument
selects another artwork directory. Results exclude window presentation and are
not a measurement of the displayed frame rate.

Combine `--benchmark` with any intro flag, such as `--rift` or `--assemble`, to measure each
intro across its full sequence, including the transition to live artwork.
`--print-mode` reports the effective setting without opening a window.

`--lightning --export-frames <directory>` (or `--reality-cut` / `--chasm` / `--rift` / `--assemble`) writes 216 alpha-channel
PNGs, a contact sheet and a `preview.html` player with a timeline scrubber. These
diagnostics always use a synthetic desktop; they never capture the real screen.
The 60 FPS exporter checks final background transparency and banner opacity,
and verifies that reality-cut sample pixels match the desktop during the
recombined hold before the finishing impact.
Chasm additionally checks that the collapsed opening is opaque black before the
banner reveal. Its diagnostic export uses the standard 1040x720 demo canvas;
actual window playback uses the monitor's work-area dimensions.
Rift checks that the sky is still intact and cold at 1.60 seconds and that the
breach is open, opaque and molten at 2.20 seconds.
