#include "ImGuiSource.h"
#include "ReflectionTypes.h"
#include "TransformComponent.h"

#include <iostream>
#include <type_traits>
#include <utility>

#if NYX_UI_INSPECTION
	#error Runtime targets must not enable UI source inspection.
#endif

namespace
{
	template<typename T>
	concept HasSourceLocation = requires(T value) { value.Source; };

	static_assert(!HasSourceLocation<Nyx::Reflection::TypeMetadata>);
	static_assert(!HasSourceLocation<Nyx::Reflection::PropertyMetadata>);
	static_assert(&Nyx::UI::Begin == &ImGui::Begin);
	static_assert(&Nyx::UI::BeginPopup == &ImGui::BeginPopup);
	static_assert(&Nyx::UI::BeginPopupModal == &ImGui::BeginPopupModal);
	static_assert(std::is_lvalue_reference_v<decltype(NYX_UI(std::declval<int&>()))>);
}

int main()
{
	int evaluations = 0;
	int& result = NYX_UI(++evaluations);
	if (evaluations != 1 || &result != &evaluations) return 1;
	NYX_UI(static_cast<void>(++evaluations));
	if (evaluations != 2) return 1;

	// Runtime reflection still has the data needed for serialization and scene editing.
	const auto& type = Nyx::Reflection::GetTypeMetadata<Nyx::Engine::TransformComponent>();
	if (type.PropertyCount == 0 || type.Properties[0].NameHash == 0) return 1;

	std::cout << "Runtime reflection and UI calls work without inspection support.\n";
	return 0;
}
