# Shaders and materials

How shaders are compiled and reloaded, how materials are stored and drawn, and why. The code is in
`Engine/Source/Runtime/Renderer` (`ShaderCompiler`, `ShaderCache`, `ShaderPrograms`,
`ShaderCompileService`, `VulkanBindless`, `VulkanMaterials`, `MaterialAsset`,
`VulkanEnvironment`), `Engine/Shaders` and `Tools/NyxShaderCompiler`.

## Slang, for every shader

All shaders are compiled with Slang, through its API, to SPIR-V: material shaders written in
Slang, and the engine's own `.vert`, `.frag` and `.comp` files in Slang's GLSL mode. One compiler
means one set of diagnostics, dependency tracking and cache for all of them.

Slang rather than GLSL for materials, because:

- **Modules and generics instead of a preprocessor.** The engine writes a domain's entry points
  once, generic over "a surface" (`Engine/Shaders/Library/nyx/surface.slang`), and specialises
  them with the material's struct. A material file holds only the material.
- **Reflection with attributes.** Fields carry `[Range(0, 1)]`, `[Color]`, `[Category(...)]` and
  others; the editor's Parameters window is built from them, as the details panel is built from
  `NYX_PROPERTY`.
- **Layouts come from the compiler.** The CPU writes each value at the offset reflection reports,
  so no C++ struct mirrors a shader struct and drifts from it. Everything is compiled with scalar
  layout, the layout `ByteAddressBuffer.Load<T>()` reads; other layouts report offsets that
  don't match what the GPU loads.

## Building, caching and live reload

- **The build** runs `NyxShaderCompiler build`, which compiles the engine's shaders into a shader
  cache next to every program. A program starts with nothing to compile, and a game without the
  compiler loads from the cache.
- **The cache** (`ShaderCache`) keeps each compile with every file it read and a hash of their
  contents, with paths relative to a root, so finding an entry tells whether it still matches
  the files, on any machine. What live reload compiles goes to `Saved/ShaderCache`.
- **Live reload** (`ShaderPrograms`): a watcher sees a source saved, and the shader is compiled
  again 50 ms after the last change. The owner of each pipeline rebuilds it from the new code; a
  shader that fails keeps its last good code. Every engine pipeline can be rebuilt, the painter's
  and the scene's alike.
- **Compiling out of process** (`RemoteShaderCompileService`): the compiler runs in
  `NyxShaderCompiler.exe`, connected over TCP, so a crash in the compiler costs one compile, not
  the editor. Nothing blocks: requests go in, results come back on later frames.
- **Games started by the editor** compile nothing. The editor sends each compile (SPIR-V and the
  material's layout) over the editor link, so the game shows exactly what the editor shows.

## Bindless

One way of binding serves every scene pipeline (`VulkanBindless`, `bindless.slang`):

- **Set 0, global, bound once per pass:** one array of every sampled image (2D and cube), the
  fixed samplers, and one buffer with every material's values, each at its own offset.
- **Set 1, per view:** the scene's uniforms, the environment and the time (`view.slang`), and
  the point lights that reach the view, a storage buffer (`PointLights`).
- **Push constants, per draw:** the model matrix, the material's offset, the entity id, six
  values set by code (`CustomData`, `CustomData2`), such as a decal's, and the instance's custom
  numbers where a draw has no instance stream of its own (`InstanceCustom`; 112 bytes in all).

So there is no descriptor set, pool or layout per material or shader. A material is bytes at an
offset: changing a value writes those bytes, and switching an object's material changes a push
constant. All materials of one shader share its pipeline. A texture is an index into the array,
stored as a plain `uint` (`nyx::Texture`): Slang's own handle type has no default, and loads from
the wrong offset inside a struct when it isn't 8-byte aligned.

This needs Vulkan 1.2's descriptor indexing and `scalarBlockLayout`, which `VulkanContext` turns
on and requires when it chooses the GPU.

## Materials

- **A shader** is a `.slang` file in `Assets/Shaders`: a struct named like the file, with its
  parameters and their defaults, for a domain. `Standard.slang` is the one in use for most meshes,
  `VertexColor.slang` draws a mesh in its vertex colours (a generated ground's); Surface (3D
  meshes) is the only domain so far. A surface material gets `SurfaceInput`: UV, the vertex colour
  (`Color`, linear, white for meshes without one), normal, tangent, position, the direction to the
  camera, the draw's custom data, the mesh's and the instance's own numbers (`Custom`,
  `InstanceCustom`, zero unless set: [Vertex data](vertex-data.md)) and the decal's values. `[Enum("A, B, C")]` on a `uint` field makes the Parameters
  window offer the choices by name.
