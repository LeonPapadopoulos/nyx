# Shaders and materials

Proposal written 2026-10-13, revised the same day: bindless from the start, one compiler and live reload for every shader, a physically based baseline, and reference scenes instead of today's three looks as the measure. The current shader setup was not designed with care, so nothing here is kept just because it exists. Second revision: steering material values from code, and later from scripts and animation. Since then, the *As built* sections at the end record each change and what it changed in the plan.

A place to write shaders and materials as code, see them live, and use them in 3D, 2D and the UI. No node graph; the design leaves room for one later as a front end that writes code.

Related: `Design/editor-ui-architecture.md` (3.1 workspaces, 3.2 documents, 3.10.1 the painter's custom command), `Design/ui-painter.md`, `Design/undo-by-guid.md`. Pictures: `EditorConcepts/Nyx-Editor-Shader-Workspace-v1.md` (prompts) and `EditorConcepts/Nyx-Editor-Workspaces-v1-Materials.png`.

## What a session should feel like

1. Pick *New Material* in the asset browser, choose *Surface*. A shader file and a material file appear; the material opens in the Materials layout, with a sphere in the preview.
2. The shader opens in VS Code at the function to fill in. Save: the preview changes in under a quarter of a second. So does every scene view showing the material, and every running game.
3. A typo: the preview keeps the last good picture under a red line naming the file, line and message. Click it, VS Code jumps there. Fix, save, the line goes.
4. Add a field `[Range(0, 1)] float Wetness = 0.3;` to the shader: a slider appears in the parameters. Drag it: the preview, the scene views and the games follow every frame, with no compile. Ctrl+Z puts it back.
5. Switch the preview from the sphere to the selected entity's mesh, to *Roughness* view, to half old / half new. Click a pixel and read what the shader computed there.
6. The same loop for a sprite, a post effect, the background of a painted panel, or one of the engine's own shaders: the painter's, the grid's, the blur's.
7. In the game, code makes one locked door glow red, makes every enemy that is hit flash, and makes the whole world wet when it rains, each with one call. Later, an animation track or a script does the same through the same path.

## What exists today

- **Materials:** `Nyx::Material` is a pipeline pointer plus `Reflectivity`, `bUseTexture` and `Tint`. The renderer makes three in code (textured, reflective, untextured). The `Materials/*.nyxmat` paths in `DefaultScene.cpp` are names only; no such files exist. `MeshRendererComponent::Material` is read-only in the details panel.
- **Shaders:** GLSL in `Engine/Shaders`, compiled by `glslc` at build time and copied next to each program. The grid shader alone reloads: the renderer checks the files' times every frame and runs `glslc` through `std::system`, on the main thread. Every other pipeline is made once, by hand, in `VulkanRenderer.cpp` (about 4,900 lines), `VulkanPainter.cpp` and `VulkanBlur.cpp`.
- **Bindings:** one descriptor set for the scene pipeline (0 scene uniforms, 1 texture, 2 skybox); push constants hold the model matrix, `uParams` and `uTint`. Descriptor indexing is not turned on. The painter binds a small fixed array of textures per draw.
- **Lighting and colour:** one directional light, ambient, and a skybox reflection. The skybox cubemap has one mip. Scene views render straight into `R8G8B8A8Unorm`: no HDR, no exposure, no tonemapping.
- **Meshes:** `Vertex` has position, colour, UV and normal; no tangent, so normal maps can't be lit correctly. The glTF importer reads only the base colour factor and texture of a material.
- **Scene views:** all of them render the one world set with `IRenderer::SetWorld()`.
- **Painter:** a fill mode field is reserved in every command; a *custom command* (a shader of the caller's choice, masked by a shape) is planned in the architecture doc, 3.10.1.
- **Editor:** documents with their own undo (3.2) and workspaces (3.1) are planned, workspaces "not before a second kind of document exists". A material is that second kind. `SourceNavigation` already opens a file at a line in VS Code or Visual Studio. The editor link already carries live edits to games.
- **Vulkan SDK 1.4.341.1** (installed) ships Slang (`slang.dll`, `slangc`, and the language server `slangd`), shaderc, SPIRV-Reflect and SPIRV-Tools. Nothing new to download. Vulkan 1.4 with dynamic rendering and extended dynamic state is already on.

## Recommendations

### 1. Slang, compiled inside the engine

Material shaders are written in **Slang** and compiled in process through Slang's API, to SPIR-V.

- **Composition without a preprocessor.** Modules (`import nyx.surface;`) instead of `#include`, and interfaces with generics: the engine writes the entry points once per domain, generic over "a surface", and specialises them with the user's type. The user's file holds only what they mean.
- **Reflection with attributes.** Slang reflects user attributes on fields (`[Range(0, 1)]`, `[Color]`, `[Category("Surface")]`). The parameters panel is generated from that, the way the details panel is generated from `NYX_PROPERTY` metadata. The same names are used on purpose.
- **Layouts come from the compiler, never by hand.** The CPU writes each parameter at the offset reflection reports. A C++ struct that mirrors a shader struct, and drifts from it, can't happen.
- **Bindless is part of the language** (descriptor heaps, `DescriptorHandle`), so a material's textures are plain fields of its struct (recommendation 3). Nyx wraps them in a `nyx::Texture` of its own (as built, change 1).
- **Specialisation constants** for switches (`[Switch] bool UseNormalMap`): flipping one makes a new pipeline, not a new compile.
- **Tooling:** `slangd` gives VS Code completion, go-to-definition and errors while typing. It is in the SDK.
- **Why not GLSL + shaderc + SPIRV-Reflect:** all in the SDK too. But composition would be `#include` and text templates, reflection has no attributes (parameter metadata would need comments parsed by hand), bindless is extension syntax, and there is no language server worth the name. It's the fallback if the spike in stage 1, change 1 goes wrong.

### 2. One compiler, one cache and live reload for every shader

The engine's own shaders (painter, grid, picking, selection mask, outline, blur, skybox) go through the same compiler, cache and file watcher as materials. Editing `Painter.frag` updates the editor's UI on save, the way editing a material updates the scene.

- **One front end.** Slang compiles the engine's existing `.vert`/`.frag` files too, in its GLSL mode, with the same diagnostics (columns included), dependency tracking (`#include` included) and cache as materials. No shaderc (as built, change 2). Engine shaders move to Slang proper one at a time, when they are next worked on.
- **Every pipeline is rebuildable.** Whoever owns a pipeline holds a `ShaderProgram` handle and rebuilds the pipeline when the program changes. The grid's hand-made reload is the first one replaced.
- **The build uses the same compiler.** The `glslc` step in `Engine/Shaders/CMakeLists.txt` is replaced by `NyxShaderCompiler build`, the live-reload compiler in a second mode, which fills the shader cache at build time. Build and runtime can't disagree about how a shader compiles, the editor starts with nothing to compile, and a game without a compiler loads from the cache.

### 3. Bindless from the start

Vulkan 1.4 makes this the simple path, not the advanced one:

- **One global descriptor set, bound once per frame:** an array of every sampled texture, a few fixed samplers, and one buffer with the values of every material.
- **One set per view:** scene uniforms and the environment.
- **Push constants per draw:** the model matrix, the material's offset in the material buffer, the entity ID.

What this buys:

- No descriptor set per material, no pool per material, no layout per shader. Surface, Sprite, UI and Post shaders all see the same bindings.
- A material is bytes at an offset. Changing a value writes those bytes; switching an object's material changes one push constant.
- All materials of one shader share one pipeline; only switches add pipelines.
- The painter can later take its images from the same array, which removes its texture limit per draw (left for later; not needed here).

It needs the Vulkan 1.2 descriptor indexing features (runtime arrays, partially bound, update after bind, non-uniform indexing) and `scalarBlockLayout` turned on in `VulkanContext` and required when choosing the GPU. Every GPU that runs Nyx today has them.

### 4. A shader is a code file; a material is a small text file of values

- **Shader** (`Assets/Shaders/**/*.slang`): code, a domain, parameters with defaults.
- **Material** (`*.nyxmat`): which shader, and values for some of its parameters. Everything not listed takes the shader's default.
- **Material instance:** a material whose `Parent` is another material instead of a shader. It overrides some values and inherits the rest. Stage 3.
- Text, so it diffs, merges and can be written by hand beside the shader. Scenes stay binary; materials are few and small.
- The glTF importer will write one `.nyxmat` per material of the file, on `Standard.slang`, once meshes are assets (see *Left for later*).

```ini
# Assets/Materials/WeatheredStone.nyxmat
Shader = Shaders/Environment/Stone.slang
BaseColor = 0.62 0.60 0.56
Roughness = 0.78
Albedo = Textures/Stone_Albedo.png
Normal = Textures/Stone_Normal.png
UseNormalMap = true
```

### 5. Four domains

A shader's domain says what the user writes, what the engine adds around it, and how it is previewed.

| Domain | The user writes | The engine provides | Previewed on |
| --- | --- | --- | --- |
| **Surface** (3D) | `SurfaceOutput shade(SurfaceInput)`: base colour, metallic, roughness, normal, occlusion, emission, opacity. Optional vertex offset. `[Unlit]` skips lighting | vertex shader, lighting, picking and selection variants, debug views | a mesh in a small lit scene |
| **Sprite** (2D) | `float4 shade(SpriteInput)`: UV, texture, vertex colour, pixel size, time | quad vertex shader, blending modes, pixel-snapping | a sprite on a checker, tiled, 1× to 8× with a pixel grid |
| **UI** | `float4 shade(UIInput)`: position in the shape, distance to its edge, shape size, theme colours, time | the shape's outline, clip, anti-aliasing, through the painter's custom command | painted cards and buttons at 100% and 150%, on dark and light backdrops |
| **Post** | `float4 shade(PostInput)`: scene colour (HDR), depth, UV, time | full-screen pass in a scene view's post stack, before tonemapping or after it | the preview scene, or any open scene view |

Compute, decals and particles come later as domains of their own.

### 6. A physically based baseline, measured against references

Materials are only as good as the lighting they are judged under. Stage 1 sets a baseline that the `Standard` shader and every surface material are written against, and measures it against references instead of against today's look.

- **Surface model:** glTF's metallic-roughness: base colour, metallic, roughness, normal, occlusion, emission, opacity (opaque, masked, blended). What the importer brings in is what the shader takes.
- **Direct light:** GGX distribution, height-correlated Smith visibility, Schlick Fresnel, Lambert diffuse, for the one directional light.
- **Image-based light** from the environment: specular prefiltered by roughness into the cubemap's mips, diffuse irradiance as spherical harmonics, a split-sum BRDF lookup table made once.
- **HDR and tonemapping:** scene views render into `R16G16B16A16Sfloat`, with exposure, and are tonemapped into the 8-bit picture the editor shows. The editor's overlays (grid, debug lines, outline) draw after tonemapping, so they look as they do now.
- **Tangents:** `Vertex` gets a tangent. The built-in meshes have theirs written out; imported meshes will take glTF's, or MikkTSpace's when the file has none (with mesh assets, see *Left for later*).
- **References:** a material test scene (rows of spheres from metallic 0 to 1 and roughness 0 to 1) with approved reference images, and checks with known answers: the furnace test (under a uniform sky, a white dielectric sphere shows the sky's colour), and values computed on the CPU from the same environment. A GPU test renders it and compares. glTF sample models beside the glTF Sample Viewer's renders come with mesh assets.
- **Not in this plan:** more lights, shadows, a light component of its own. `SurfaceOutput` is the boundary: when lighting grows, materials don't change.

### 7. External code editor first, a built-in one later

- **Stages 1 to 3:** shaders open in VS Code (with `slangd`) or Visual Studio, through the existing `SourceNavigation`. Saving is the trigger. The best editing there is, for no work.
- **Stage 4:** a painted code editor inside Nyx, for what an external editor can't do: compile as you type with no save, errors under the line, and dragging a number in the code to change it live (the same for colours). It uses the painter's text and the multi-line field that exist.

### 8. Games get compiled code, not source

The editor compiles and sends SPIR-V and the reflected layout over the editor link. A game needs no compiler for live edits, and shows exactly what the editor shows, including unsaved text from the built-in editor later. Parameter changes travel as bytes for the material buffer, like property edits do now.

### 9. Steering at four levels, all addressed by path

Material values change at runtime at four levels, each for its own job. Code reaches all four through one API; animation and scripts, when they exist, reach them through paths. Details under *Steering materials at runtime*.

## How a shader looks

```slang
// Assets/Shaders/Environment/Stone.slang
import nyx.surface;

struct Stone : ISurface
{
    [Category("Surface"), Color]       float3 BaseColor = float3(0.62, 0.60, 0.56);
    [Category("Surface"), Range(0, 1)] float  Roughness = 0.78;
    [Category("Surface"), Range(0, 1)] float  Metallic  = 0.0;

    [Category("Textures"), Default("White")]      nyx::Texture Albedo;
    [Category("Textures"), Default("FlatNormal")] nyx::Texture Normal;
    [Category("Textures"), Switch] bool UseNormalMap = true;

    [Category("UV"), DragSpeed(0.01)] float2 Tiling = float2(1, 1);

    SurfaceOutput shade(SurfaceInput input)
    {
        float2 uv = input.UV * Tiling;
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = Albedo.Sample(nyx::LinearWrap, uv).rgb * BaseColor;
        if (UseNormalMap)
            o.Normal = nyx::normalFromMap(Normal.Sample(nyx::LinearWrap, uv), input);
        o.Roughness = Roughness;
        o.Metallic = Metallic;
        return o;
    }
}
```

- **Parameters are the struct's fields.** The struct is stored in the material buffer at the material's offset; the domain's entry point loads it from there and calls `shade`.
- **Textures are indices.** `nyx::Texture` is a struct holding one `uint`: an index into the global texture array, slot 0 when unset, with `Sample` and `SampleLevel` like a texture. Not Slang's `DescriptorHandle<Texture2D>`, which has no default and loads from the wrong offset inside a struct (as built, change 1). The material file names an asset; the engine writes its index.
- **Per-object data and globals** are read the same way in every domain: `input.CustomData` (a `float4` set per object by code) and `nyx::globals.Wetness` (a project-wide value). See *Steering materials at runtime*.
- **Attributes:** `Category`, `Tooltip`, `DisplayName`, `DragSpeed` (as in `NYX_PROPERTY` meta), plus `Range(min, max)`, `Color`, `HDR`, `Default(...)` for textures, `Switch` for specialisation constants, `Enum("A, B, C")` for an int shown as a combo, `Hidden`.
- **Shared code** lives in modules under `Engine/Shaders/Library/nyx/`: `surface`, `lighting`, `sprite`, `ui`, `post`, `color` (sRGB, tonemapping, luminance), `noise` (value, gradient, simplex, Voronoi, fbm), `sdf` (2D shapes, matching the painter's), `math`. Project modules live beside the shaders and are imported the same way.

## Runtime

### Where it lives

- `Engine/Source/Runtime/Renderer`:
  - `ShaderCompiler.h/.cpp`: Slang, for materials and for the engine's GLSL; reflection, diagnostics, dependencies, the defaults writer. No Vulkan, so tests run without a GPU.
  - `ShaderLayout.h`: what reflection found, as plain data: parameters (name, type, offset, size, attributes), texture handles, switches (constant ID), entry points.
  - `ShaderCache.h/.cpp`: SPIR-V and layouts on disk.
  - `ShaderPrograms.h/.cpp`: programs by name, compile results swapped in at the start of a frame, change notifications to pipeline owners.
  - `VulkanBindless.h/.cpp`: the global set: texture array and its free slots, samplers, the material buffer.
  - `VulkanEnvironment.h/.cpp`: the prefiltered cubemap, irradiance and BRDF lookup table, made from the skybox when it loads.
  - `MaterialAsset.h/.cpp`: the `.nyxmat` file, reading and writing, values by parameter name, the parent chain.
  - `VulkanMaterials.h/.cpp`: materials on the GPU (offset, bytes, pipeline per shader and switches). `Nyx::Material` becomes a handle into it, and the three materials in code go away.
- `Engine/Source/Runtime/Engine`:
  - `MaterialSystem.h/.cpp`: the steering API (`FindParam`, `Set`, `SetOverride`, `SetGlobal`, custom data), per-entity overrides, globals, paths. No Vulkan; it hands bytes to `VulkanMaterials`.
  - `MaterialOverrides.h`: the reflected list of overrides on `MeshRendererComponent` (and `SpriteComponent`).
- **Boundary:** all of this lives in its own files behind small interfaces. `VulkanRenderer` asks for a pipeline and a material offset and records draws; no new code grows inside `VulkanRenderer.cpp`.
- `Engine/Source/Runtime/Core`: `FileWatcher.h/.cpp` (`ReadDirectoryChangesW`).
- `Engine/Source/Runtime/Renderer`: `ShaderCompileService.h/.cpp`: compiling in `NyxShaderCompiler.exe` (`Tools/NyxShaderCompiler`) or in process.
- `Engine/Shaders/Library/nyx/*.slang`, copied next to each program.
- `Tools/NyxShaderCompiler`: compiles for live reload, and in its build mode fills the cache at build time.
- `Editor/Source`: the material document and its panels.

### Compiling

- **In a process of its own** (`NyxShaderCompiler.exe`, as built, change 3). A save queues a request; results are swapped in at the start of a frame, and the frame never waits for a compile. Pipelines of changed programs are made on the main thread at the start of the next frame, after the GPU is idle; making them on a worker is left for when materials make many at once.
- **Last good stays.** A failed compile changes nothing on screen but the error line. A material that never compiled shows a magenta checker; an engine shader that never compiled is a startup error with file and line, as a missing `.spv` is today.
- **Diagnostics** are kept as file, line, column, severity and message, not as one string, so the log and (later) the built-in editor can point at them.
- **Dependencies:** each result lists the files it imported or included. Saving `nyx/lighting.slang` recompiles every shader that imports it, and only those.
- **Cache:** key = hash of the sources of all dependencies, the compiler's version, the kind of source (material, Slang program, GLSL), the domain and the target. A `VkPipelineCache` is saved beside it.
- **Saves come in bursts** (editors write temp files, rename, write twice). Changes are gathered for 50 ms before compiling; a file that can't be read yet is tried again.

### Binding

- **Set 0, global:** binding 0 one array of every sampled image, 2D and cube alike (update after bind, partially bound); binding 1 the fixed samplers (`LinearWrap`, `LinearClamp`, `PointClamp`, `Anisotropic`); binding 2 the material buffer. **Set 1, per view:** scene uniforms, environment indices, exposure, the global parameters. **Push constants:** model matrix, material offset, entity ID, custom data (`float4`); 96 bytes with padding (as built, change 5) of the 128 every GPU guarantees.
- **Texture slots:** a loaded texture takes a free slot; a freed slot is reused only after the frames in flight that could still read it have finished. Slots 0 to 3 are fixed defaults: white, black, flat normal, magenta checker.
- **Defaults:** a new material's bytes are its shader's defaults. Reflection gives them for scalars only, so the engine generates a small compute shader per material shader that stores the struct made with its defaults (`Store<M>(0, M())`), runs it once, and keeps the bytes with the compiled shader. A field without a default is 0 (the session zero-initialises).
- **Material buffer:** each material's struct at a 16-byte aligned offset, laid out as reflection reports (scalar layout). One copy per frame in flight; a changed material is written into the copy of the frame being recorded, so the change shows in the same frame. No compile, no new pipeline.
- **Changing the shader's fields:** after the compile, the material gets a new size and offset, and values are carried over by name. Values whose type changed, or whose field is gone, are dropped, and the log lists them; the material file keeps them until it is saved, so undoing the shader edit brings them back.

### Passes

- A surface material takes part in the main pass, picking and the selection mask, all on the same sets. Picking and the mask use the engine's own shaders, unless the material moves vertices or discards pixels (masked opacity); then they use a variant built from the material's code, so outlines and clicks match what is drawn.
- Pipelines are made with dynamic rendering and extended dynamic state, so cull mode, depth test and so on don't multiply them.
- Order in a scene view: scene (HDR) → post materials before tonemapping → tonemap → post materials after it → the editor's overlays.

## Steering materials at runtime

Nyx has no scripting or animation system yet. This section defines what they will plug into, and gives game code the same reach now.

### Four levels

| Level | Affects | Example | How it is stored |
| --- | --- | --- | --- |
| **Material** | every object using it | all lanterns glow brighter at night | the material's bytes in the material buffer |
| **Per-entity override** | one object | this door turns red while locked | a copy of the material's bytes at an offset of the entity's own; same pipeline |
| **Custom data** | many objects, each its own value | every enemy that is hit flashes | a `float4` per object in the push constants |
| **Global parameters** | every shader that reads them | world wetness, wind, time of day | one buffer in the per-view set |

- **Per-entity overrides** are the reason bindless pays off here: an override is another offset, with no descriptor set and no pipeline of its own. The entity's bytes are the material's with the overrides applied on top; when the material changes, they are made again, so values that aren't overridden follow it. The copy is freed when the last override goes or the entity is destroyed.
- **Custom data** is for many objects with a value each, where an override per object would copy whole materials. The shader decides what the four numbers mean (`input.CustomData.x` as hit flash, `.y` as team colour index). Every domain gets it; for UI materials it comes with the painter command.
- **Global parameters** are declared once per project, as a struct in `Assets/Shaders/Globals.slang` with the same attributes as material fields. Shaders read them as `nyx::globals`. Reflection gives the editor a *Global Parameters* panel, and the values are saved with the project. Changing one doesn't recompile anything; adding one recompiles the shaders that read globals.

### The API

`MaterialSystem`, in the engine, for game code now and for scripting bindings later:

```cpp
MaterialParam wetness = Materials.FindParam(stone, "Wetness"); // the name is looked up once
Materials.Set(stone, wetness, 0.8f);                    // the shared material
Materials.SetOverride(lanternEntity, wetness, 0.8f);    // this entity only
Materials.ClearOverride(lanternEntity, wetness);
Materials.SetCustomData(enemyEntity, { hitFlash, 0.0f, 0.0f, 0.0f });
Materials.SetGlobal("Wetness", 0.6f);
```

- `MaterialParam` holds the offset and type; it stays valid across recompiles (it is found again by name), and a parameter that disappears makes later `Set` calls do nothing and log once.
- Values are typed: `float`, `float2..4`, `int`, `bool`, colours, textures (by asset). Setting the wrong type is an error, reported once.
- Writes land in the copy of the material buffer for the frame being recorded, so any number per frame is fine; only changed ranges are uploaded.
- Changes made at runtime never write back to assets.

### Paths

Every steerable value has a path, built like the reflected property paths of the details panel:

- `Lantern/MeshRenderer/Material:Wetness`: an entity's override;
- `Lantern/MeshRenderer/CustomData.x`: an entity's custom data;
- `Materials/WeatheredStone.nyxmat:Wetness`: the shared material;
- `Globals:Wetness`: a global.

`MaterialSystem::Resolve(path)` turns a path into a target once; setting it afterwards is as fast as the API. Animation tracks and script bindings bind to paths, so neither needs to know about materials, and a track can animate a material value and a transform the same way.

### In the editor

- **Overrides are reflected.** `MeshRendererComponent` gets a `MaterialOverrides` property: a list of name and typed value. The details panel shows it with a widget built from the material's layout (the same widgets as the parameters panel, with only overridden values set, and a button to add one). Undo, saving with the scene and live edits to games come from the reflection pipeline as they do for every property. New reflection work: the typed list, and its widget in `PropertyWidgetRegistry`.
- **Custom data** is a plain reflected `glm::vec4` on the same component, so it can be set by hand to try a shader.
- **An override whose parameter is gone** stays stored and inactive, shown greyed out with a remove button, and the log lists it.
- **The Global Parameters panel** edits the globals live in the editor and every game.
- What a running game has set at runtime is not shown in the editor; that direction is left for later.

## Editor

### The material document

- `MaterialDocument` implements `IEditorDocument` (architecture 3.2): file, values, dirty state, its own undo history. Edits are transactions, so dragging a slider is one undo step, as in the details panel.
- Opening a material makes it the active document. Shader files are not documents in Nyx until stage 4; they belong to the external editor.
- Assigning: drag a material onto an entity in a scene view or onto the field in the details panel. `MeshRendererComponent::Material` stops being read-only, with an asset picker that shows thumbnails.

### The Materials layout

The first *workspace* (architecture 3.1): a saved layout of the panels below, picked from the titlebar.

```
+---------------------------------------------+----------------------+
| Preview                                     | Material             |
|  [Sphere v] [Lit v] [Studio v] [A|B] [||]   |  Stone > WeatheredSt |
|                                             |  Shader: Stone.slang |
|                                             +----------------------+
|                                             | Parameters           |
|                                             |  v Surface           |
|                                             |    Base Color  [##]  |
+---------------------------------------------+    Roughness  ==o--  |
| Shader Log | Inspect                        |  v Textures          |
|  x Stone.slang(23,14): undeclared 'uvv'     |    Albedo  [img] ... |
|  compiled 41 ms . 1 pipeline . 1.2 KB       |  > UV                |
+---------------------------------------------+----------------------+
```

### Panels

1. **Preview**: a scene view with a world of its own (a mesh, a light, the environment). This needs a world per scene view (`SetSceneViewWorld(id, world)`), where today there is one for all.
   - Shapes: sphere, rounded cube, plane, teapot, the selected entity's mesh, any mesh asset.
   - Environment: studio (neutral grey with soft light), the project's skybox, black. Exposure. The light and the environment rotate by dragging with a modifier.
   - Views: lit, and each output of the domain on its own (base colour, normals, roughness, metallic, occlusion, emission, opacity, UV checker), plus a NaN/Inf view that paints bad pixels bright and a view of values outside 0 to 1 before tonemapping. The engine writes the domain's entry points, so these are switches in them, not edits to the user's code.
   - **A|B:** last saved or last good on one side, current on the other, with a divider to drag. Or two materials side by side.
   - Time: pause, step, scrub, speed. `nyx::time` comes from the preview, not the clock, so an animated shader can be held still.
   - For the other domains, the stages in the table under recommendation 5.
2. **Parameters**: generated from the layout: categories, sliders for ranges, swatches for colours, asset fields with thumbnails for textures, checkboxes for switches, combos for enums. A value that differs from its default (or its parent's) shows a reset button. Copy and paste of single values and of the whole material.
3. **Material**: which shader, the parent, the domain, which materials derive from this one, which entities use it (click to select).
4. **Shader Log**: diagnostics of the last compile of every shader, materials and the engine's own, newest first; a click opens the file at the line. A summary per compile: time, pipelines made, SPIR-V size, cache hit.
5. **Inspect**: the reflected layout (each field with offset and size), the SPIR-V disassembly, the generated entry point. Stage 4 adds GPU time per preview draw and, where the driver offers `VK_KHR_pipeline_executable_properties`, register and instruction counts.

### Asset browser

- *New Material* per domain, from a template in `Engine/Shaders/Templates`: a shader and a material together, or a material for an existing shader, or an instance of the selected material.
- Material thumbnails, rendered by the preview's code into a small image when a material changes (editor UX overhaul change 9 lists thumbnails).

## Stages

### Stage 1: foundations

Done when:

- Every shader of the engine, the painter's included, reloads on save; a broken one keeps the last good pipeline and the log names file and line.
- The build fills the shader cache with `NyxShaderCompiler build`; the `glslc` step is gone; the editor starts without compiling anything that didn't change.
- Scene drawing is bindless (global set, view set, push constants), and the default scene rendered before and after that change is identical.
- Scene views render HDR with exposure and tonemapping; the editor's overlays look as before.
- `Standard.slang` implements the metallic-roughness model with image-based light.
- The material test scene matches its reference images.
- Saving a material shader, or editing a `.nyxmat` by hand, updates every scene view in under 250 ms for a shader like `Stone.slang`.
- `NyxShaderCompilerTests`, `NyxMaterialTests` and `NyxMaterialGpuTests` pass.

Changes:

1. **Spike: Slang and bindless.** Done 2026-10-13: Slang holds; see *As built*. Link `slang.lib`, copy `slang.dll` next to the programs. Compile `Stone.slang` against a first `nyx.surface`: a generic entry point specialised by the user's struct, the struct loaded from a byte buffer at an offset, texture fields as `DescriptorHandle`. Read back the attributes and the layout. *Decides the language*: if any of the three doesn't hold up, switch to GLSL + shaderc + SPIRV-Reflect for materials here, with the same `ShaderLayout` out, and the rest of the plan stands. Measure: session start, compile of a small shader, compile with a warm session.
2. **`ShaderCompiler` and `ShaderLayout`.** Done 2026-10-13; see *As built*. Both front ends, diagnostics as data, dependencies, reflection into the layout. Tests: diagnostics have the right line and column in both front ends; every attribute arrives; a field's offset matches the SPIR-V; imports and includes are listed.
3. **`FileWatcher`, the compile worker and `ShaderPrograms`.** Done 2026-10-14; see *As built*. Watch `Assets/` and `Engine/Shaders`, gather changes, map files to the programs that use them, compile off the main thread, swap results in at a frame's start. Every engine pipeline (scene, grid, skybox, picking, selection mask, outline, debug lines, painter, blur) is rebuilt from its program; the grid's own reload is removed. **Compile in a process of its own** (a small `NyxShaderCompiler` server the editor starts and restarts): Slang can crash on valid code (as built, change 2), and a crash must cost one compile, not the editor.
4. **`ShaderCache` and the build's compiler.** Done 2026-10-14; see *As built*. Keys, files under `Saved/ShaderCache` (and next to the programs for builds), `VkPipelineCache` beside it; the CMake step runs the tool instead of `glslc`. Material shaders' default bytes are cached with their SPIR-V; making them takes a GPU, so the tool needs a Vulkan device without a window. Tests: hit, miss on a changed include, miss on a new compiler version.
5. **Bindless.** Done 2026-10-14; see *As built*. The descriptor indexing features and `scalarBlockLayout`; `VulkanBindless` with the texture arrays, samplers and default slots; the view set; push constants, custom data included (zero until stage 3 sets it); the scene, picking, mask and skybox pipelines moved onto them. The default scene's pixels are compared before and after: this change must not change the picture.
6. **`MaterialAsset` and `VulkanMaterials`.** Done 2026-10-14; see *As built*. The `.nyxmat` reader and writer; materials as bytes in the material buffer, pipelines per shader and switches; the `IAssetResolver` loading real files; the magenta checker; last good. Tests: round trip, values carried over on a layout change, unknown keys kept, bytes written at reflected offsets.
7. **HDR and the environment.** Done 2026-10-14, in two halves; see *As built*. `R16G16B16A16Sfloat` scene targets, exposure, the tonemap pass, overlays and outline after it, the blur and ambilight reading the tonemapped picture. `VulkanEnvironment`: cubemap mips prefiltered by roughness, spherical harmonics for irradiance, the BRDF lookup table.
8. **`Standard`.** Done 2026-10-14; see *As built*. `nyx.surface` and `nyx.lighting` with the baseline of recommendation 6, the environment's light included; `Standard.slang` (base colour, metallic, roughness, normal map, occlusion, emission; opaque and masked); tangents in `Vertex`, written out for the built-in meshes (the cube, a sphere); linear textures and mips. The default scene's three materials become `Standard` materials; their look changes on purpose. Revised 2026-10-14: the glTF importer and MikkTSpace wait for mesh assets, since imported materials would have no imported meshes to go on yet; blended alpha and real switches wait too (see *Left for later*).
9. **The material test scene.** Done 2026-10-14; see *As built*. `Assets/Scenes/MaterialTest.nyxscene`: the sphere grid, the skybox. `NyxMaterialGpuTests` renders it at a fixed size and compares with reference images (largest and average difference within a tolerance, since GPUs differ slightly); `NYX_MATERIAL_TEST_OUTPUT` saves what it rendered, as the painter's GPU tests do. The references are approved by eye once. Beside them, checks with known answers: the furnace test, and spheres against the lighting computed on the CPU. (Revised 2026-10-14: glTF samples beside the glTF Sample Viewer's renders come with mesh assets.)

### Stage 2: the material editor

Done when:

- *New Material* makes a surface shader and material and opens them in the Materials layout.
- Adding, removing or changing a field updates the parameters panel; values carry over by name.
- Dragging a parameter shows in the preview and the scene views in the same frame; undo and redo work; saving writes the `.nyxmat`.
- A shader that doesn't compile keeps the last good picture, and clicking the log line opens the file at the line.
- A material can be dragged onto an entity.

Changes:

1. **A world per scene view.** Done 2026-10-15; see *As built*. `SetSceneViewWorld(id, world)`, picking and revisions per world. May land earlier, as a change of its own.
2. **The material document and the parameters panel.** Done 2026-10-15; see *As built*. `MaterialDocument`, transactions, the generated widgets (painted `Nyx::UI` widgets, as the details panel uses), reset to default, save.
3. **The preview.** Done 2026-10-15; see *As built*. The preview world, shapes and environments, exposure, the lit view and the debug views, the light rig. A|B and time controls if they fit; otherwise stage 4 (they went to stage 4).
4. **Shader Log and the Materials layout.** Done 2026-10-15, in two halves; see *As built*. The log, opening at a line, the compile summary; the layout as the first workspace; *New Material* for surfaces; drag to assign; `MeshRendererComponent::Material` editable.

### Stage 3: 2D, UI, post, steering, and games

Done when:

- A sprite material, a UI material and a post material each have a template, a preview, and a place in a scene or the editor where they are used.
- Editing any shader or material while games run shows in every game at once, and in a game started afterwards.
- A material instance overrides some values of its parent, and follows changes to the rest.
- Game code changes a material, one entity's override, an entity's custom data and a global, through the API and through paths; a test scene in NyxGame shows all four.
- Overrides and custom data are edited in the details panel, with undo, saved with the scene and sent to games live; globals are edited in their panel.
- The asset browser shows material thumbnails.

Changes:

1. **Live in games.** Done 2026-10-15; see *As built*. Link messages for a compiled program (SPIR-V per stage, the layout) and for material bytes; the game swaps them in like the editor does. Games started later get the current state on connect, like other live edits.
2. **Material instances.** Done 2026-10-15; see *As built*. `Parent` in the file, inheritance of values, the parameters panel showing what is inherited and what is overridden; changing a parent updates its children.
3. **Sprite domain.** `nyx.sprite`, the preview stage, and a small `SpriteComponent` (a textured quad with a material, size and pivot), so 2D materials have somewhere to live in a scene. A 2D renderer with batching and sorting is a plan of its own.
4. **UI domain.** The painter's custom command (architecture 3.10.1): the painter ends its current run, draws the command's shape with the material's pipeline (coverage and clip from `nyx.sdf`, the same distance functions the painter uses), and goes on. `Painter::AddMaterialRect(rect, radii, material)` and an optional material on `Nyx::UI` panels and buttons. Each one costs a draw; the painter's Statistics tab shows how many.
5. **Post domain.** A post stack per scene view (materials before and after the tonemap), shown in the scene view's settings and saved with the scene.
6. **`MaterialSystem` and paths.** The API, typed values, `MaterialParam` surviving recompiles, writes into the frame's copy, `Resolve(path)`. Tests: a handle found again after a recompile; a removed parameter logs once; the wrong type is refused; paths of all four levels resolve.
7. **Per-entity overrides and custom data.** The entity's bytes made from the material plus its overrides, made again when the material changes, freed with the last override; custom data from the component into the push constants. The reflected `MaterialOverrides` list, its widget, greyed-out stale overrides. Tests: a value not overridden follows the material; removing the last override frees the bytes; the list saves, loads and undoes.
8. **Global parameters.** `Globals.slang`, its reflection, the buffer in the view set, the panel, saving with the project, sending to games.
9. **Thumbnails.** Rendered on change, cached beside the shader cache, shown in the asset browser and the asset pickers.

### Stage 4: tools

1. **Pixel inspector.** Click a pixel of the preview: a variant of the entry point writes the input and output of the domain at that pixel into a buffer, shown as a table (UV, normal, base colour, roughness, ...). Since the engine owns the entry point, the user's code doesn't change.
2. **Built-in code editor.** A painted editor panel for shader files: Slang highlighting, compile while typing (300 ms after the last key, no save), errors under the line, drag a number to change it live, a colour swatch beside `float3` literals. Unsaved text compiles and goes to games like saved text.
3. **Performance.** GPU time per preview draw (timestamps, as the painter's statistics do), register and instruction counts where the driver exposes them, the cost of each switch combination.
4. **Debug views in scene views**, not only the preview: any scene view can show base colour, normals, roughness and the rest for the whole scene. The renderer has them per view since stage 2, change 3 (`SetSceneViewDebug()`); what is left is a choice in the scene views' bar.
5. **A|B and time in the preview** (moved from stage 2, change 3): last saved or last good on one side, current on the other, with a divider to drag, or two materials side by side; pause, step, scrub and speed, with `nyx::time` from the preview.

## Risks

- **Slang's generic entry points, struct loads with handles, and attribute reflection.** The authoring model leans on all three. Change 1 checks them before anything else is built; the fallback keeps the plan.
- **Moving every engine pipeline onto programs** (change 3) touches the painter, blur and outline, which work well today. Each moves on its own, with its GPU tests run before and after.
- **Bindless mistakes are quiet.** A wrong index samples the wrong texture or nothing, without an error. Validation layers on in debug builds, the magenta default for out-of-range indices, and the GPU tests catch most of it. Slots are freed only after the frames in flight are done.
- **HDR changes what reads the scene picture:** the outline composite, the blur behind glass and ambilight, picking's depth. They read the tonemapped picture, so they don't change; change 7 checks each.
- **Reference images differ between GPUs and drivers.** A tolerance, and references approved per machine when they must; the test saves its output so a difference can be looked at.
- **Slang crashes on some valid code.** Change 2 found a struct with an array that crashes Slang 2026.1 (and `slangc`) while generating code; materials refuse arrays until it is fixed. Others may exist, which is why change 3 compiles out of process.
- **One more big DLL.** `slang.dll` and `slang-compiler.dll` (24 MB) next to the programs, and versions move with the SDK. Pin the version the build expects and say so clearly when the SDK differs. Shipping games load from the cache and don't need it.
- **First compile hitch.** Making a Slang session and loading its core module takes a noticeable moment. It happens on the worker at startup, and the cache means nothing waits on it when nothing changed.
- **A world per scene view** changes a renderer assumption (one world, one revision for picking).
- **UI materials and the painter's single draw.** Every custom command splits a run. Fine for a few effects, wrong for every button; the Statistics tab and the heatmap keep it honest.
- **Live edits racing a game that is starting.** The same rule as other live edits: sent once linked, state on connect.
- **A typed, dynamic list in reflection.** Reflection so far describes fixed C++ fields; `MaterialOverrides` holds values whose names and types come from a shader. It needs its own serialisation, diffing for undo, and link messages. If that grows large, overrides can first be stored as one reflected string-to-text map, parsed by the material system.
- **Overrides on many entities** each copy their material's bytes. Fine for tens or hundreds; for thousands, custom data is the answer, and the log warns past a budget.

## Left for later

- A node graph. If it comes, it writes a Slang struct that implements `ISurface`, and everything here keeps working.
- Compute, decal and particle domains.
- More lights, shadows, a light component of its own.
- Animation tracks and script bindings on material paths, once animation and scripting exist.
- Showing in the editor what a running game has set at runtime.
- Custom data larger than a `float4`, in a per-object buffer.
- The painter's images from the global texture array.
- Picking and selection masks from an ID target written by the main pass, instead of passes of their own.
- Material layers and blending several materials on one mesh.
- Virtual texturing.
- Permutations beyond switches.
- Cooking for shipping, and a cache shared across machines.
- **Meshes as assets, with glTF** (moved out of change 8 on 2026-10-14): imported meshes and their tangents (glTF's, or MikkTSpace's), the importer writing one `.nyxmat` per material (factors, all five textures, alpha mode), and glTF sample models in the material test scene beside the glTF Sample Viewer's renders.
- **Multiple scattering for rough metals** (Kulla-Conty, or Fdez-Agüera's single-lookup version): single-scattering GGX loses the light that bounces between microfacets, so a white metal at roughness 1 reflects only about 31% of a uniform sky (the furnace test, change 9).
- **Blended materials:** a second pipeline per shader, drawn after the opaque objects, sorted back to front. Until then a blended material is masked.
- **Switches as specialisation constants**, a pipeline per combination; until then `[Switch]` fields are bools in the material buffer, a uniform branch.
- The rest of the engine's shaders in Slang proper.
- Arrays in materials, once Slang no longer crashes on them (`WithArray.slang` in the tests is ready).
- Importing Shadertoy shaders into the Post or UI domain.

## As built

### Stage 1, change 1: the Slang spike

`Tools/NyxSlangSpike` (`NyxSlangSpike.exe`, and the CTest `NyxSlangSpike`): a program that checks each question below against Slang 2026.1 (`2026.1-52-gc8ddf20bb`, Vulkan SDK 1.4.341.1), prints a report and fails if a check fails. Its shaders sit in `Tools/NyxSlangSpike/Shaders` and are read from there: a first `nyx/surface.slang` (attributes, bindings, `nyx::Texture`, `ISurface`, both entry points), `Stone.slang`, a broken copy for diagnostics, a layout probe, and a material without a default. Change 2 replaced it with `NyxShaderCompilerTests` and `NyxShaderCompilerGpuTests`, and removed it.

**Verdict: Slang holds.** All three things the authoring model leans on work, each with a rule:

- **Generic entry point, specialised with the material.** `surfaceFragment<S : ISurface>` specialised with `Stone` compiles to SPIR-V that passes `spirv-val` for Vulkan 1.3, with entry points keeping their names. The fragment uses exactly bindings 0.0, 0.1, 0.2 and 1.0. *Rule:* compose `[domain module, material module, entry point]`, then specialise. Leaving the domain's module out, or putting it after the material, makes Slang fail with an internal error while generating code ("The key already exists in Dictionary"); the spike lists which orders work, to see when an update changes it.
- **The material loaded from a byte buffer at an offset.** `ByteAddressBuffer.Load<T>` and `Store<T>` use scalar layout, whatever the session says. Reflection reports that layout only in a session with scalar layout (`forceGLSLScalarBufferLayout`); a default session reports std140-like offsets (176 bytes for the probe instead of 100). Checked on the GPU with every vector and texture at an offset that isn't a multiple of its size, and a non-zero material offset: every value, bools included (4 bytes), comes back from the reflected offset, both ways. *Rule:* the engine compiles everything in a scalar-layout session. `Store<T>` writes through arrays with a 12-byte stride, which needs the `scalarBlockLayout` feature (the validation layer reports it until change 5 turns it on).
- **Attributes.** User attributes declared in the library (`[__AttributeUsage(_AttributeTargets.Var)] public struct RangeAttribute { ... }`) come back by name with their arguments, strings and numbers. `findUserAttributeByName` works.

Found along the way, and changed in the plan above:

- **`DescriptorHandle` inside a material doesn't work.** At an offset that isn't a multiple of 8 it is loaded and stored through a `uint2` array of stride 8, so it reads the wrong bytes (at offset 4 it reads offset 0); it also has no default, and Slang refuses a default constructor in an extension. `nyx::Texture` is therefore `struct { uint Index = 0; }` with `Sample`/`SampleLevel` going through the heap. The spike keeps showing the handle's behaviour, informationally.
- **One heap for all sampled images.** `__DynamicResource` at binding 0.0 aliases 2D and cube textures, and an exported `getDescriptorFromHandle` puts Slang's own handles there too. No separate cube array.
- **Defaults.** Reflection gives default values for scalars only (`hasDefaultValue` is true for a `float3`, but no value). A generated compute shader that stores the material made with its defaults gives all of them, in the GPU's layout. It must be generated per material as source (`loadModuleFromSourceString`): a generic `S()` also hits an internal error. With the session's zero-initialisation on, a field without a default is 0.
- **Matrices.** The API's sessions default to row-major (`slangc` to column-major). With `SLANG_MATRIX_LAYOUT_COLUMN_MAJOR`, `mul(Object.Model, position)` with a matrix straight from glm matches glm on the GPU.
- **Module names are paths.** `import nyx.surface;` loads a module named `nyx/surface`; `loadModule("nyx.surface")` looks for a file named `nyx.surface.slang`. The compiler finds library modules among the loaded ones.
- **Diagnostics are text:** `file(line): severity code: message`, the source line, and a caret line. The column comes from the caret; the spike parses the typo's position exactly (line 13, column 32).
- **Dependencies:** `IModule::getDependencyFilePath` lists the module's file and every import.
- **DLLs:** `slang.dll` (46 KB), which loads `slang-compiler.dll` (24 MB). `slang-glslang.dll` (10 MB) isn't needed: the SPIR-V comes out byte for byte the same without it.
- **Visibility:** a material file without `public` anywhere is in Slang's legacy mode, where other modules see its fields. Whether the generated defaults writer still works when a material uses `public` is untested.

**Times** (RTX 3070 Ti, the spike's Debug build; Slang's DLLs are release builds):

| What | Time |
| --- | --- |
| Global session, once per program | 51 ms |
| A save: new session, load the material and the library, both entry points | 43 ms median (41 to 62) |
| Again in the same session (link and code only) | 24 ms |

The plan's 250 ms from save to picture leaves about 200 ms for pipelines and the frame.

**This GPU:** every descriptor indexing feature, `scalarBlockLayout`, 1,048,576 sampled images after update, 256 bytes of push constants.

### Stage 1, change 2: `ShaderCompiler` and `ShaderLayout`

- `Engine/Source/Runtime/Renderer`:
  - `ShaderLayout.h`: a material's struct as plain data: fields with kind (scalar, vector, matrix, texture, struct), scalar type, offset and size in scalar layout, array count and stride, attributes with their arguments, the scalar default when reflection has one, and members of nested structs.
  - `ShaderCompiler.h/.cpp`: `CompileProgram()` (the engine's GLSL, or a `.slang` file with `[shader]` entry points), `CompileMaterial()` (a struct named like its file, specialised into a `ShaderDomain`'s entry points; `ShaderDomain::Surface()` is `nyx.surface`), `CompileDefaultsWriter()`. Results hold SPIR-V per entry point with the name to make the pipeline stage with, diagnostics as data (`ShaderDiagnostic`: file, line, column, severity, code, message; `ToString()` prints `Stone.slang(13,32): error 30015: ...`), dependencies, the material's layout and the time taken. Each compile has a session of its own, so a changed file is read again.
- `Engine/Shaders/Library/nyx/surface.slang`: the surface domain, from the spike, with the attributes `Category`, `Tooltip`, `DisplayName`, `DragSpeed`, `Range`, `Color`, `HDR`, `Hidden` and `Default`. `Paths::GetShaderLibraryDir()` is `Engine/Shaders/Library`, read from the source tree, so library edits will reload live.
- **Build:** the engine links `slang.lib` with `slang.dll` delay-loaded (`/DELAYLOAD`), so programs that don't compile shaders don't need it; the editor and the game import nothing from it yet. `ShaderCompiler` loads it up front and reports a missing DLL through `IsAvailable()`/`GetProblem()` instead of crashing. `nyx_copy_shader_compiler_next_to(target)` (`Engine/Shaders/CMakeLists.txt`) copies `slang.dll` and `slang-compiler.dll`.
- **Tests:** `NyxShaderCompilerTests` (no GPU): Stone compiles into both entry points with valid SPIR-V and exactly the planned bindings; its layout, attributes and scalar defaults; diagnostics with line and column for Slang and for GLSL; mistakes named (a struct not named like its file, a missing file, a file name that can't be a struct, arrays); dependencies, imports and `#include`s; every engine shader compiled by Slang with the same offsets, matrix layouts, bindings and locations as `glslc`'s output; the defaults writer. `NyxShaderCompilerGpuTests`: the spike's GPU checks through the compiler (layout as loaded and as stored, the model matrix, defaults, the `DescriptorHandle` bug as information). The validation layer reports the defaults writer until change 5 turns on `scalarBlockLayout`; expected.
- The spike (`Tools/NyxSlangSpike`) is gone.

Found along the way, and changed in the plan above:

- **No shaderc.** Slang's GLSL mode compiles all 18 engine shaders. The painter's and blur's GPU tests pass with them, and every shader's offsets, matrix strides and majorness, bindings and locations equal `glslc`'s; so the second front end isn't needed. Two rules: the global session needs `enableGLSL`, and the GLSL session needs Slang's *row-major* setting, which Slang emits as GLSL's column-major. With its column-major setting every matrix in the engine's GLSL came out transposed, with code that still validates: the comparison with `glslc` in the tests exists for this. Built-ins differ in form only (`glslc` puts `gl_Position` in a `gl_PerVertex` block, Slang declares it on its own). `slang-glsl-module.dll` isn't needed either.
- **Slang 2026.1 crashes** (an access violation, in `slangc` as well) generating code for a struct with an array after two other fields, once a value of it loaded with `Load<T>()` is passed to a function. Every material is: `shade()` is a method. `[ForceInline]` on the method avoids it, but `shade()` is the user's. Materials therefore can't have arrays yet: the compiler reads the struct's layout first and refuses arrays by name before generating any code, so a material can't take the editor down. `WithArray.slang` in the tests keeps the case.
- **A compile that fails still lists the file**, and every file its diagnostics name: a file that doesn't load lists no imports, and its reload must still happen when it is fixed.
- **A material file shouldn't hold entry points of its own.** The layout probe's compute shaders moved to `LayoutProbeShaders.slang`, which imports the material. (They weren't the cause of the crash, but a material file is the material.)

### Stage 1, change 3: live reload of the engine's shaders

- `Engine/Source/Runtime/Core/FileWatcher.h/.cpp`: `ReadDirectoryChangesW` on whole folders, overlapped and polled once per frame (`TakeChanges()`), never blocking. Reports each changed file once per call; when the system lost track, the folder itself.
- `Engine/Source/Runtime/Renderer`:
  - `ShaderCompileService.h/.cpp`: `IShaderCompileService` with `Submit()` and `TakeResults()`. `RemoteShaderCompileService` starts `NyxShaderCompiler.exe` on the first request, lets it connect back over a local connection (`NetConnection.h`, the editor link's), and sends one request at a time. A crash fails the request under way with "the shader compiler crashed compiling this file (exit code ...)"; the next request starts a new program. A program that can't start is reported the same way. `LocalShaderCompileService` compiles in process, for tests. `ShaderCompileProtocol` carries requests and whole results (SPIR-V, diagnostics, dependencies, layouts) with `BinaryWriter`/`BinaryReader`.
  - `ShaderPrograms.h/.cpp`: the engine's shaders by name (`"Grid.frag"`). A program starts from the `.spv` the build made (until change 4); with live reload on, a save to its source or to anything its last compile read is compiled 50 ms after the last change (`SettleTime`); a save during a compile is compiled after it. A failed compile keeps the last good code. `Update()` returns what finished, compiled or not, with the diagnostics; it logs nothing itself.
- `Tools/NyxShaderCompiler`: `NyxShaderCompiler --port <port> [--search-path <folder>]... [--allow-test-crash]`. Turns off Windows' crash dialog, compiles with `ShaderCompiler`, exits when the connection goes. The editor and the game depend on it, so it is built next to them.
- `VulkanRenderer` owns the `ShaderPrograms`; every pipeline is made from them. `StartLiveShaderReload()` turns live reload on when `Engine/Shaders` and `NyxShaderCompiler.exe` are there, and says in the log either way. `ApplyShaderUpdates()`, at the start of each frame, logs every compile (errors with the whole path and line, so they can be opened from the log), waits for the GPU, and makes the changed shaders' pipelines again: scene, grid, skybox, picking, selection masks, outline, debug lines, painter (`VulkanPainter::RebuildPipelines()`: canvas, every segment pipeline, and the binner from `PainterBin.comp`) and blur (`VulkanBlur::RebuildPipeline()`). The grid's own reload (per-frame file times, `std::system("glslc ...")`) is gone. The game reloads too; its compiler process only starts on its first change.
- `VulkanPainter::Initialize()` and `VulkanBlur::Initialize()` take the `ShaderPrograms`.
- **Tests:** `NyxFileWatcherTests` (written, created in a folder inside, saved by renaming a temporary file, deleted, 500 at once, quiet when nothing changes); `NyxShaderProgramsTests` (starts from the build, settling with the time held still, a broken save keeps the last good code and points at line and column, an include's change, other files change nothing, a save during a compile); `NyxShaderCompileServiceTests` (the protocol round-trips a whole result, `NyxShaderCompiler.exe` gives exactly what compiling in process gives, a crash costs one request and a new program answers the next, a program that can't start); `NyxPainterGpuTests` gains a test where `Painter.frag` is changed on disk and `PainterBin.comp` saved again, both compiled by Slang, and the rebuilt painter paints the new colour, binned right. `NyxShaderCompilerTests` compares compute shaders with `glslc`'s too.

Found along the way:

- **Slang's SPIR-V 1.6 turns `discard` into `OpDemoteToHelperInvocation`**, which needs Vulkan 1.3's `shaderDemoteToHelperInvocation`; `glslc`'s code uses `OpKill`. The validation layer said so the first time a Slang-compiled `Painter.frag` ran. `VulkanContext` turns the feature on and requires it (every Vulkan 1.3 device has it), and `NyxShaderCompilerTests` now checks that Slang's code declares no SPIR-V capability that `glslc`'s doesn't, apart from this one.
- **Slang declares some buffers twice**, as a second view with the same layout (`PainterBin.comp`). The comparison with `glslc` therefore compares the sets of decorations, each once, not their counts.
- **Moved to a newer main.** Main had gained GPU binning (`PainterBin.comp`), the input work and the new titlebar while steps 1 to 3 were built on `9fe1233`. The worktree now builds on `2b48f11`: steps 1 and 2 applied unchanged, step 3 was merged by hand (the binner's pipeline joined live reload) and its patch made again. No commits; the steps' trees are recorded again in `Patches/steps.txt`.
- **One request at a time to the compiler process.** With several sent at once, an answer the program had sent before crashing on the next request was lost: Windows drops data a crashed process sent but the other side hadn't read yet. The test of a crash found it.

### Stage 1, change 4: the shader cache, and the build compiling with Slang

- `Engine/Source/Runtime/Renderer/ShaderCache.h/.cpp`: an entry per request, `<file name>.<hash of the request>.nyxshader`: format, Slang's version, the request (kind, file relative to a root folder, domain), every file the compile read with an FNV-1a hash of its content, and the result (`ShaderCompileProtocol`). `Find()` says whether the entry is up to date: every file it read that still exists has its old content (a file's time alone doesn't count; a missing file counts as unchanged, so a game without sources uses its entries). Paths are relative to the root, so the same files elsewhere find the same entries. Another Slang version, another format, a damaged file or a failed compile is a miss.
- `ShaderPrograms` starts each program from the newest code that matches its files: live reload's (`Saved/ShaderCache/Engine`), then the build's (the `Shaders` folder next to the program). Code that no longer matches is used all the same, and with live reload on it is compiled at the first update, without waiting. What live reload compiles is stored for the next start. `StartedOutOfDate()` tells.
- **The build compiles with Slang.** `NyxShaderCompiler build --source <folder> --output <folder>` compiles every `.vert`, `.frag` and `.comp` of the folder that isn't up to date, prints errors as `file(line,column): error S<code>: message` (Visual Studio's error list reads them), and fails when any doesn't compile. `NyxShaders` runs it on every build, writing straight into `Binaries/<config>/Shaders`, which every program shares; `nyx_copy_shaders_next_to()` only orders the build now (a copy per program wrote the same folder at once in parallel builds and failed). `glslc` is used only by the tests, to make the references `NyxShaderCompilerTests` compares with. The two `Grid.*.spv` files left in `Engine/Shaders` by an old reload are gone.
- `VulkanContext` owns a `VkPipelineCache`, loaded from `Saved/ShaderCache/Pipelines.bin` before any pipeline is made and saved on shutdown; data from another GPU or driver (the header) is ignored. Every pipeline of the renderer, the painter and the blur is made with it.
- `Paths::GetSavedDir()` and `GetShaderCacheDir()`.
- **Tests:** `NyxShaderCacheTests` (entries follow their files' content, not their times; misses; a copy of the folder finds the entries; the build mode fails on a broken shader, keeps the good ones, compiles only what changed, follows an include); `NyxShaderProgramsTests` gains a program changed while nothing ran (starts from the build's code, compiled at once, the next start begins from live reload's); `NyxPainterGpuTests` gains the pipeline cache.

Found along the way:

- **The engine now runs on Slang's code throughout**, and that showed what the comparisons with `glslc` couldn't: compiled through the API (module loaded, `main` found afterwards with `findAndCheckEntryPoint()`), Slang 2026.1 drops GLSL's `layout(local_size_x = 64) in;`. The painter's binner ran with workgroups of one and binned almost nothing. `slangc` compiles it right, because it names the entry point before reading the file; `ShaderCompiler` now does the same for GLSL, with a compile request (`ISession::createCompileRequest()`). Asking a failed request for its dependencies crashes Slang, so a failed GLSL compile lists only its file and the files its diagnostics name. `NyxShaderCompilerTests` now compares execution modes (workgroup size, fragment origin) with `glslc`'s too.
- The same bug was in step 3 for live reload of `PainterBin.comp`; this step fixes it. Step 3's painter test didn't see it: its second paint reused the first paint's target, and with it the binning of the build's binner. The second paint now gets a target of its own.
- Material shaders' default bytes (the defaults writer) need a GPU at build time; they come with materials in change 6.

### Stage 1, change 5: bindless

- `VulkanContext` turns on descriptor indexing (runtime arrays, partially bound, sampled images updated after bind, non-uniform indexing), `scalarBlockLayout` and `samplerAnisotropy`, and only chooses a GPU that has them.
- `Engine/Source/Runtime/Renderer/VulkanBindless.h/.cpp`, **set 0**: binding 0 an array of 4096 sampled images (update after bind, partially bound); binding 1 the four fixed samplers (`EFixedSampler`); binding 2 the material buffer, 64 KiB, host-visible, each material at a 16-byte aligned offset. Slots 0 to 3 are textures of its own: white, black, flat normal, magenta checker (`EBindlessTexture`). `AddTexture()` takes the lowest free slot; `RemoveTexture()` frees it three frames later (`EndFrame()`, once per frame) and points it at the checker, so a material still holding the index shows magenta rather than a freed image. Material values are written in place, so only while no frame is in flight (at start, for now); the copy per frame in flight comes with steering materials while drawing.
- **Set 1, the view's:** one uniform buffer, `SceneUBO`, 272 bytes: the old fields, then the sky's view-projection and the environment's slot. Fields aligned to 16 bytes, so std140 and scalar layout agree on it. The sky's own uniform buffer and set per view are gone.
- **Push constants:** `ObjectConstants`, 96 bytes (the plan said 88; padding keeps `CustomData` 16-aligned): model, material offset, entity ID, custom data (zero; the selection masks keep their value in its x).
- One pipeline layout, `ScenePipelineLayout`, for the scene, the sky, the grid, debug lines, picking and both selection masks; `BindSceneSets()` binds both sets. The outline composite, the painter and the blur keep sets of their own.
- **The library:** `nyx/bindless.slang` (set 0, `ObjectConstants`, `nyx::Texture`, `nyx::Cubemap`, the samplers) and `nyx/view.slang` (set 1, `ViewConstants`); `nyx/surface.slang` passes both on to materials (`__exported import`).
- **The scene and the sky are Slang programs**, `Engine/Shaders/Scene.slang` (`sceneVertex`, `sceneFragment`) and `Skybox.slang`, ported from the GLSL without changing the picture. The three looks in code are bytes in the material buffer (`SceneMaterial`: reflectivity, use texture, tint, the albedo's slot); `Material` has its `MaterialOffset`. Picking, the selection masks, the grid and debug lines stay GLSL, on set 1 and the same push constants.
- `ShaderPrograms` keeps every entry point of a program: `GetSpirv(name, entryPoint)`. A compile that drops an entry point in use doesn't replace the program, and its diagnostics name the entry point. The build compiles the top-level `.slang` files of `Engine/Shaders` too.
- `VulkanRenderer::ReadSceneViewPixels()`, for the tests; the scene view's image can be copied from.
- **Tests:** `NyxSceneGpuTests` (new): the editor's default scene through its camera at 480x270, the reflective cube selected, compared with `Engine/Source/tests/References/DefaultScene.png`, which was rendered before this change. After it, no pixel differs at all. A click on the middle cube picks it, one on the sky picks nothing. As a check of the check, a fresnel factor of 0.6 instead of 0.5 fails it (1,607 pixels differ, by up to 7). `NYX_SCENE_TEST_OUTPUT` saves what it rendered. `NyxShaderCompilerTests` checks the engine's Slang programs (their entry points, valid SPIR-V, only the planned bindings); `NyxShaderProgramsTests` gains a program with two entry points.

Found along the way:

- `DefaultShader.frag` never used the tint or "use texture": the lit colour was the texture's on all three cubes, so the "untextured" cube is checkered too. Kept, since this change must not change the picture; `Standard` replaces it in change 8.
- `DestroySceneView()` freed a view's descriptor sets while the last frame could still read them (a validation error, e.g. when the game closes its view). It now waits for the GPU first.
- With `scalarBlockLayout` on, the validation layer no longer reports the defaults writer (change 2).

### Stage 1, change 6: material files and materials on the GPU

- `Engine/Source/Runtime/Renderer/MaterialAsset.h/.cpp` (no Vulkan): the `.nyxmat` file. Lines are kept as written (comments, blank lines, alignment, values the shader doesn't know); setting a value changes its own line, a new one goes at the end; a line that isn't `Name = value` fails with its number. `WriteMaterialValues()` writes a file's values over the shader's default bytes at the offsets reflection gives: scalars and vectors as numbers apart by spaces or commas (bools as `true`/`false`), a struct's members as `Struct.Field`, a texture as an asset path that becomes a slot. A texture the file doesn't set takes the slot its `Default(...)` names (`BindlessTextures.h`: White, Black, FlatNormal, Missing); one that can't be loaded shows the missing texture. Values the shader has no field for, or that don't fit their field (count, type, matrices), are left out and listed; the file keeps them, so a shader that gets the field back finds them again.
- `Engine/Source/Runtime/Renderer/VulkanMaterials.h/.cpp`: a material is a `.nyxmat` file in the assets folder, asked for by its path (`IRenderer::GetMaterial()`, never null). Its values are bytes in the material buffer, laid out by its shader; it draws with its shader's pipeline (one per shader, made like the scene pipeline was). The renderer draws a `Nyx::Material`, now just a pipeline and an offset that `VulkanMaterials` keeps up to date. A new material's bytes are its shader's defaults: the defaults writer runs once on the GPU each time the shader compiles (it is fast; its SPIR-V is cached, the bytes aren't). Textures load from the assets folder once each, as sRGB and without mips, like the checker always was.
- **Missing material:** a material whose shader hasn't compiled yet, or whose file can't be read or names no shader, draws as an unlit magenta and black checker (`Engine/Shaders/MissingMaterial.slang`, an engine program). A file that can't be read later, or a shader that stops compiling, keeps the last good look.
- **Live:** saving a material file writes its new values 50 ms after the last write (a broken file is reported and changes nothing); saving its shader compiles it again, and pipeline and bytes follow once both of its compiles (material and defaults writer) are in, values carried over by name. Material values are written in place after the renderer waited for the last frame (one frame in flight), so no frame reads them half written.
- `ShaderPrograms` compiles material shaders and their defaults writers too (`GetResult(request)`, `IsPending()`), through the same caches, live reload and service; unlike a program, one without any result doesn't throw: it is compiled at the first update. The assets folder is watched for them. The build doesn't compile material shaders: the first use compiles them (magenta for a moment), live reload's cache has them at every start after.
- `VulkanBindless`: textures made from pixels (`CreateTexture()`, freed with their slot) and material room given back (`FreeMaterial()`, reused first-fit) for a layout that changes size.
- **The default scene's three materials are files now**, `Assets/Materials/{Textured,Reflective,Untextured}.nyxmat`, on `Assets/Shaders/Classic.slang`: the old look as a surface material (the lit texture times one minus the reflectivity as base colour, the sky's reflection times the reflectivity as emission). The picture is still the same to the pixel. `Scene.slang`, the scene pipeline, the three materials in code and the test texture are gone; `BuiltinAssetResolver` is `AssetResolver` and gets materials from the renderer. `IRenderer::IsCompilingShaders()` tells when shaders compile.
- Surface pipelines leave the vertex colour out: `SurfaceInput` has none, so no surface shader reads it.
- **Tests:** `NyxMaterialTests` (new, no GPU): a file writes back as it was, values set and removed; mistakes with their line; Stone's values at their offsets over defaults, the unknown value listed and kept; values that don't fit listed and not written; values found by name after a shader's fields are reordered, one gone and one of another type; texture slots from `Default(...)`, and the missing texture. `NyxSceneGpuTests`: the default scene from its files (still 0 pixels differ); a missing material file is magenta; a material file saved while drawing changes the picture within frames, and saved broken changes nothing; a material's shader saved with a new field before the one the file sets: the file's value at its new offset, the new field's default from the shader, and saved broken nothing changes. `NyxShaderProgramsTests`: a material shader and its defaults writer compiled without the build, kept when broken, found in live reload's cache at the next start. `NyxShaderCompilerTests` checks `MissingMaterial.slang` instead of `Scene.slang`.

Not yet, and why:

- **Switches** (specialisation constants): no material has one yet, so one pipeline per shader. They come when `Standard` needs one (change 8).
- **Parents** (material instances) are stage 3, as planned.
- **Linear textures and mips:** every material texture is sRGB without mips, as the checker was. Normal maps need linear ones, and mips come with them in change 8.
- Textures aren't reloaded when their files are saved.

Found along the way:

- **An entry point that calls another entry point crashes Slang 2026.1** when compiled through its API (`slangc` compiles it). `MissingMaterial.slang`'s vertex shader wanted to call `surfaceVertex`; `nyx.surface` now has `transformSurfaceVertex()`, which both call. A crash in the build's compiler fails the build with an access violation and no message about which shader; worth catching later.
- Writing a file into a watched folder also reports the folder itself, so every material in it is read again once. Harmless; noted.

### Stage 1, change 7, first half: HDR, exposure and tonemapping

- **Two passes per scene view** (`VulkanViewportTarget`): the scene pass draws the materials and the sky into an `R16G16B16A16Sfloat` image and the depth buffer, and keeps both; the picture pass tonemaps the HDR image into the 8-bit picture (`Engine/Shaders/Tonemap.slang`, one triangle over the view), then draws the editor's overlays over it (grid, debug lines, selection outline), tested against the scene's depth. The overlays' pipelines are made for the picture pass, the scene's for the scene pass (`GetSceneRenderPass()`, `GetPictureRenderPass()`). Everything that read the picture before (the UI, the blur behind glass and ambilight, screenshots, `ReadSceneViewPixels()`) reads it as before; picking and the selection masks have passes of their own.
- **The tonemap** reads the view's HDR image through the bindless array (each view's HDR image has a slot, taken again when the view is resized), multiplies it by the exposure, and applies Khronos PBR Neutral, the glTF Sample Viewer's: colours up to about 0.76 pass through all but unchanged, brighter ones are compressed towards white keeping their hue. Then it encodes sRGB.
- **Exposure**, per view, in stops (`IRenderer::SetSceneViewExposure()`, 0 by default): the view constants' `ExposureScale` is 2 to the stops. The view constants also carry the HDR image's slot (`Scene`); both took padding the struct had.
- `Engine/Shaders/Library/nyx/color.slang`: `luminance`, `linearToSrgb`, `srgbToLinear`, `tonemapPbrNeutral`.
- **The picture changed, on purpose:** textures are sRGB and the GPU decodes them to linear light, but the scene used to write that linear light unencoded into an 8-bit picture that the UI shows as sRGB-encoded. Everything was darker than its source, the sky included. Now the tonemap encodes: the sky looks as its image does, the cubes are lit as they should be. The overlays look as before. `DefaultScene.png` was rendered again and looked at.
- **Tests:** `NyxSceneGpuTests`: the default scene against its new reference; a cube glowing (0.5, 0.75, 3) at exposures 0, -1, +1 and -3 matches the tonemap computed on the CPU within 2 levels in each channel, so light brighter than white survives until the tonemap; the glow and missing-material checks expect tonemapped colours. `NyxShaderCompilerTests` checks `Tonemap.slang`, and that no engine Slang program needs `DrawParameters`.

Found along the way:

- `SV_VertexID` makes Slang declare the `DrawParameters` capability (HLSL counts vertices from the draw's base vertex), which the device doesn't turn on; the validation layer reports it. `SV_VulkanVertexID` is Vulkan's own index. `NyxShaderCompilerTests` now fails on it.
- The missing material is no longer pure magenta on screen: PBR Neutral compresses (1, 0, 1) towards white a little, to about (241, 33, 241).

### Stage 1, change 7, second half: the environment's light

- `Engine/Source/Runtime/Renderer/VulkanEnvironment.h/.cpp`, from the sky when it loads (3 ms), in compute (`Engine/Shaders/Environment.slang`, five entry points), all three results in the bindless array:
  - **the source**: the sky copied into a 512 cube in `R16G16B16A16Sfloat`, with every mip (each texel the mean of four);
  - **the specular cube**: 256 per side, 6 mips, roughness 0 at mip 0 (a mirror: the source's mip of its size) to 1 at the last, each prefiltered with GGX from 512 importance samples that read the source's mip covering as much of the sphere as the sample does (Křivánek and Colbert), so the result is smooth;
  - **the irradiance**: the source's 32-texel mip projected onto nine spherical harmonics, each texel weighted by its solid angle; 256 threads write partial sums that the CPU adds, then convolves with the cosine and divides by pi, so the sum of the coefficients times the basis at a normal is the diffuse light there;
  - **the BRDF table**: 128 by 128, n·v by roughness, scale and bias of F0 for the split sum, 1024 samples, height-correlated Smith visibility as the direct light will use. RGBA, since RG would need extended storage formats.
- **To shaders:** the view constants carry the specular cube and its mip count, the table, and the nine coefficients (`SceneUBO` is 432 bytes now). `Engine/Shaders/Library/nyx/environment.slang`: `environmentDiffuse(n)`, `environmentSpecular(r, roughness)`, `environmentBrdf(n·v, roughness)`, for `nyx.lighting` in change 8. Saving `Environment.slang` makes it all again.
- `VulkanBindless::CreateCubeTexture()`.
- **Nothing on screen changes yet:** no material reads the environment until `Standard`.
- **Tests:** `NyxEnvironmentGpuTests` (new): a uniform sky lights every direction with its colour, diffuse and at every specular mip; a sky lit from one face gives the coefficients a CPU projection gives, light from above more than from the side more than from below, and its roughest mip spreads the light while mip 0 is the sky; the BRDF table matches the same integral on the CPU at sixteen points, and a smooth surface seen head-on reflects all of F0. `NyxSceneGpuTests`: a material glowing with `environmentDiffuse`, `environmentSpecular` and `environmentBrdf` shows what `VulkanEnvironment` made, within 2 levels after the tonemap.

Found along the way:

- With roughness near 0, GGX sampling's cos θ can round above 1, its sin is NaN, and the sample silently drops out (the BRDF table was 4% low). Clamped.
- The tangent frame turns the samples about the normal; with a few hundred samples, a turned set integrates a little differently, so the CPU reference uses the same frame.

### Stage 1, change 8: the Standard material

- `Engine/Shaders/Library/nyx/lighting.slang`, `lightSurface()`: glTF's metallic-roughness model. The sun: GGX distribution, height-correlated Smith visibility, Schlick Fresnel, Lambert diffuse; its colour is what a white matte surface facing it shows (irradiance pi times the colour). The sky: diffuse from its spherical harmonics, specular from the prefiltered cube and the split-sum table (`nyx.environment`, change 7), dimmed by the occlusion. The sky's diffuse part gets what its specular part leaves (one minus F0 times scale plus bias), so a white dielectric under a uniform sky shows the sky's colour (the furnace test of change 9). Perceptual roughness is clamped to 0.045 at least.
- `nyx/surface.slang`: `SurfaceInput` gains the tangent and the direction to the camera, `SurfaceOutput` the occlusion and `Unlit` (the base colour as it is, plus the emission); normals are carried by the inverse of the model's transpose, tangents by the model; `surfaceFragment` lights with `lightSurface()`. `nyx::normalFromMap()`; the attributes `[Linear]` (a data texture) and `[Switch]`.
- `Assets/Shaders/Standard.slang`: base colour, metallic, roughness, the metallic-roughness texture (green and blue, as glTF packs them), normal map and scale, occlusion and strength, emission, opacity, masked with a cutoff, tiling. Each texture multiplies its factor; unset, a texture changes nothing (white, or the flat normal). Textures are sampled with the anisotropic sampler.
- **Tangents:** `Vertex` has one (xyz along rising u, w the side of the bitangent, which points up the image, towards falling v, where a green-up normal map points). `ComputeTangents()` makes them from the triangles (exact on flat faces); the cube has them now, and `MakeSphere()` gives a sphere of radius 0.5, a built-in mesh beside the cube (`Meshes/Sphere.nyxmesh`, `IRenderer::GetSphereMesh()`).
- **Textures:** mips, filtered down with blits when the format allows; a `[Linear]` texture is loaded `R8G8B8A8Unorm`, a colour `R8G8B8A8Srgb` (`WriteMaterialValues()` tells the loader which).
- Material pipelines bind the vertex attributes the vertex code reads, read from its SPIR-V (the colour, which no surface shader reads; the tangent, for materials that don't use it).
- **The default scene's materials are Standard materials:** the checker, matte; polished metal mirroring the sky; light blue plastic, the tint it never had. `Classic.slang` is gone. Its look changed on purpose; `DefaultScene.png` was rendered again and looked at.
- `DirectionalLightComponent::Ambient` no longer does anything: the sky lights the scene. Worth removing or turning into the sky's strength later.
- **Tests:** `NyxSceneGpuTests`: a dielectric (grey, roughness 0.5) and a metal (copper-like, roughness 0.3) facing the camera under the default sun match the same light computed on the CPU from the environment's own data, within 3 levels after the tonemap; a one-texel normal map's normal, shown as colour, is the one the tangent convention gives; a masked material below its cutoff shows what is behind it; the default scene against its new reference; the glow tests use unlit materials. `NyxMeshTests` (new, no GPU): a flat face's tangent and bitangent, both ways round, and the sphere's tangents, lengths and winding. `NyxMaterialTests`: a `[Linear]` texture is asked for as linear.

### Stage 1, change 9: the material test scene

- `Assets/Scenes/MaterialTest.nyxscene`: 25 spheres, metallic 0 to 1 by row (bottom to top) and roughness 0 to 1 by column, all of base colour (1, 0.77, 0.34), on the Standard material (`Assets/Materials/MaterialTest/M<metallic>-R<roughness>.nyxmat`), under the sky and a sun from the upper right, seen through the scene's camera. Scene files are binary, so `NyxMaterialGpuTests --write-scene` writes the scene and its materials: the test's source is theirs.
- `NyxMaterialGpuTests` (new):
  - **the reference:** the scene file loaded as the editor loads it (`SceneSerializer`, `AssetResolver`), at 640x360 without the editor's overlays, against `Engine/Source/tests/References/MaterialTest.png`, approved by eye: the mean difference must stay under 1 level, and at most one pixel in 500 may differ by more than 16 (other GPUs and drivers round a little differently). `NYX_MATERIAL_TEST_OUTPUT` saves what it rendered.
  - **against the CPU:** single spheres facing the camera (metallic and roughness 0 to 1, five of the grid's materials) match the Standard material's light computed on the CPU within 3 levels.
  - **the furnace test:** a uniform grey sky (`VulkanRenderer::SetSky()`), the sun off: white dielectric spheres at roughness 0, 0.5 and 1 vanish into the sky, every pixel within one level of it. White metals come out at 181, 173 and 96 against the sky's 181: single-scattering GGX loses light as it gets rougher (31% left at roughness 1, as the same integral gives on the CPU); multiple scattering is left for later.
- The scene test's helpers (the renderer in a hidden window, the tonemap and the Standard material's light on the CPU, the environment's samples, files of a test's own in the assets folder) are shared in `Engine/Source/tests/RenderTestSupport.h`.
- `VulkanRenderer::SetSky()` and `GetBindless()`, for tests.

Found along the way:

- The CPU's copy of the BRDF table lookup read past the table at roughness 1, where the GPU's sampler clamps; it clamps too now.

### Stage 1: where it stands

- Every shader of the engine reloads on save, a broken one keeps its last good pipeline, and the log names file and line. Done (changes 2 to 4).
- The build fills the shader cache with `NyxShaderCompiler build`; `glslc` is left for the tests' comparisons; the editor starts without compiling what didn't change. Done (change 4). Material shaders aren't in the build: their first use compiles them (change 6).
- Scene drawing is bindless, and the default scene was the same to the pixel before and after. Done (change 5).
- Scene views render HDR with exposure and tonemapping; the overlays look as before. Done (change 7).
- `Standard.slang` implements the metallic-roughness model with image-based light. Done (change 8). glTF's part moved to mesh assets.
- The material test scene matches its reference image. Done (change 9).
- Saving a material shader or a `.nyxmat` updates every scene view quickly: a material file within a frame or two after 50 ms of settling; a material shader once both its compiles are in, about 50 + 90 + 120 ms for `Standard.slang` in a warm compiler (the two compiles run one after the other). Close to the 250 ms aimed at; compiling both in one request would make it.
- `NyxShaderCompilerTests`, `NyxMaterialTests` and `NyxMaterialGpuTests` pass, with the rest of the suite (31 tests).

### Stage 2, change 1: a world per scene view

- `IRenderer::SetSceneViewWorld(id, world)`: a view shows a world of its own, through its camera and under its sun, and picks from it; null shows the renderer's world again (`SetWorld()`), which views show by default.
- The renderer keeps a `RenderWorld` per world a view shows: its objects, its picking IDs, its range of the debug lines, and its revision. Each world is read once a frame, however many views show it; one no view shows any more is forgotten at the start of the next frame, before its registry is read, so a world can be destroyed right after it is unset.
- Revisions are per world and never repeat across worlds: setting a world (`SetWorld()`, `SetSceneViewWorld()`) starts a new one, also at the same address, and drops the pending picks of the views showing it, and only theirs.
- The selection is in the renderer's world: a view of another world draws no outline and no masks, even of an entity with the selected one's handle.
- Nothing changes in the editor yet: all its views show the scene. The material preview (change 3) is the first to use a world of its own.
- `NyxSceneGpuTests` gains a case: the default scene in one view and a sphere in a world of its own in another. The scene's view still matches `DefaultScene.png`; each view picks from its own world; the preview shows no outline; setting one world drops only its views' picks; a view set back shows the scene, and the preview goes on without it.

### Stage 2, change 2: the material document and the Parameters window

- `MaterialDocument` (editor): the material file open for editing, as text, with an undo history of its own. Edits set a value or remove it (the shader's default shows); a drag previews values and ends as one undo step. Each step keeps the file's text before and after, so undo puts lines back where they were and undoing to the last save is "not unsaved" again. Save writes the file, comments and values the shader doesn't know kept. The file read again (saved in a text editor) is followed while nothing is unsaved, and the history starts anew; unsaved values stay. Not the editor's reflected `TransactionSystem`: material values aren't reflected properties, and undo per document (architecture 3.2) wants a history of its own anyway.
- `MaterialParametersPanel`, the "Parameters" window: the shader's fields from its layout, those without a category first, then a collapsing heading per `[Category]`; structs as tree rows of their members. Widgets: `[Range]` sliders, drags (`[DragSpeed]`), `[Color]` swatches (drags with `[HDR]`), checkboxes for bools and `[Switch]`, combos for `[Enum("A, B")]`, a path field per texture (hint: its `Default`), a text field for what has no widget or a value typed wrong in the file. `[DisplayName]`, `[Tooltip]` and `[Hidden]` are honoured. A value that differs from the shader's default has Reset. Values the shader doesn't know are listed under *Not in the Shader*, with Remove. Save, Revert, "Unsaved changes"; Ctrl+Z, Ctrl+Y and Ctrl+S are the material's while the window has focus. Opening another material over unsaved values asks: Save, Don't Save, Cancel.
- Opening: double-click a material in the asset browser, or *Window > Parameters*.
- Renderer: `IMaterialSource` (which `IRenderer` is): `ViewMaterial(path)` gives the values, the shader's layout and the bytes of its defaults, and the file's revision; `SetMaterialValues(path, asset)` draws values without touching the file, written once the UI is built, so they show in that frame. `ReadMaterialValue()` reads a field's default as a file writes it; `ParseMaterialNumbers()` and `FormatMaterialNumbers()` (shortest text that reads back as the same float).
- Tests: `NyxMaterialEditorTests` (new: the document, and the window drawn headless over a stand-in for the renderer: every kind of parameter, edits sent, the keys, following the file, the question before another material opens); `NyxMaterialTests` (values read back as written); `NyxSceneGpuTests` (values set in memory show in the next frame, the file stays, a save read again wins).

Found along the way:

- `NyxSceneGpuTests`' reload test still used `Classic.slang`, gone since change 8; it passed only because `Saved/ShaderCache` still held Classic's last good compile, and would fail on a fresh checkout. It uses `Standard.slang` now.

Left for this stage's later changes or after: texture fields with thumbnails and a picker (thumbnails are stage 3), copy and paste of values, asking about an unsaved material when the editor closes, and editing the shader of a material (the Material panel, change 4).

### Stage 2, change 3: the preview

- `MaterialPreviewPanel`, the "Preview" window: the material open in the Parameters window on a shape in a world of its own (change 1), with a camera and a sun. Its bar: the shape (sphere, cube, plane, or the selected entity's mesh), the view (lit or a debug view), the sky (studio, the scene's, black) and the exposure in stops. Drag turns the camera around the shape, the wheel brings it closer; Shift+drag turns the sun, Ctrl+drag the sky. Every shape is fitted into the same sphere, so the framing and the light don't change with its size. Its scene view exists while the window is open; it opens with the Parameters window when a material is double-clicked, and is under *Window*.
- Renderer, per scene view (`IRenderer`): `SetSceneViewSky(id, sky, rotation)`: the scene's sky, a studio (a grey room, lighter above, with a soft key, fill and rim light, made in code as a 64-pixel half-float cube) or black, turned about the up axis; `SetSceneViewDebug(id, view)`: lit, base colour, normal, roughness, metallic, occlusion, emission, opacity, a UV checker, NaN/Inf (magenta over the picture in grey) and out of range (light above 1 before tonemapping red, below 0 blue). `GetPlaneMesh()`.
- `VulkanEnvironment` keeps a light per sky (by its slot): the views choose between them; the source cube is let go once the light is made. The studio's and black's light is made at their first use.
- Shaders: `nyx/view.slang` gains `SkyRotation` and `DebugView` in the padding it had (the view constants keep their size), `nyx::skyDirection()` and the `Debug*` constants; the sky and `nyx.environment` look up turned directions; `surfaceFragment` returns the material's output in a debug view, as the plan wanted, in the engine's entry point, not the user's code; the tonemap shows outputs as they are (colours sRGB-encoded, data as stored, so 0.5 is 128) and marks wrong light; behind a material's debug view the sky is a dark grey.
- Tests: `NyxSceneGpuTests` (a view's sky: black, studio, the scene's turned half and fully round; every debug view's value at the middle of a sphere; light above 1 and NaN marked); `NyxMaterialEditorTests` (the preview's orbit looks at its target from its distance and turns).

Left: a rounded cube, the teapot and mesh assets (with mesh assets), and A|B and time controls (stage 4).

Found later (fixed 2026-10-15): closing the Preview let go of its scene view while the UI was built, after the frame had recorded drawing it, and destroying its images and sets broke the frame's commands (a crash in the validation layer). A view let go of while a frame is recorded is destroyed at the next frame's start; one made then shows from its first drawn frame.

### Stage 2, change 4, first half: the Shader Log

- `ShaderLog` (renderer, no Vulkan): the last compile of every shader since the start, the newest last; a shader's compile replaces its entry. An entry: the name (`Grid.frag`, or a material shader's path in the assets folder, `Shaders/Stone.slang`), the file, whether it compiled, its time, its entry points and the size of their SPIR-V, when, and every diagnostic. `IRenderer::GetShaderLog()`; `VulkanRenderer` fills it from the updates it takes each frame. A material's defaults writer shows only while it fails.
- The "Shader Log" window (*Window > Shader Log*): newest first, "Compiled" or "Failed" (keeps its last good code), the time of day, the milliseconds, the entry points and the SPIR-V size, then each diagnostic coloured by its severity; clicking one opens its file at the line in the source editor of the preferences (Visual Studio or VS Code), as the UI source inspector does. *Failing only*; the title counts the shaders that fail.
- The Preview: while the material's shader doesn't compile, the last good picture over a red line with its first error, which opens the file at the line.
- Not in the summary: pipelines made and cache hits. Live reload always compiles, so there are no hits to show; a compile's pipelines are its entry points, which it shows.
- Tests: `NyxShaderProgramsTests` (the log replaces a shader's entry and moves it last, finds entries by file, counts failures); `NyxSceneGpuTests` (a material shader's compile is in the log by its path; saved broken, the log has the error at its line); `NyxMaterialEditorTests` (a diagnostic, clicked, opens its file at its line; the window draws its entries, all and failing only).

### Stage 2, change 4, second half: the Materials workspace

- Workspaces (`Workspaces`, architecture 3.1): Scene and Materials, tabs in the titlebar between the menus and the scene. Each keeps its dock layout in `Saved/Layouts/<Workspace>.ini`; switching saves the layout left and loads the other (between frames, where ImGui can load settings), or builds its default the first time (the preview large on the left, the parameters on the right, the Shader Log and the asset browser under the preview; the Scene workspace's: outliner, scene views, details, asset browser and game link). Each opens its panels and closes the other's. The workspace of the last session is in the preferences (`Workspace=`), whose layout imgui.ini holds.
- Opening a material (double-click in the asset browser) switches to Materials.
- *New Material* (right-click in the asset browser where there is no card): asks a name, makes `Shaders/<Name>.slang` from `Engine/Shaders/Templates/Surface.slang` (its struct named after it, as a material's must be like its file) and `Materials/<Name>.nyxmat` on it, opens the material in the Materials workspace and the shader in the source editor at the line to fill in. *New Material for Shader* on a shader's card: a material on it, numbered when the name is taken. Shaders are assets of their own in the browser now; double-clicking one opens it in the source editor.
- Assigning: a material dragged from the asset browser onto an entity in a scene view (the view picks under the mouse, and the entity gets it once the pick is back), or onto the material field in the details panel. `MeshRendererComponent::Material` is editable: an asset reference is one row, a combo of the assets of its type (`AssetType` metadata, new in the header tool), which takes a dragged asset of that type too. Each assignment is one undo step, and reaches running games as any edit; the material is loaded through the component's PostLoad, as after any edit.
- The Parameters window's *Material* section, the plan's Material panel folded in: the shader (another can be chosen, as an edit; values it doesn't know go under *Not in the Shader*), the domain, and the entities of the scene that use the material (a click selects one).
- Tests: `NyxMaterialEditorTests` (New Material's files, names and refusals, and a material for a shader; the workspaces' layouts saved per workspace, and the Materials default docking the preview and the parameters apart; a dragged material taken by a material target, not by a mesh target); `NyxMaterialTests` (the template compiles as a material once named); `NyxEditorSceneTests` (asset references drawn as one picker, not their fields; assigning a material is one undo step, at the reference's place).

Left: thumbnails in the pickers (stage 3), the Inspect panel (stage 4), parents and derived materials (stage 3).

### Stage 2: where it stands

- *New Material* makes a surface shader and material and opens them in the Materials layout. Done (change 4).
- Adding, removing or changing a field updates the parameters panel; values carry over by name. Done (changes 2; values by name since stage 1).
- Dragging a parameter shows in the preview and the scene views in the same frame; undo and redo work; saving writes the `.nyxmat`. Done (change 2).
- A shader that doesn't compile keeps the last good picture, and clicking the log line opens the file at the line. Done (change 4).
- A material can be dragged onto an entity. Done (change 4).

### Stage 3, change 1: live in games

- Before: a game started from the editor runs in the same project with NyxShaderCompiler beside it, so it already watched and compiled saved shaders and material files itself, each game on its own (Play All compiled every save once per game), and unsaved values never reached it.
- Two link messages (`EditorLinkMessages.h`): `ShaderProgram`, a compile the editor made (the request and the result: each entry point's SPIR-V, a material's layout, the files it read, in `ShaderCompileProtocol`'s format, paths relative to the project's root), and `MaterialValues`, a material's `.nyxmat` text as the editor shows it, saved or not. Both are described in the Game Link window and NyxDump. New types, so the protocol's version stays.
- The editor: every shader that compiles and replaces its code (the engine's and the materials', the defaults writer too) goes to the games it started (`IRenderer::SetShaderCompiledListener()`), and so do the values the Parameters window sends to the renderer, each change while dragging (`MaterialParametersPanel::ValuesSent`), through `PlaySession::SendToGames()` in order with the scene's edits. A game that starts (Play, Play All, Restart) first gets what the files don't have: the open material's unsaved values (`PlaySession::StateForNewGames`). Compiled code it finds in the saved shader cache, which the editor writes with every compile.
- The game: a game the editor started follows the editor's shaders (`IRenderer::FollowEditorShaders()`, `ShaderPrograms::FollowEditor()`): saved files and code that is out of date compile nothing; the editor's compiles are taken in the next frame as if made here (`ShaderPrograms::Inject()`), only successful ones. A shader nothing compiled yet still compiles in the game at its first use. Material files saved are still read by the game itself. `ApplyRendererEdit()` (`LiveEdits.h`) applies the two messages.
- Tests: `NyxLiveEditTests` (the messages read back as written, the project's root never on the wire, their descriptions); `NyxShaderProgramsTests` (following the editor, a saved file compiles nothing, an injected compile is an update with its SPIR-V, a failed one isn't taken, a material without code compiles); `NyxSceneGpuTests` (a renderer following the editor: a saved shader isn't compiled, the editor's compiles sent as messages are drawn, the editor's values too); `NyxMaterialEditorTests` (the values the Parameters window sends are handed on for the games). The editor's part with real games (PlaySession, NyxGame's link layer) is tested by hand: the game-instance tests run a stand-in program.

### Stage 3, change 2: material instances

- `.nyxmat`: `Parent = Materials/Stone.nyxmat` instead of a `Shader` line (`MaterialAsset::GetParent()`/`SetParent()`; neither is among the values). With both, the parent wins.
- `VulkanMaterials` resolves a material's chain of parents (`Resolve()`): its shader is the one the chain ends in, its values are the farthest parent's with each nearer one's over them and its own last. A material's new values, its file read again or set in memory (`SetValues()`), write its instances again in the same update (`WriteInstancesOf()`); a shader's compile reaches them through their shader. A chain that goes round, a parent that can't be read, or a chain without a shader draws as the missing material, logged once per material until it changes. Entries are in the map before their parents are looked for, so a chain that comes back is found, not loaded again.
- The editor's view of a material (`MaterialView`) gains `Inherited` (what an instance takes from its parents, merged) and `Shader` (the one it is drawn with).
- Parameters window: a value an instance takes from its parents shows the parent's value, marked *Parent* (from which material, in its tooltip); changing it overrides it, and Reset goes back to the parent's value, not the shader's default. The Material section: *Parent* (None or another material; choosing one replaces the Shader line, choosing None puts the parents' shader back with the values taken from them, so the look stays; each one undo step: `MaterialDocument::SetValues()`), *Open* the parent, *Shader* (the parent's, not editable, for an instance), and *Instances*, the materials whose parent this is (read from their files at most once a second), each opened by a click.
- *New Material Instance* on a material's right-click menu in the asset browser: `Materials/<Name>Instance.nyxmat` (numbered when taken) with only its parent, opened in the Materials workspace.
- Games: nothing new. A game resolves parents itself from the files; the editor's values, of a parent or an instance, reach it as `MaterialValues` (change 1) and its instances follow there too.
- Tests: `NyxMaterialTests` (the parent is read, isn't a value, and setting it changes its line alone); `NyxMaterialEditorTests` (an instance's file and name, numbered; several lines as one undo step, which undo brings back whole); `NyxSceneGpuTests` (an instance draws its parent's value at its own strength, follows its parent saved and set in memory, its own value wins; its view has the parent's shader and values; parents that go round are the missing material).

## To decide before stage 1

1. ~~**Slang** as the material language, or GLSL with shaderc.~~ Slang: the spike (change 1) found nothing that needs the fallback.
2. **Text `.nyxmat` files** (recommended), or binary like scenes.
3. **External editor first** and the built-in one in stage 4 (recommended), or the built-in one sooner.
4. **Tonemapper:** Khronos PBR Neutral (recommended: colours stay true, which is what judging a material needs), or a filmic one such as AgX. Either can become a setting per view later.
5. Whether **a world per scene view** goes ahead of this plan as its own change.
