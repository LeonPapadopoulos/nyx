# Startup artwork

Place PNG or JPEG artwork in this directory to customize the Nyx startup banner.
The first eight images in filename order form two gently drifting rows. Landscape
images work best; artwork is cropped to fill each tile. With no images here, the
banner uses the existing skybox faces, or muted gradient tiles if those are absent.

The banner runs independently of Vulkan while the editor initializes. Its labels
report actual startup stages; the moving line indicates activity, not percentage
completion. Press Escape while the banner is focused to hide it. Initialization
continues normally. It fades away when initialization finishes, without imposing
a minimum display time. The main editor then opens.

Implementation: `WindowsStartupBanner.cpp`. This first version is Windows-only,
uses GDI+ (no extra downloaded dependency), and uses the system DPI for its size.

To review the animation without initializing Vulkan, explicitly build the
`NyxStartupPreview` target. Double-click the executable from any working directory:
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
