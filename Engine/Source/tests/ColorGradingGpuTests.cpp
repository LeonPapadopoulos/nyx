// Colour grading on a real GPU (ColorGradingComponent). The view's lookup table, read back: at the
// defaults, every texel the colour it stands for; graded, what ColorGrading.h works out on the CPU.
// It is baked when the grade changes, not every frame, nor for a change too small to show. Under a
// black sky in thick fog the whole picture is the fog's colour, tonemapped, so a graded pixel is the
// CPU's grade of it: warmer, more saturated, its shadows lifted; with the keys, by the sun's height,
// halfway between two keys halfway, the same wherever the camera is, a kilometre and a half up too.
// Without the component, or with it off, the picture is the same to the bit. A crate yard in fog
// under a low sun, graded by keys for the golden hour, looks as its reference picture does; the
// colour grading test scene loads and is graded.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference after a
// change that is meant to change the picture. NyxColorGradingGpuTests --write-scene writes
// Assets/Scenes/ColorGradingTest.nyxscene.
#include "CameraComponent.h"
#include "ColorGrading.h"
#include "ColorGradingComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "Paths.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"
#include "AssetResolver.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("ColorGrading-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	std::array<uint8_t, 3> Middle(const RgbaImage& image)
	{
		return PixelAt(image, image.Width / 2, image.Height / 2);
	}

	TransformComponent Placed(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = rotation, .Scale = scale };
	}

	// What the grade makes of linear light the tonemap shows, as the 8-bit picture holds it
	std::array<uint8_t, 3> GradedPixel(const ColorGrade& grade, const glm::vec3& light)
	{
		const glm::vec3 shown = GradeColor(MakeColorGradingConstants(grade), LinearToSrgb(TonemapPbrNeutral(light)));
		std::array<uint8_t, 3> pixel{};
		for (int channel = 0; channel < 3; ++channel)
		{
			pixel[channel] = static_cast<uint8_t>(std::lround(std::clamp(shown[channel], 0.0f, 1.0f) * 255.0f));
		}
		return pixel;
	}

	void RequireNear(const std::array<uint8_t, 3>& actual, const std::array<uint8_t, 3>& expected, int tolerance, const std::string& what)
	{
		Require(Near(actual, expected, tolerance), what + ": " + Text(actual) + ", expected " + Text(expected));
	}

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, Registry& world, ESceneViewSky sky)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		renderer.SetSceneViewSky(view, sky, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A grade that moves every control
	ColorGrade EveryControl()
	{
		ColorGrade grade;
		grade.Temperature = 0.4f;
		grade.Tint = -0.2f;
		grade.Contrast = 1.2f;
		grade.Lift = glm::vec3(0.03f, 0.01f, 0.05f);
		grade.Gamma = glm::vec3(1.1f, 1.0f, 0.9f);
		grade.Gain = glm::vec3(1.05f, 1.0f, 0.9f);
		grade.Saturation = 1.3f;
		grade.ShadowTint = glm::vec3(0.3f, 0.5f, 1.0f);
		grade.ShadowTintStrength = 0.4f;
		grade.HighlightTint = glm::vec3(1.0f, 0.75f, 0.4f);
		grade.HighlightTintStrength = 0.5f;
		grade.TintBalance = 0.2f;
		return grade;
	}

	ColorGradingKey Key(float sunElevation, const ColorGrade& grade)
	{
		return ColorGradingKey{ .SunElevation = sunElevation, .Grade = grade };
	}

	// Grades for a painted golden hour, by the sun's height: a cool, dim night; a gold-green dusk
	// just above the horizon, its shadows teal and lifted; a warm backlight a little higher, gold
	// highlights over violet shadows; and a clear noon, hardly graded
	std::vector<ColorGradingKey> GoldenHourKeys()
	{
		ColorGrade night;
		night.Temperature = -0.45f;
		night.Tint = 0.05f;
		night.Contrast = 1.05f;
		night.Lift = glm::vec3(0.0f, 0.005f, 0.02f);
		night.Gain = glm::vec3(0.85f, 0.9f, 1.0f);
		night.Saturation = 0.7f;
		night.ShadowTint = glm::vec3(0.3f, 0.4f, 1.0f);
		night.ShadowTintStrength = 0.3f;

		ColorGrade goldGreen;
		goldGreen.Temperature = 0.35f;
		goldGreen.Tint = -0.3f;
		goldGreen.Contrast = 1.05f;
		goldGreen.Lift = glm::vec3(0.01f, 0.025f, 0.0f);
		goldGreen.Gamma = glm::vec3(1.02f, 1.06f, 0.95f);
		goldGreen.Saturation = 1.15f;
		goldGreen.ShadowTint = glm::vec3(0.35f, 0.6f, 0.45f);
		goldGreen.ShadowTintStrength = 0.35f;
		goldGreen.HighlightTint = glm::vec3(1.0f, 0.85f, 0.35f);
		goldGreen.HighlightTintStrength = 0.45f;
		goldGreen.TintBalance = 0.1f;

		ColorGrade backlight;
		backlight.Temperature = 0.5f;
		backlight.Tint = 0.05f;
		backlight.Contrast = 1.1f;
		backlight.Lift = glm::vec3(0.01f, 0.0f, 0.02f);
		backlight.Gain = glm::vec3(1.06f, 1.0f, 0.9f);
		backlight.Saturation = 1.2f;
		backlight.ShadowTint = glm::vec3(0.45f, 0.4f, 0.9f);
		backlight.ShadowTintStrength = 0.35f;
		backlight.HighlightTint = glm::vec3(1.0f, 0.7f, 0.35f);
		backlight.HighlightTintStrength = 0.5f;
		backlight.TintBalance = -0.1f;

		ColorGrade noon;
		noon.Temperature = 0.05f;
		noon.Contrast = 1.05f;
		noon.Saturation = 1.1f;
		noon.ShadowTint = glm::vec3(0.6f, 0.75f, 1.0f);
		noon.ShadowTintStrength = 0.15f;
		noon.HighlightTint = glm::vec3(1.0f, 0.95f, 0.85f);
		noon.HighlightTintStrength = 0.1f;

		return { Key(-8.0f, night), Key(3.0f, goldGreen), Key(12.0f, backlight), Key(50.0f, noon) };
	}

	// A camera under a black sky in fog thick enough that every pixel is the fog's own colour,
	// uniform at every height: the picture is that colour tonemapped, wherever the camera is. A sun
	// that gives no light, whose height chooses the grade.
	struct FlatScene
	{
		Registry World;
		Entity Camera;
		Entity Sun;
		Entity Environment;
		Entity Grading;

		explicit FlatScene(const glm::vec3& fogColor)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 2.0f, 0.0f)));
			World.Add<CameraComponent>(Camera, CameraComponent{});

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f)));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);
			SetSun(40.0f);

			Environment = World.CreateEntity();
			EnvironmentComponent environment;
			environment.SkyMode = ESkyMode::CubeMap;
			environment.bBloom = false;
			environment.bAmbientOcclusion = false;
			environment.FogDensity = 0.5f;
			environment.FogHeightFalloff = 0.0f;
			environment.FogColor = fogColor;
			environment.FogSunScattering = 0.0f;
			World.Add<EnvironmentComponent>(Environment, environment);

			Grading = World.CreateEntity();
		}

		void SetFog(const glm::vec3& color)
		{
			World.Get<EnvironmentComponent>(Environment).FogColor = color;
		}

		// The sun `degrees` above the horizon, behind the camera
		void SetSun(float degrees)
		{
			World.Get<TransformComponent>(Sun).Rotation = glm::angleAxis(glm::radians(-degrees), glm::vec3(1.0f, 0.0f, 0.0f));
		}

		ColorGradingComponent& Grade(const ColorGrade& grade)
		{
			ColorGradingComponent grading;
			grading.Grade = grade;
			if (World.Has<ColorGradingComponent>(Grading))
			{
				World.Get<ColorGradingComponent>(Grading) = grading;
			}
			else
			{
				World.Add<ColorGradingComponent>(Grading, grading);
			}
			return World.Get<ColorGradingComponent>(Grading);
		}
	};

	size_t TableIndex(uint32_t x, uint32_t y, uint32_t z)
	{
		return (static_cast<size_t>(z) * ColorGradingTableSize + y) * ColorGradingTableSize + x;
	}

	// The largest difference between the view's table and what the grade gives on the CPU
	float TableDifference(SceneRenderer& gpu, uint64_t view, const ColorGrade& grade)
	{
		const std::vector<glm::vec4> table = gpu.Renderer.ReadSceneViewColorGradingTable(view);
		Require(table.size() == static_cast<size_t>(ColorGradingTableSize) * ColorGradingTableSize * ColorGradingTableSize, "The view has no grading table");
		const ColorGradingConstants constants = MakeColorGradingConstants(grade);
		float largest = 0.0f;
		for (uint32_t z = 0; z < ColorGradingTableSize; ++z)
		{
			for (uint32_t y = 0; y < ColorGradingTableSize; ++y)
			{
				for (uint32_t x = 0; x < ColorGradingTableSize; ++x)
				{
					const glm::vec3 expected = GradeColor(constants, GetColorGradingTableInput(glm::uvec3(x, y, z)));
					const glm::vec3 difference = glm::abs(glm::vec3(table[TableIndex(x, y, z)]) - expected);
					largest = std::max(largest, std::max(difference.x, std::max(difference.y, difference.z)));
				}
			}
		}
		return largest;
	}

	// The table, read back: the identity at the defaults, the CPU's grade when graded; baked when the
	// grade changes, not every frame, nor for a change too small to show
	void TestTable(SceneRenderer& gpu)
	{
		FlatScene scene(glm::vec3(0.4f));
		scene.Grade(ColorGrade{});
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		gpu.DrawFrames(2);
		const float identity = TableDifference(gpu, view, ColorGrade{});
		std::cout << "The identity's table differs from its colours by up to " << identity << "\n";
		Require(identity < 1e-3f, "The identity's table moves a colour by " + std::to_string(identity));

		scene.Grade(EveryControl());
		gpu.DrawFrames(2);
		const float graded = TableDifference(gpu, view, EveryControl());
		std::cout << "The graded table differs from the CPU's grade by up to " << graded << "\n";
		Require(graded < 2e-3f, "The graded table differs from the CPU's grade by " + std::to_string(graded));

		// Baked once for a change, then not again while nothing changes
		const SceneViewColorGrading& grading = *gpu.Renderer.GetSceneViewColorGrading(view);
		const uint32_t bakes = grading.BakeCount;
		gpu.DrawFrames(5);
		Require(grading.BakeCount == bakes, "The table is baked again with nothing changed");
		ColorGradingComponent& component = scene.World.Get<ColorGradingComponent>(scene.Grading);
		component.Grade.Saturation += ColorGradingBakeTolerance * 0.25f;
		gpu.DrawFrames(2);
		Require(grading.BakeCount == bakes, "The table is baked again for a change too small to show");
		component.Grade.Saturation += 0.1f;
		gpu.DrawFrames(1);
		Require(grading.BakeCount == bakes + 1, "The table isn't baked once for a change");
		// The bake's time on the GPU, read a frame later
		gpu.DrawFrames(1);
		const SceneViewGpuTimes& times = *gpu.Renderer.GetSceneViewGpuTimes(view);
		std::cout << "The bake took " << times.ColorGrading << " ms on the GPU; the picture pass " << times.Picture << " ms\n";
		Require(grading.BakeCount == bakes + 1, "The table is baked again after the change");

		// Without the component, no table
		scene.World.Remove<ColorGradingComponent>(scene.Grading);
		gpu.DrawFrames(1);
		Require(grading.Slot == 0, "The table is kept without the component");
		EndView(gpu, view);
	}

	// Without the component, with it off, and at its defaults: the picture as it was
	void TestUngraded(SceneRenderer& gpu)
	{
		const glm::vec3 fog(0.55f, 0.35f, 0.2f);
		FlatScene scene(fog);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const RgbaImage none = Draw(gpu, view);
		SaveIfAsked(none, "Ungraded");
		RequireNear(Middle(none), Tonemapped(fog), 1, "The fog's colour, tonemapped");

		ColorGradingComponent& grading = scene.Grade(EveryControl());
		grading.bGrading = false;
		const RgbaImage off = Draw(gpu, view);
		Require(off.Pixels == none.Pixels, "Grading off doesn't draw the picture as without the component, to the bit");

		scene.Grade(ColorGrade{});
		const RgbaImage identity = Draw(gpu, view);
		RequireNear(Middle(identity), Middle(none), 1, "The defaults change the picture");

		scene.World.Remove<ColorGradingComponent>(scene.Grading);
		const RgbaImage removed = Draw(gpu, view);
		Require(removed.Pixels == none.Pixels, "The component removed doesn't draw the picture as before, to the bit");

		// A material's debug view isn't graded
		scene.Grade(EveryControl());
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::BaseColor);
		gpu.DrawFrames(2);
		Require(gpu.Renderer.GetSceneViewColorGrading(view)->Slot == 0, "A debug view of the materials is graded");
		gpu.Renderer.SetSceneViewDebug(view, ESceneViewDebug::Lit);
		EndView(gpu, view);
	}

	int RedOverBlue(const std::array<uint8_t, 3>& rgb)
	{
		return int(rgb[0]) - int(rgb[2]);
	}

	// Graded pixels are the CPU's grade of the picture, and move the way each control says
	void TestGradedPixels(SceneRenderer& gpu)
	{
		const glm::vec3 fog(0.35f, 0.3f, 0.25f);
		FlatScene scene(fog);
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);
		const std::array<uint8_t, 3> plain = Middle(Draw(gpu, view));

		ColorGrade warm;
		warm.Temperature = 0.6f;
		scene.Grade(warm);
		const std::array<uint8_t, 3> warmer = Middle(Draw(gpu, view));
		RequireNear(warmer, GradedPixel(warm, fog), 2, "Warmer");
		Require(RedOverBlue(warmer) > RedOverBlue(plain) + 10, "A warmer white balance doesn't warm the picture: " + Text(warmer) + " against " + Text(plain));

		ColorGrade saturated;
		saturated.Saturation = 1.8f;
		scene.Grade(saturated);
		const std::array<uint8_t, 3> vivid = Middle(Draw(gpu, view));
		RequireNear(vivid, GradedPixel(saturated, fog), 2, "More saturated");
		Require(RedOverBlue(vivid) > RedOverBlue(plain) + 10, "More saturation doesn't spread the colour: " + Text(vivid) + " against " + Text(plain));

		// The shadows lifted: a dark fog lighter, as the CPU says
		const glm::vec3 dark(0.02f, 0.02f, 0.025f);
		scene.SetFog(dark);
		const std::array<uint8_t, 3> darkPlain = Middle(Draw(gpu, view));
		ColorGrade lifted;
		lifted.Lift = glm::vec3(0.08f);
		scene.Grade(lifted);
		const std::array<uint8_t, 3> liftedShadows = Middle(Draw(gpu, view));
		RequireNear(liftedShadows, GradedPixel(lifted, dark), 2, "Lifted shadows");
		Require(int(liftedShadows[1]) > int(darkPlain[1]) + 10, "The lift doesn't lift the shadows: " + Text(liftedShadows) + " against " + Text(darkPlain));

		// Every control at once, over a few colours none of whose channels the grade clips (where
		// one is clipped, the table's texels either side round the kink off by a few steps)
		scene.Grade(EveryControl());
		for (const glm::vec3 color : { glm::vec3(0.05f, 0.08f, 0.12f), glm::vec3(0.3f, 0.25f, 0.2f), glm::vec3(0.6f, 0.62f, 0.55f), glm::vec3(1.5f, 1.3f, 1.2f) })
		{
			scene.SetFog(color);
			RequireNear(Middle(Draw(gpu, view)), GradedPixel(EveryControl(), color), 2, "Every control over " + std::to_string(color.r) + " " + std::to_string(color.g) + " " + std::to_string(color.b));
		}
		EndView(gpu, view);
	}

	// The keys by the sun's height: on a key, its grade; halfway between two, halfway; below the
	// lowest, the lowest. The same with the camera 1.5 km up.
	void TestKeys(SceneRenderer& gpu)
	{
		const glm::vec3 fog(0.3f, 0.28f, 0.22f);
		FlatScene scene(fog);
		ColorGrade low;
		low.Temperature = 0.6f;
		low.Saturation = 1.4f;
		low.HighlightTint = glm::vec3(1.0f, 0.7f, 0.3f);
		low.HighlightTintStrength = 0.6f;
		ColorGrade high;
		high.Temperature = -0.3f;
		high.Lift = glm::vec3(0.0f, 0.02f, 0.06f);
		ColorGradingComponent& grading = scene.Grade(ColorGrade{});
		grading.bUseKeys = true;
		grading.Keys = { Key(20.0f, high), Key(0.0f, low) };
		const uint64_t view = MakeView(gpu, scene.World, ESceneViewSky::Black);

		scene.SetSun(0.0f);
		RequireNear(Middle(Draw(gpu, view)), GradedPixel(low, fog), 2, "The sun on the horizon, on the low key");
		scene.SetSun(20.0f);
		RequireNear(Middle(Draw(gpu, view)), GradedPixel(high, fog), 2, "The sun 20 degrees up, on the high key");
		scene.SetSun(-15.0f);
		RequireNear(Middle(Draw(gpu, view)), GradedPixel(low, fog), 2, "The sun set, below the lowest key");
		scene.SetSun(10.0f);
		const RgbaImage halfway = Draw(gpu, view);
		SaveIfAsked(halfway, "Halfway");
		RequireNear(Middle(halfway), GradedPixel(MixColorGrades(low, high, 0.5f), fog), 2, "The sun halfway between the keys");

		// The camera a kilometre and a half up, in the same uniform fog: the same picture
		scene.World.Get<TransformComponent>(scene.Camera).Position = glm::vec3(300.0f, 1500.0f, -200.0f);
		const RgbaImage up = Draw(gpu, view);
		RequireNear(Middle(up), Middle(halfway), 0, "The grade with the camera 1.5 km up");

		// Use Keys off: the component's grade
		scene.World.Get<ColorGradingComponent>(scene.Grading).bUseKeys = false;
		RequireNear(Middle(Draw(gpu, view)), GradedPixel(ColorGrade{}, fog), 1, "Use Keys off");
		EndView(gpu, view);
	}

	// A crate yard in fog under a low sun, graded by the golden hour's keys, against
	// References/ColorGrading.png
	void TestGradedScene(SceneRenderer& gpu)
	{
		Registry world;
		Material* plastic = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const auto addMesh = [&](const TransformComponent& transform, Mesh* mesh)
		{
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, transform);
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = mesh;
			meshRenderer.MaterialAsset = plastic;
			world.Add<MeshRendererComponent>(entity, meshRenderer);
		};
		addMesh(Placed(glm::vec3(0.0f), glm::vec3(300.0f, 1.0f, 300.0f)), gpu.Renderer.GetPlaneMesh());
		for (int row = 0; row < 6; ++row)
		{
			for (int column = -2; column <= 2; ++column)
			{
				addMesh(Placed(glm::vec3(column * 6.0f + (row % 2) * 3.0f, 1.0f, -8.0f - row * 14.0f), glm::vec3(2.0f)), gpu.Renderer.GetCubeMesh());
			}
		}
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-4.0f, 0.0f, 0.0f)))));
		world.Add<CameraComponent>(camera, CameraComponent{});
		// The sun 8 degrees up, ahead and to the side: between the gold-green dusk and the backlight
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-8.0f, 155.0f, 0.0f)))));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		// The fog and the sky's cube map tinted by the sun, as the reference was made; bloom and
		// ambient occlusion have pictures of their own
		const Entity air = world.CreateEntity();
		EnvironmentComponent environment;
		environment.SkyMode = ESkyMode::TintedCubeMap;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		world.Add<EnvironmentComponent>(air, environment);
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Keys = GoldenHourKeys();
		world.Add<ColorGradingComponent>(air, grading);

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "Scene");
		world.Get<ColorGradingComponent>(air).bGrading = false;
		SaveIfAsked(Draw(gpu, view, 2), "SceneUngraded");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "ColorGrading.png");
		EndView(gpu, view);
	}

	constexpr const char* ScenePath = "Scenes/ColorGradingTest.nyxscene";

	// The atmosphere test scene, graded by the golden hour's keys, the sun as low as there
	void WriteScene()
	{
		const std::filesystem::path assets = Paths::GetAssetsDir();
		Registry world;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(assets / "Scenes/AtmosphereTest.nyxscene", world, context), "The atmosphere test scene can't be loaded");
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ "Color Grading" });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xC0109AD1ull } });
		world.Add<TransformComponent>(entity, Placed(glm::vec3(0.0f)));
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Keys = GoldenHourKeys();
		world.Add<ColorGradingComponent>(entity, grading);
		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The colour grading test scene as the game loads it: graded, by its keys
	void TestColorGradingTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The colour grading test scene can't be loaded; NyxColorGradingGpuTests --write-scene writes it");
		UpdateScatters(world);
		Entity graded;
		size_t components = 0;
		world.Each<ColorGradingComponent>([&](Entity entity, const ColorGradingComponent& grading)
			{
				graded = entity;
				++components;
				Require(grading.bUseKeys && grading.Keys.size() == 4, "The colour grading test scene's grading has no keys");
			});
		Require(components == 1, "The colour grading test scene has " + std::to_string(components) + " gradings");

		const uint64_t view = MakeView(gpu, world, ESceneViewSky::Scene);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "TestScene");
		world.Get<ColorGradingComponent>(graded).bGrading = false;
		const RgbaImage ungraded = Draw(gpu, view, 2);
		SaveIfAsked(ungraded, "TestSceneUngraded");
		size_t changed = 0;
		for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
		{
			changed += std::memcmp(&image.Pixels[pixel], &ungraded.Pixels[pixel], 3) != 0 ? 1 : 0;
		}
		Require(changed > image.Pixels.size() / 4 / 2, "The colour grading test scene's grade changes only " + std::to_string(changed) + " pixels");
		EndView(gpu, view);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		SceneRenderer gpu("NyxColorGradingGpuTests");
		TestTable(gpu);
		TestUngraded(gpu);
		TestGradedPixels(gpu);
		TestKeys(gpu);
		TestGradedScene(gpu);
		TestColorGradingTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxColorGradingGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxColorGradingGpuTests: all passed\n";
	return 0;
}