- **A material** is a `.nyxmat` text file: which shader, and values for some of its parameters;
  everything else takes the shader's default. Text, so it diffs and can be written by hand. Its
  lines are kept as written when the editor saves it, and values the shader doesn't know stay in
  the file, so a shader that gets the field back finds them again.
- **A material instance** names a parent material instead of a shader and sets only what it
  changes. It is drawn with its parents' values under its own; a parent's change reaches its
  instances in the same update.
- **Options** are how a material is drawn rather than its shader's values: a reflected struct,
  `MaterialOptions`, each property a line of the file named as the property (a bool's without its
  b), inherited by instances like a value, shown under *Material* in the Parameters window from its
  reflection: `TwoSided`, `CastShadows`, and `MaxVertexOffset` for a shader with a vertex
  function. Options aren't shader parameters, so `GetValues()` and *Not in the Shader* leave them
  out, and the material buffer has no room for them: the renderer reads them into the material's
  handle (`Material::bTwoSided`, `Material::bCastShadows`, `Material::MaxVertexOffset`).
- **Casting shadows.** `CastShadows = false` takes everything drawn with the material out of the
  sun's shadow maps, while it is still drawn and still receives shadows: one switch for a whole
  kind of mesh, however many entities draw it. A section casts only if its entity's Mesh Renderer
  and its material both do; an entity none of whose materials cast is left out before the cascades
  cull it, its instances with it, and an entity of both kinds has only its non-casting sections
  left out ([Shadows](shadows.md#what-casts)). Like `TwoSided`, it reaches the renderer's handle
  as soon as the material is set from the editor or saved.
- **Faces.** Every mesh Nyx makes (the built-in cube, sphere, capsule, plane and crossed quads, a
  ground, a scatter's copies) and glTF's wind counter-clockwise seen from the side their normals
  face, and the surface pipelines cull the backs: half the triangles of a closed mesh aren't shaded.
  `NyxMeshTests` checks the winding of every one. The draw sets which faces are drawn (dynamic
  cull mode and front face, `SetMeshFaces()`), so one pipeline a shader serves both kinds of
  material, and an entity whose transform mirrors it (a negative scale) is drawn with its front
  clockwise, outside still out; entities batched together are mirrored alike. A *two-sided*
  material (`TwoSided = true`) draws both sides, for single sheets such as leaves, blades and
  petals: on a back face `surfaceFragment` turns the normal round (`SV_IsFrontFace`), and the
  bitangent's side with it, so the back is lit as a front by a light on its side, and a normal
  map's bumps show mirrored through the sheet. `Materials/VertexColorTwoSided.nyxmat` is the
  vertex colours, two-sided. Picking and the selection outline draw the faces the scene pass
  draws, section by section: a click on a one-sided back goes to what is behind it. Decals are
  drawn from either side, as they lie on what they decal. Instances that mirror one by one
  (`MeshInstancesComponent` transforms of negative determinant) aren't turned round: the cull mode
  is per draw.
- **A vertex function.** A material that moves its vertices, such as one that sways in the wind or
  grows, implements `ISurfaceVertex` beside `ISurface`: `displace()` moves a vertex in the mesh's
  space or in the world, by the material's values, the mesh's and the instance's own numbers and
  the time. The compiler finds it by reflection and compiles two more entry points for it; it
  runs in the scene, the shadows and the editor's picking and outline alike, and the material's
  *Max Vertex Offset* option widens the bounds it is culled by
  ([Vertex data](vertex-data.md#the-vertex-function)). `Materials/VertexColorWind.nyxmat` and its
  two-sided instance sway in the wind.
- **Defaults:** reflection gives default values for scalars only. So for each material shader the
  engine generates a small compute shader that stores the struct made with its defaults, runs it
  once on the GPU, and keeps the bytes: a new material starts from those.
- **A material that can't be drawn** (its shader hasn't compiled yet, its file can't be read, or
  its parents go round in a circle) draws as a magenta checker (`MissingMaterial.slang`), never
  as nothing.
- **Editing:** the editor sets values on the renderer (`IRenderer::SetMaterialValues()`) as they
  change, before saving; every view shows them in the same frame, and games started by the
  editor get them over the link.

## Decals

A surface decal (`SurfaceDecalComponent`) draws a surface material on a quad laid on another
entity's surface. No domain of its own: every surface material can be a decal's.

- **Three pipelines a shader.** The Surface domain has a third entry point, `decalFragment`,
  compiled with the other two, and each material shader gets a second pipeline from it: depth
  tested without writing depth, blended. A fourth, `decalPickingFragment`, gives a third, for the
  editor's picking pass: it writes the decal's entity where the material's `SurfaceOutput.Coverage`
  is above 0.05 and leaves the rest of the quad to what is beneath. Coverage is 1 unless the
  material says less; the masked decal says its mask, revealed, without the glow. Live reload
  makes all three again; the missing material has all three, its picking one covering the quad.
- **Premultiplied alpha**, not additive: `decalFragment` gives the lit colour times the opacity
  plus the emission, and the pipeline keeps what is beneath by one minus the opacity. A glow alone
  is additive (opacity 0); a mark that covers, a burn or paint, is opacity 1; one material can do
  both. Additive blending could only ever brighten.
- **The decal's values** go in the push constants, not the material buffer: its colour and
  intensity in `CustomData`, its reveal and pulse in `CustomData2`. `decalFragment` hands them to
  the material as `SurfaceInput.Decal` (`DecalInput`); `surfaceFragment` gives the defaults (white,
  intensity 1, revealed, not pulsing), so a decal material on a mesh, as in its preview, shows as
  it would fully revealed. The time is the view's world's (`View.Time`), the same for every
  material: a game's world, drawn by its clock (`SetWorldInterpolation()`), has its simulation's
  time at the moment its poses are drawn at, so it stands still while paused and Step moves it
  on by a step (`GetDrawnWorldTime()`); other worlds, the editor's, have the seconds since the
  renderer started. A per-world time in the view's set, not a per-decal one in the push
  constants: every view knows its world, and meshes' materials get it too.
- **Drawn after the sky**, as what is blended comes after all that is opaque, in the scene pass, in
  HDR, so a glow above 1 reaches the tonemap.

The **masked decal material** (`Assets/Shaders/MaskedDecal.slang`,
`Assets/Materials/MaskedDecal.nyxmat`) is the engine's decal material, for the tests and as a
sample:

- its `Mask` is a texture, white on black, tinted by the decal's colour times its intensity; the
  edge is where the grey crosses `Threshold`, faded over `Softness` pixels by the grey's
  screen-space derivative (`fwidth()`), so it stays smooth and antialiased near and far. A mask
  whose edges fade over a few texels, like a distance field, works best. The sample mask,
  `Textures/DecalRing.png`, is a ring, made by `Scripts/MakeDecalMask.py`;
- the optional glow around it is the mask read from a smaller mip (`GlowReach` levels down), so it
  costs one more sample and reaches as far on the decal at any distance;
- `Reveal` uncovers it as a wipe along `RevealDirection`, out from the middle, or round the middle
  from `RevealStart` (`RevealMode`), with a soft, brighter edge where it moves: at 0 nothing, at 1
  all;
- `Pulse` dims it by up to the pulse and back, `PulseRate` times a second, on `View.Time`;
- it gives off light only; `Cover` lets the mask cover what is beneath too.

## Light

Materials are judged under physically based light, so the lighting is a fixed baseline rather
than a look:

- glTF's metallic-roughness model: GGX distribution, height-correlated Smith visibility, Schlick
  Fresnel and Lambert diffuse, for the sun (`lighting.slang`), shadowed by the sun's cascaded
  shadow maps (`shadows.slang`, [Shadows](shadows.md)); the sky's light isn't shadowed;
- light from the sky (`VulkanEnvironment`, `Environment.slang`): a specular cube prefiltered by
  roughness into its mips, diffuse light as nine spherical harmonics, and a split-sum lookup
  table, made on the GPU when the sky loads;
- scene views render in HDR (`R16G16B16A16Sfloat`) with an exposure and are tonemapped into the
  8-bit picture; the editor's overlays are drawn after tonemapping.

`NyxMaterialGpuTests` renders a grid of spheres (metallic by row, roughness by column), under a
sun that casts no shadows so no sphere is judged in another's, and compares it with an approved
reference image, and runs the furnace test: under a uniform sky,
white dielectric spheres send back the sky's colour.

## Not done

- Domains other than Surface (sprites, UI, post-processing).
- Decals projected onto curved surfaces: a decal is a flat quad on the tangent plane.
- `[Switch]` parameters are plain bools in the material buffer, not specialisation constants.
- Meshes as assets: scenes use the built-in cube and sphere.
- Shaders are edited in an external editor; there is no built-in one.
