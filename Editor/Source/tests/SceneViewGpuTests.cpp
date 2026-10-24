// The editor's scene views on a real GPU: any number opened and closed, each with its own camera,
// the Debug menu's culling and shadow views reaching views opened later, and the views' section of
// the layout files read back. Needs a Vulkan device; its window stays hidden.
#include "DirectionalLightComponent.h"
#include "EditorCameraController.h"
#include "EditorPreferences.h"
#include "InputSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "SceneEditor.h"
#include "SceneViewSet.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;
	using Nyx::RenderTests::SceneRenderer;
	using Nyx::Tests::Require;

	constexpr uint32_t ViewWidth = 320;
	constexpr uint32_t ViewHeight = 180;

	// Where the first view looks from: down at the plane and the box, from the front
	const glm::vec3 CameraPosition(0.5f, 6.0f, 9.0f);
	const glm::vec3 CameraRotation(glm::radians(-32.0f), 0.0f, 0.0f);

	// What the editor has around its scene views
	struct EditorRig
	{
		explicit EditorRig(SceneRenderer& gpu)
			: Gpu(gpu)
		{
			Registry& world = Editing.Scene.GetRegistry();
			Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
			AddMesh(world, TransformComponent{ .Scale = glm::vec3(24.0f, 1.0f, 24.0f) }, gpu.Renderer.GetPlaneMesh(), material);
			AddMesh(world, TransformComponent{ .Position = glm::vec3(-1.5f, 1.5f, 0.0f) }, gpu.Renderer.GetCubeMesh(), material);
			const Entity sun = world.CreateEntity();
			world.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f, 10.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, -60.0f, 0.0f))) });
			world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
			gpu.Renderer.SetWorld(&world);
		}

		~EditorRig()
		{
			Views.Detach();
			Gpu.Renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		static void AddMesh(Registry& world, const TransformComponent& transform, Mesh* mesh, Material* material)
		{
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, transform);
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = mesh;
			meshRenderer.MaterialAsset = material;
			world.Add<MeshRendererComponent>(entity, meshRenderer);
		}

		// A frame with the views drawn in their windows, as the editor draws them
		void DrawFrame()
		{
			Gpu.Renderer.DrawFrame([this]()
				{
					Views.Draw(Editing, Preferences, Cameras);
				});
		}

		// What a view shows, at the test's size, drawn without the windows
		RgbaImage Picture(const SceneViewPanel& view)
		{
			static_cast<IEditorRenderer&>(Gpu.Renderer).SetSceneViewSize(view.GetViewId(), ViewWidth, ViewHeight);
			Gpu.DrawFrames(2);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(view.GetViewId(), image), "The scene view can't be read back");
			return image;
		}

		bool RendererHas(uint64_t sceneViewId) const
		{
			const std::vector<uint64_t> ids = static_cast<const IEditorRenderer&>(Gpu.Renderer).GetSceneViewIds();
			return std::find(ids.begin(), ids.end(), sceneViewId) != ids.end();
		}

		SceneRenderer& Gpu;
		SceneEditor Editing;
		EditorPreferences Preferences;
		Nyx::Input::InputSystem Input;
		EditorCameraController Cameras{ Input };
		SceneViewSet Views;
	};

	std::array<uint8_t, 3> Middle(const RgbaImage& image)
	{
		const size_t offset = (static_cast<size_t>(image.Height / 2) * image.Width + image.Width / 2) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	std::vector<int> Numbers(const SceneViewSet& views)
	{
		std::vector<int> numbers;
		for (const std::unique_ptr<SceneViewPanel>& view : views.GetViews())
		{
			numbers.push_back(view->GetNumber());
		}
		return numbers;
	}

	void TestOpenAndClose(SceneRenderer& gpu)
	{
		EditorRig editor(gpu);
		SceneViewSet& views = editor.Views;
		views.Attach(gpu.Renderer, nullptr);
		Require(views.GetViews().size() == 1 && views.GetViews().front()->GetTitle() == "Scene", "The editor should start with one view, \"Scene\"");
		gpu.Renderer.SetSceneViewEditorCameraTransform(views.GetViews().front()->GetViewId(), CameraPosition, CameraRotation);

		for (int index = 0; index < 5; ++index)
		{
			views.Open();
		}
		Require(Numbers(views) == std::vector<int>{ 1, 2, 3, 4, 5, 6 }, "New views should be numbered one after the other");
		Require(views.GetTitles() == std::vector<std::string>{ "Scene", "Scene 2", "Scene 3", "Scene 4", "Scene 5", "Scene 6" }, "The views' windows should be named by their numbers");
		for (int frame = 0; frame < 3; ++frame)
		{
			editor.DrawFrame();
		}
		gpu.DrawUntilShadersCompiled();

		// Each its own scene view and camera, starting where the first looks from
		for (const std::unique_ptr<SceneViewPanel>& view : views.GetViews())
		{
			Require(editor.RendererHas(view->GetViewId()), view->GetTitle() + " should have a scene view in the renderer");
			glm::vec3 position;
			glm::vec3 rotation;
			Require(gpu.Renderer.GetSceneViewEditorCameraTransform(view->GetViewId(), position, rotation) && position == CameraPosition && rotation == CameraRotation,
				view->GetTitle() + " should start looking from where the first view looks");
			const SceneViewCullingStats* culling = gpu.Renderer.GetSceneViewCullingStats(view->GetViewId());
			Require(culling && culling->Objects == 2 && culling->ObjectsCulled == 0, view->GetTitle() + " should cull by its own camera, and see the plane and the box");
		}
		const uint64_t third = views.GetViews()[2]->GetViewId();
		gpu.Renderer.SetSceneViewEditorCameraTransform(third, glm::vec3(0.0f, 6.0f, 200.0f), glm::vec3(0.0f, glm::radians(180.0f), 0.0f));
		editor.DrawFrame();
		Require(gpu.Renderer.GetSceneViewCullingStats(third)->ObjectsCulled == 2, "A view looking away should cull everything");
		Require(gpu.Renderer.GetSceneViewCullingStats(views.GetViews()[1]->GetViewId())->ObjectsCulled == 0, "Moving one view's camera shouldn't move another's");

		// Closing two
		const uint64_t second = views.GetViews()[1]->GetViewId();
		views.GetViews()[1]->bOpen = false;
		views.GetViews()[3]->bOpen = false;
		editor.DrawFrame();
		editor.DrawFrame();
		Require(Numbers(views) == std::vector<int>{ 1, 3, 5, 6 }, "Closed views should go");
		Require(!editor.RendererHas(second), "A closed view's scene view should be destroyed once the frame is done with it");

		// The lowest number free again
		Require(views.Open().GetNumber() == 2, "A new view should take the lowest number free");
		editor.DrawFrame();

		// Every view closed at once: the last stays, and is still drawn
		for (const std::unique_ptr<SceneViewPanel>& view : views.GetViews())
		{
			view->bOpen = false;
		}
		editor.DrawFrame();
		editor.DrawFrame();
		Require(views.GetViews().size() == 1 && views.GetViews().front()->bOpen, "The last view should stay open");
		Require(editor.RendererHas(views.GetViews().front()->GetViewId()), "The last view should keep its scene view");
	}

	void TestDebugViewsReachNewViews(SceneRenderer& gpu)
	{
		EditorRig editor(gpu);
		SceneViewSet& views = editor.Views;
		views.Attach(gpu.Renderer, nullptr);
		const SceneViewPanel& first = *views.GetViews().front();
		gpu.Renderer.SetSceneViewEditorCameraTransform(first.GetViewId(), CameraPosition, CameraRotation);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage lit = editor.Picture(first);

		// Debug > Shadow Cascades: the views open and those opened after tint the plane by cascade
		views.SetShadowDebug(ESceneViewShadowDebug::Cascades);
		const SceneViewPanel& later = views.Open();
		const RgbaImage tinted = editor.Picture(later);
		Require(!Nyx::RenderTests::Near(Middle(tinted), Middle(lit), 20),
			"A view opened with Shadow Cascades on should tint the plane: " + Nyx::RenderTests::Text(Middle(tinted)) + ", lit " + Nyx::RenderTests::Text(Middle(lit)));
		Require(Nyx::RenderTests::Near(Middle(editor.Picture(first)), Middle(tinted), 2), "The view open when Shadow Cascades was chosen should tint the plane too");
		views.SetShadowDebug(ESceneViewShadowDebug::Off);
		Require(Nyx::RenderTests::Near(Middle(editor.Picture(later)), Middle(lit), 2), "Shadow Cascades off should light the plane again");

		// Debug > Bloom: the views open and those opened after show their bloom alone, black in a
		// world without an environment, under the editor's grid
		views.SetViewDebug(ESceneViewDebug::Bloom);
		const SceneViewPanel& bloomed = views.Open();
		const auto dark = [&](const std::array<uint8_t, 3>& rgb)
		{
			return int(rgb[0]) + int(rgb[1]) + int(rgb[2]) < 80 && int(Middle(lit)[0]) + int(Middle(lit)[1]) + int(Middle(lit)[2]) > 200;
		};
		const std::array<uint8_t, 3> bloomedMiddle = Middle(editor.Picture(bloomed));
		const std::array<uint8_t, 3> firstMiddle = Middle(editor.Picture(first));
		Require(dark(bloomedMiddle) && dark(firstMiddle),
			"Views open and opened with Bloom on should show the bloom alone: " + Nyx::RenderTests::Text(bloomedMiddle) + ", " + Nyx::RenderTests::Text(firstMiddle) + ", lit " +
				Nyx::RenderTests::Text(Middle(lit)));
		views.SetViewDebug(ESceneViewDebug::Lit);
		Require(Nyx::RenderTests::Near(Middle(editor.Picture(bloomed)), Middle(lit), 2), "Bloom off should show the lit scene again");

		// Debug > Ambient Occlusion: white in a world without an environment, under the grid
		views.SetViewDebug(ESceneViewDebug::AmbientOcclusion);
		const SceneViewPanel& occluded = views.Open();
		const std::array<uint8_t, 3> occludedMiddle = Middle(editor.Picture(occluded));
		Require(int(occludedMiddle[0]) + int(occludedMiddle[1]) + int(occludedMiddle[2]) > 600 && !Nyx::RenderTests::Near(occludedMiddle, Middle(lit), 20),
			"Views opened with Ambient Occlusion on should show it: " + Nyx::RenderTests::Text(occludedMiddle));
		views.SetViewDebug(ESceneViewDebug::Lit);
		Require(Nyx::RenderTests::Near(Middle(editor.Picture(occluded)), Middle(lit), 2), "Ambient Occlusion off should show the lit scene again");

		// Debug > Freeze Culling: a view opened after culls by where its camera was at first
		views.SetCullingDebug(ESceneViewCullingDebug::Frozen);
		const SceneViewPanel& frozen = views.Open();
		editor.Picture(frozen);
		gpu.Renderer.SetSceneViewEditorCameraTransform(frozen.GetViewId(), glm::vec3(0.0f, 6.0f, 200.0f), glm::vec3(0.0f, glm::radians(180.0f), 0.0f));
		editor.Picture(frozen);
		Require(gpu.Renderer.GetSceneViewCullingStats(frozen.GetViewId())->ObjectsCulled == 0, "A view opened while culling is frozen should cull by where its camera was");
		views.SetCullingDebug(ESceneViewCullingDebug::Off);
		editor.Picture(frozen);
		Require(gpu.Renderer.GetSceneViewCullingStats(frozen.GetViewId())->ObjectsCulled == 2, "Culling unfrozen should cull by the camera again");
	}

	void TestSectionReadsBack(SceneRenderer& gpu)
	{
		std::vector<std::string> lines;
		{
			EditorRig editor(gpu);
			SceneViewSet& views = editor.Views;
			views.Attach(gpu.Renderer, nullptr);
			views.Open();
			views.Open();
			views.GetViews()[1]->bOpen = false;
			editor.DrawFrame();
			gpu.Renderer.SetSceneViewEditorCameraTransform(views.GetViews()[0]->GetViewId(), glm::vec3(1.25f, -2.0f, 0.1f), glm::vec3(0.3f, -1.7f, 0.0f));
			gpu.Renderer.SetSceneViewEditorCameraTransform(views.GetViews()[1]->GetViewId(), glm::vec3(-7.0f, 3.5f, 1e-3f), glm::vec3(-0.25f, 3.0f, 0.0f));
			views.GetViews()[1]->SetShowsGameView(true);
			lines = views.Write();
			Require(lines.size() == 2 && lines[0].starts_with("View=1,0,") && lines[1].starts_with("View=3,1,"), "Each view should be a line with its number and whether it shows the game");
		}

		EditorRig editor(gpu);
		SceneViewSet& views = editor.Views;
		views.Attach(gpu.Renderer, nullptr);
		std::vector<std::string> damaged = lines;
		damaged.insert(damaged.begin() + 1, "View=7,0,1,2");
		damaged.push_back("View=3,0,0,0,0,0,0,0");
		damaged.push_back("Something=1");
		views.Read(damaged);
		Require(Numbers(views) == std::vector<int>{ 1, 3 }, "The views should come back as they were, a damaged line and a number twice skipped");
		Require(views.Write() == lines, "The views' cameras and game views should read back exactly");
		Require(views.GetViews()[1]->IsShowingGameView() && !views.GetViews()[0]->IsShowingGameView(), "Each view should show the game as it did");
		editor.DrawFrame();

		views.Read({ "Nonsense" });
		Require(Numbers(views) == std::vector<int>{ 1, 3 }, "A section without a view should keep the views open");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxEditorSceneViewGpuTests");

		TestOpenAndClose(gpu);
		TestDebugViewsReachNewViews(gpu);
		TestSectionReadsBack(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor scene view GPU test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor scene view GPU tests passed.\n";
	return 0;
}
