# Frame statistics and benchmarks

How Nyx measures a frame, shows it, and records it, and why. The code is in
`Engine/Source/Runtime/Render` (`RenderFrameStats`, `SceneViewFrameStats` in `SceneViewTypes.h`,
`VulkanRenderer::CompleteFrameStats()`), `Engine/Source/Runtime/App` (`FrameStats.h`,
`FrameBenchmark.h`, `Application::GetFrameStats()`) and `Game/Source` (`FrameStatsOverlay`,
`BenchmarkLayer`).

## The shape of it

- **The renderer measures each frame** and keeps the numbers in place
  (`IRenderer::GetFrameStats()`, one view's with `GetSceneViewFrameStats()`):
  - on the CPU, the whole frame (from the end of the frame before to this one's present, so it
    holds everything the application did), extracting the worlds, culling (the views' cameras,
    their culling and their shadow cascades' culling), recording the scene views (the editor's
    passes, then each view's passes), the scene's work in all (with the uploads and the pictures'
    blur), the UI (the layers' `OnUI()`, ImGui, the painter), waiting for the frame before's fence,
    and acquiring and presenting;
  - on the GPU, by timestamps: the whole command buffer, the scene views together, and each view's
    passes: its shadow cascades, its scene pass (objects, sky, decals, and with MSAA the resolve
    at its end), its ambient occlusion, its particles, its bloom, its picture (the tonemap and what
    is drawn over it); and the samples a pixel the views draw with;
  - what each view drew, counted as it culls (`CullDraws()`): its scene pass's draw calls (one for
    each section of each draw), those of more than one instance, the instances drawn and culled
    (an entity out of view one, a cell out of view all of its instances), the triangles submitted
    (every instance's); its shadow cascades, and their draw calls and triangles; its decals; the
    point lights it lit its pixels with (neither culled nor past its most); the particle emitters
    it drew and their particles.
- **A frame is complete one frame late.** Its GPU times can be read only once its fence has
  passed, at the start of the next frame, so `CompleteFrameStats()` runs there and makes the whole
  record at once: the frame's CPU times (kept by the UI backend and the renderer), its views'
  counts (still the frame's: the next frame culls after this) and the GPU times just read. Every
  number in one record is of the same frame. `RenderFrameStats::Frame` counts the frames, so a
  reader sees when a new one is there.
- **The application adds its own times** (`Application::Run()`): every layer's `OnUpdate()` and
  `OnLateUpdate()`, and the fixed steps with how many there were, which is the simulation's time.
  It keeps them for the frame drawn last and, once the renderer completes that frame, makes one
  `FrameStatsSample`: a fixed array of numbers by `EFrameStat` (`MakeFrameStatsSample()`), the
  scene views' summed. A layer reads them with `Application::GetFrameStats()`.
- **Windows of about a second** (`FrameStatsWindow`): the samples are summed until their frame
  times add up to a second; then the window's average and worst (the highest of each number) come
  out, and stay until the next window closes, so what is shown holds still long enough to read. The
  last 4096 frames' CPU and GPU times are kept for a graph. Adding a frame copies numbers into
  fixed arrays: nothing is allocated, before or after the first frame.
- **The overlay** (`DrawFrameStatsOverlay()`, F8 in the game, every build, or `--frame-stats` from
  the start): an ImGui window at the picture's top left that takes no input. The frame rate and
  time with the worst, the CPU's and the GPU's breakdown, the counts, and a graph of the last five
  seconds' frames, each as wide as it took, the CPU's frame time in white and the GPU's in orange,
  with lines at 60 and 30 frames a second. ImGui, because the game draws its UI with ImGui.
- **The editor** shows the same window's average in its status bar (the frame rate and time), and
  the breakdown in the frame time's tooltip, rather than the overlay over its Game View: in the
  editor a frame is the editor's, every panel and every view of it, so the frame's numbers belong
  to the window's frame, not to one view. Play runs `NyxGame.exe`, which has F8.

## Benchmark mode

```
NyxGame.exe <scene> --benchmark <seconds> [--benchmark-out <file.csv>] [--benchmark-warmup <seconds>]
            [--benchmark-camera <file>]
```

- `BenchmarkLayer` turns vsync off, waits for the scene to settle (`--benchmark-warmup`, 3 s by
  default, and then until no shader compiles), records every frame's sample until the frames add
  up to the seconds asked for, writes the CSV (`--benchmark-out`, else
  `Saved/Benchmarks/<scene>-<date>-<time>.csv`), logs a summary line and quits with exit code 0; 1
  when it couldn't (a camera path it can't read, a file it can't write, no frame drawn).
- **The CSV** (`MakeFrameBenchmarkCsv()`): a header (`frame`, `time_s`, then each number's name,
  `frame_ms` to `particles`), a row a frame (its number from 0, the seconds from the recording's start
  to its end, its numbers: times with three decimals, counts whole), then three rows whose frame is
  `average`, `1% low` and `worst`. The 1% low is each number's 99th percentile by nearest rank: the
  value only the slowest 1% of the frames exceed. Every number is a time or a count, so higher is
  slower, and the frame rate's 1% low is 1000 over the frame time's.
- **The camera**, so every run sees the same:
  - with `--benchmark-camera`, a path: a text file of keys, one a line,
    `<seconds> <x> <y> <z> <look-at x> <look-at y> <look-at z>`, in time order, `#` starting a
    comment. The benchmark makes a camera of its own (the scene's primary camera's lens), makes it
    the only primary one, and moves it in a straight line from key to key by the seconds since the
    recording began; before the first key it is at the first, after the last at the last;
  - else, with several cameras in the scene, each is primary for an equal share of the recording,
    in the scene's order;
  - else the scene's primary camera, as the scene moves it (an orbit camera follows its target).

  For example, a pass over a valley and back:

  ```
  # seconds   position      look-at
  0           -40 12 -40    0 0 0
  10           40 12 -40    0 0 0
  20          -40 12 -40    0 0 0
  ```
- **The simulation runs as it always does**: the fixed steps of the real time, the game module's
  systems, the player's input (none comes in a benchmark). A seeded scene repeats, but not to the
  frame:
  - how many fixed steps a frame runs follows the real time, so the steps fall in other frames on
    every run, and physics, which runs once a step, moves the same but costs its time in other
    frames;
  - the camera's path follows real time too: the same pose comes at the same second, not at the
    same frame;
  - without vsync the frame rate is as fast as the machine draws (mailbox; where the device has no
    mailbox, the swapchain stays in FIFO, under vsync), and other programs, the GPU's clocks and
    its temperature change it from run to run. Compare averages and 1% lows over runs of ten
    seconds or more, not single frames.
- **Recording costs** a sample a frame copied into a vector reserved for a thousand frames a second;
  the overlay is off unless `--frame-stats` asks for it, so its UI isn't measured.

## Why the numbers are what they are

- **Draw calls, not draws**: a draw of the culled list (`SceneViewCullingStats::Draws`) is one
  for every section of its mesh, each with its own material's pipeline; `DrawCalls` is what the
  command buffer gets. The sky, the tonemap and the overlays aren't counted: they are the same few
  every frame.
- **Every scene view drawn is summed.** A frame's cost is all of its views'; one view's numbers
  are there for whoever wants them (`GetSceneViewFrameStats()`). A view not drawn in the frame
  (one drawn only when shown, and not shown: `SetSceneViewDrawnWhenShown()`) adds nothing and
  isn't counted; its own numbers are zero with `bDrawn` false. The renderer's own first view is
  never drawn (Findings below), so the game's frame is its one view's.
- **The MSAA resolve has no time of its own.** The multisampled scene is resolved by the scene
  render pass itself, into its resolve attachments as the pass ends, so its cost is in
  `gpu_scene_ms`; timing it apart would take a pass of its own, slower than the resolve. Compare
  runs with `--msaa 1` and `--msaa 4` to see what multisampling costs, the sample count being in
  every row (`msaa_samples`).
- **GPU passes are timed from the end of the work before them** to their own end (bottom of the
  pipe), as the bloom and the ambient occlusion were before: so the passes of a view add up to its
  time, gaps and all.
- **Times are floats, samples doubles**: triangles go past 2^24 in a big scene, and a double holds
  every count exactly.

## The numbers

The engine's test scenes, from their orbit cameras, 10 s each after 3 s, Release, 1280 by 720,
4x MSAA (the default), RTX 3070 Ti, both views summed (the game's and the renderer's first, 1 by
1, which was still drawn then: see Findings for the numbers since): the ground test (`GroundTest.nyxscene`: crates, a character, grass scattered, the sun's four
cascades), the atmosphere test (the same ground with small cubes scattered, fog, bloom, ambient
occlusion), the lights test (it at dusk with point lights) and the particles test (the lights test
with emitters). Averages:

| | Ground | Atmosphere | Lights | Particles |
| --- | --- | --- | --- | --- |
| GPU frame | 1.02 ms | 2.01 ms | 1.85 ms | 1.94 ms |
| GPU shadows, scene, occlusion, particles, bloom | 0.57, 0.34, 0, 0, 0 | 1.23, 0.52, 0.11, 0, 0.06 | 1.10, 0.50, 0.10, 0, 0.06 | 1.13, 0.50, 0.11, 0.04, 0.05 |
| CPU extract, cull, record | 0.011, 0.017, 0.081 ms | 0.018, 0.026, 0.128 ms | 0.018, 0.025, 0.137 ms | 0.032, 0.033, 0.152 ms |
| Draw calls (instanced) | 8 (4) | 17 (11) | 16 (10) | 15 (9) |
| Instances (culled) | 3400 (2696) | 19 354 (29 334) | 19 109 (29 579) | 20 049 (28 639) |
| Triangles: scene, shadows (8 cascades) | 110 k, 506 k | 304 k, 1.57 M | 301 k, 1.56 M | 312 k, 1.61 M |
| Point lights, particles (emitters) | 0, 0 | 0, 0 | 12, 0 | 14, 325 (6) |

- The frame times of these runs (6.8 to 13 ms) were set by other programs drawing on the same GPU
  at the time: `wait_for_gpu_ms` was most of each frame, while the game's own GPU work stayed at 1
  to 2 ms. Exactly what the columns are there to tell apart; compare frame times only between runs
  on a quiet machine.
- Shadows are the largest part of the GPU's frame in every scene, and the shadow pass draws five
  times the triangles the scene pass does: every cascade draws what casts into it, in both views.
- Particles and point lights cost little here: 0.04 ms for 325 particles, and the twelve lights
  show in the scene pass (0.50 against 0.52 ms without them, within the noise).

## Findings

What the numbers showed, and what came of it.

- **The game drew two scene views** (`scene_views` 2, `shadow_cascades` 8). The renderer makes a
  view of its own as it starts, 1 by 1 pixels, whose render passes the scene's pipelines are made
  from, so that every other view can come and go; it was drawn every frame, through the editor's
  default camera, with all four cascades of the sun's shadows, though nobody showed it. Now a view
  can be drawn only when shown (`IRenderer::SetSceneViewDrawnWhenShown()`): in the frames after one
  whose UI asked for its picture (`GetSceneViewTextureId()`, `GetSceneViewBlurTextureId()`,
  `WasSceneViewRecreatedThisFrame()`); else it doesn't cull, cast shadows, draw its passes or the
  editor's, or blur its picture. The renderer's own view is such a view and nobody asks for it, so
  it is never drawn; `GetSceneViewIds()` leaves it out. The editor's scene views and the material
  preview are drawn so too: a view in a tab behind another, the scene views while the Materials
  workspace shows, the preview while it doesn't, cost nothing. Shown again, a view shows the
  picture it drew last for one frame, then that frame's; if its targets were made again while it
  was hidden (a new sample count), it has no picture until it is drawn, which
  `WasSceneViewRecreatedThisFrame()` says, as for a view just made. Its ambient occlusion and
  particles keep framebuffers of its targets: they are made again at its next draw
  (`bTargetsNotDrawn`), not only in the frame the targets were. Views not shown that way are drawn
  every frame, as tests reading a view back need.

  The same machine, the same ground and atmosphere tests as above, two runs each, another program
  using part of the GPU in both (`wait_for_gpu_ms` a millisecond and more of each frame), averages:

  | | Ground before | Ground after | Atmosphere before | Atmosphere after |
  | --- | --- | --- | --- | --- |
  | Scene views, shadow cascades | 2, 8 | 1, 4 | 2, 8 | 1, 4 |
  | GPU frame | 1.22 ms | 0.70 ms | 2.02 ms | 1.32 ms |
  | GPU shadows, scene | 0.62, 0.40 ms | 0.34, 0.28 ms | 1.24, 0.53 ms | 0.58, 0.39 ms |
  | Triangles: scene, shadows | 121 k, 543 k | 63 k, 278 k | 312 k, 1.62 M | 181 k, 869 k |
  | CPU cull, record | 0.016, 0.073 ms | 0.010, 0.047 ms | 0.024, 0.118 ms | 0.015, 0.083 ms |
  | Frame (fps) | 1.84 ms (544) | 1.29 ms (778) | 2.74 ms (365) | 1.95 ms (513) |

  The 1 by 1 view drew half the triangles, through a camera of its own: its size made its scene
  pass cheap, not its shadows. The ambient occlusion and bloom, unchanged, measured a little slower
  after (0.11 to 0.15 ms, 0.05 to 0.12 ms in the atmosphere test): passes that small move with the
  GPU's clocks and the other program's share from run to run.
- **Frames spent in the UI step** (`ui_ms`): up to 36 ms while another program loaded the GPU,
  6 to 15 ms on a quieter one, a few a minute in the atmosphere test with the overlay on. Timed
  step by step, with the thread's own cycles beside the clock, they were
  two things, and in both the thread waited rather than worked (an 11 ms step used 1 ms of
  cycles):
  - **ImGui's Vulkan backend making its buffers again.** Its vertex and index buffers, a pair for
    each swapchain image, are freed and allocated again, at exactly the size needed, whenever a
    frame draws more than they hold; that took 5 to 7 ms each time. The overlay's graph has as
    many points as frames in five seconds, so its vertex count rose to a new peak now and then
    for as long as it ran. Nyx now has the backend allocate at least a megabyte
    (`ImGui_ImplVulkan_InitInfo::MinAllocationSize`, `VulkanImGuiBackend::Initialize()`): about
    50 000 vertices, more than the game's UI draws, so they are made once.
  - **Windows calls ImGui's GLFW backend makes every frame**, which now and then wait 2 to 10 ms
    on the system: its monitors' positions, video modes, work areas and scales
    (`ImGui_ImplGlfw_UpdateMonitors()`, every frame since ImGui stopped using GLFW's monitor
    callback), and each window's mouse pass-through set again (`glfwSetWindowAttrib()` with
    `GLFW_MOUSE_PASSTHROUGH`, in `ImGui_ImplGlfw_UpdateMouseData()`). Both are in
    `Dependencies/imgui`, not Nyx; it would take a change there (asking for the monitors only
    when they change, setting the pass-through only when it changes) or upstream.

  Runs of 60 s with the overlay on, frames with over 5 ms of UI: before, 42 in 12 runs (1 to 6 a
  run, the worst 14.6 ms); after, 1 in 3 runs (9.8 ms, the GLFW backend's). Texture uploads (ImGui's font atlas), the submit, the layers' own UI and ImGui's
  layout took no part, nor did saving ImGui's settings: the game saves none (`IniFilename` is
  null), the editor's layout files being the editor's alone.
