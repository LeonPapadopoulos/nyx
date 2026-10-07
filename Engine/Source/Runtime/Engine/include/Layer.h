#pragma once

namespace Nyx::Engine
{
	class Application;

	// A self-contained part of an application, such as the editor, that plugs into
	// the application's frame loop. Override only the hooks the layer needs.
	class ILayer
	{
	public:
		virtual ~ILayer() = default;

		// Called once when the layer is pushed. The window and renderer already exist.
		virtual void OnAttach(Application& /*application*/) {}

		// Called once during shutdown, while the renderer is still alive.
		virtual void OnDetach() {}

		// Called every frame before rendering; deltaTime is in seconds.
		virtual void OnUpdate(float /*deltaTime*/) {}

		// Called while a frame's UI is built; draw ImGui windows here.
		virtual void OnUI() {}
	};
}
