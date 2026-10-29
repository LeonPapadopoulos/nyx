// Cloud modifiers without a GPU (CloudModifiers.h), as nyx.volumetricclouds does them to every density
// sample: a shape's strength over its life (nothing before its start, fading in, for ever or for its
// duration, fading out); a cleared column keeping none in its middle and all beyond its soft edge; a
// thickened ring adding most at its radius; a swirl turning the clouds' lookup by its angle within
// its radius and less beyond, further the longer it goes on; nothing outside a shape's heights; the
// nearest shapes a view takes; a world's modifiers; a lightning entity's channel, a strike's lingering
// and closing, a column's with its swirl; the component saved and loaded.
#include "CloudModifierComponent.h"
#include "CloudModifiers.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "Lightning.h"
#include "LightningComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	CloudModifier Shape(ECloudModifierShape shape, float strength)
	{
		CloudModifier modifier;
		modifier.Shape = shape;
		modifier.Radius = 300.0f;
		modifier.Width = 200.0f;
		modifier.Strength = strength;
		modifier.FadeIn = 0.0f;
		return modifier;
	}

	void TestFade()
	{
		CloudModifier modifier;
		modifier.StartTime = 10.0f;
		modifier.FadeIn = 2.0f;
		modifier.Duration = 5.0f;
		modifier.FadeOut = 4.0f;
		Require(GetCloudModifierFade(modifier, 9.0f) == 0.0f, "Nothing before its start");
		Require(Near(GetCloudModifierFade(modifier, 11.0f), 0.5f), "Half way through its fade in, half of it");
		Require(GetCloudModifierFade(modifier, 14.0f) == 1.0f, "All of it during its duration");
		Require(Near(GetCloudModifierFade(modifier, 17.0f), 0.5f), "Half way through its fade out, half of it");
		Require(GetCloudModifierFade(modifier, 19.5f) == 0.0f, "Nothing after its fade out");
		modifier.Duration = 0.0f;
		Require(GetCloudModifierFade(modifier, 1000.0f) == 1.0f, "Without a duration, for ever");
		Require(!MakeRenderCloudModifier(modifier, glm::vec3(0.0f), 5.0f).has_value(), "Gone before its start");
	}

	void TestShapes()
	{
		const glm::vec3 center(1000.0f, 50.0f, -2000.0f);
		const glm::vec2 middle(center.x, center.z);
		std::vector<RenderCloudModifier> modifiers;

		// A cleared column: none left in its middle with a strength of 1, all beyond its edge
		modifiers = { *MakeRenderCloudModifier(Shape(ECloudModifierShape::ClearColumn, 1.0f), center, 1.0f) };
		Require(Near(GetCloudModifierEffect(modifiers, middle, 800.0f).Keep, 0.0f), "A cleared column keeps nothing in its middle");
		Require(Near(GetCloudModifierEffect(modifiers, middle + glm::vec2(500.0f, 0.0f), 800.0f).Keep, 1.0f), "Past its edge it keeps everything");
		const float edge = GetCloudModifierEffect(modifiers, middle + glm::vec2(300.0f, 0.0f), 800.0f).Keep;
		Require(edge > 0.4f && edge < 0.6f, "At its radius, half way through its soft edge");
		modifiers[0].Strength = 0.5f;
		Require(Near(GetCloudModifierEffect(modifiers, middle, 800.0f).Keep, 0.5f), "Half its strength clears half");

		// Its heights: nothing outside them, soft at their ends
		CloudModifier banded = Shape(ECloudModifierShape::ClearColumn, 1.0f);
		banded.Bottom = 500.0f;
		banded.Top = 1500.0f;
		modifiers = { *MakeRenderCloudModifier(banded, center, 1.0f) };
		Require(GetCloudModifierEffect(modifiers, middle, 300.0f).Keep == 1.0f && GetCloudModifierEffect(modifiers, middle, 1700.0f).Keep == 1.0f, "Nothing below or above its heights");
		Require(Near(GetCloudModifierEffect(modifiers, middle, 1000.0f).Keep, 0.0f), "All of it between them");

		// A thickened ring: the most at its radius, none in its middle or far out
		modifiers = { *MakeRenderCloudModifier(Shape(ECloudModifierShape::ThickenRing, 2.0f), center, 1.0f) };
		Require(Near(GetCloudModifierEffect(modifiers, middle + glm::vec2(0.0f, 300.0f), 800.0f).Add, 2.0f), "A ring adds its strength at its radius");
		Require(GetCloudModifierEffect(modifiers, middle, 800.0f).Add < 0.01f && GetCloudModifierEffect(modifiers, middle + glm::vec2(900.0f, 0.0f), 800.0f).Add < 0.01f,
			"Nothing in its middle or far out");

		// A swirl: the lookup turned back by its angle within its radius, less beyond; further the
		// longer it goes on
		const CloudModifier swirl = Shape(ECloudModifierShape::Swirl, 2.0f);
		const RenderCloudModifier tenSeconds = *MakeRenderCloudModifier(swirl, center, 10.0f);
		Require(Near(tenSeconds.Angle, 2.0f * glm::two_pi<float>() / 60.0f * 10.0f, 1e-4f), "A swirl turns as many turns a minute as asked");
		Require(MakeRenderCloudModifier(swirl, center, 20.0f)->Angle > tenSeconds.Angle, "Further the longer it goes on");
		modifiers = { tenSeconds };
		const glm::vec2 inside = GetCloudModifierEffect(modifiers, middle + glm::vec2(100.0f, 0.0f), 800.0f).Lookup - middle;
		Require(Near(glm::length(inside), 100.0f, 1e-2f) && Near(std::atan2(inside.y, inside.x), -tenSeconds.Angle, 1e-3f), "Within its radius the lookup is turned back by its angle");
		const glm::vec2 outside = GetCloudModifierEffect(modifiers, middle + glm::vec2(600.0f, 0.0f), 800.0f).Lookup - middle;
		Require(Near(std::atan2(outside.y, outside.x), -tenSeconds.Angle * 0.25f, 1e-3f), "Twice its radius out, a quarter of it");
	}

	void TestWorldAndView()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(10.0f, 0.0f, 20.0f) });
		CloudModifierComponent component;
		component.Shapes = { Shape(ECloudModifierShape::ClearColumn, 1.0f), Shape(ECloudModifierShape::ThickenRing, 1.0f) };
		component.Shapes[1].StartTime = 100.0f;
		world.Add<CloudModifierComponent>(entity, component);
		std::vector<RenderCloudModifier> modifiers;
		ExtractCloudModifiers(world, 50.0f, modifiers);
		Require(modifiers.size() == 1 && modifiers[0].Center == glm::vec2(10.0f, 20.0f) && modifiers[0].SourceEntity == entity, "A world's shapes on now, round their entity");
		world.Get<CloudModifierComponent>(entity).bEnabled = false;
		modifiers.clear();
		ExtractCloudModifiers(world, 150.0f, modifiers);
		Require(modifiers.empty(), "Off, none");

		// The nearest a view takes
		std::vector<RenderCloudModifier> many;
		for (int index = 0; index < 12; ++index)
		{
			RenderCloudModifier modifier;
			modifier.Center = glm::vec2(1000.0f * static_cast<float>(12 - index), 0.0f);
			modifier.Radius = 100.0f;
			modifier.Width = 100.0f;
			many.push_back(modifier);
		}
		std::vector<RenderCloudModifier> picked;
		PickViewCloudModifiers(many, glm::vec2(0.0f), picked);
		Require(picked.size() == MaxCloudModifiers && picked.front().Center.x == 1000.0f && picked.back().Center.x == 8000.0f, "A view takes the nearest shapes");
	}

	void TestLightningChannel()
	{
		LightningComponent lightning;
		lightning.StartTime = 0.0f;
		lightning.Interval = 5.0f;
		lightning.IntervalJitter = 0.0f;
		lightning.ChannelLinger = 6.0f;
		lightning.StrikeRadius = 100.0f;
		const LightningComponent clamped = GetClampedLightning(lightning);
		std::vector<RenderCloudModifier> modifiers;
		AddLightningChannelModifiers(lightning, glm::vec3(0.0f), 0.0f, Entity{}, modifiers);
		Require(modifiers.empty(), "At a strike's start the hole hasn't opened yet");
		modifiers.clear();
		AddLightningChannelModifiers(lightning, glm::vec3(0.0f), 1.0f, Entity{}, modifiers);
		Require(modifiers.size() == 2 && modifiers[0].Shape == ECloudModifierShape::ClearColumn && Near(modifiers[0].Strength, clamped.ChannelClear) &&
				modifiers[1].Shape == ECloudModifierShape::ThickenRing && Near(modifiers[1].Radius, clamped.ChannelRadius * 2.0f),
			"After a strike, a cleared column and a ring round it");
		const glm::vec3 top = GetLightningStrikePlace(clamped, glm::vec3(0.0f), 0).Top;
		Require(modifiers[0].Center == glm::vec2(top.x, top.z), "The hole is where the strike starts");
		// Two strikes' channels at once, the first closing
		modifiers.clear();
		AddLightningChannelModifiers(lightning, glm::vec3(0.0f), 5.5f, Entity{}, modifiers);
		Require(modifiers.size() == 4 && modifiers[0].Strength < modifiers[2].Strength, "The strike before still closing beside the new one");
		modifiers.clear();
		AddLightningChannelModifiers(lightning, glm::vec3(0.0f), 6.5f, Entity{}, modifiers);
		Require(modifiers.size() == 2, "After its linger, a strike's channel is gone");

		// A column: its swirl too, while it stands
		LightningComponent column = lightning;
		column.Mode = ELightningMode::Sustained;
		column.Duration = 3.0f;
		column.bRepeat = false;
		modifiers.clear();
		AddLightningChannelModifiers(column, glm::vec3(5.0f, 0.0f, 5.0f), 2.0f, Entity{}, modifiers);
		Require(modifiers.size() == 3 && modifiers[2].Shape == ECloudModifierShape::Swirl && modifiers[2].Angle > 0.0f && modifiers[0].Center == glm::vec2(5.0f, 5.0f),
			"A column's channel swirls round its axis");
		modifiers.clear();
		AddLightningChannelModifiers(column, glm::vec3(0.0f), 3.0f + 6.0f + 0.1f, Entity{}, modifiers);
		Require(modifiers.empty(), "Its linger after it ends, then none");
		column.bChannel = false;
		modifiers.clear();
		AddLightningChannelModifiers(column, glm::vec3(0.0f), 2.0f, Entity{}, modifiers);
		Require(modifiers.empty(), "Channel off, none");
	}

	void TestSavedAndLoaded()
	{
		Nyx::Tests::TestDirectory directory("NyxCloudModifierTests");
		const std::filesystem::path scenePath = directory.Path / "Modifiers.nyxscene";
		Registry world;
		CloudModifierComponent component;
		CloudModifier swirl = Shape(ECloudModifierShape::Swirl, 2.5f);
		swirl.Bottom = 400.0f;
		swirl.Top = 1600.0f;
		swirl.Duration = 9.0f;
		component.Shapes = { swirl };
		world.Add<CloudModifierComponent>(world.CreateEntity(), component);
		Require(SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with a cloud modifier failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(scenePath, loaded, ScenePostLoadContext{}), "Loading a scene with a cloud modifier failed");
		std::optional<CloudModifierComponent> read;
		loaded.Each<CloudModifierComponent>([&](Entity, const CloudModifierComponent& value)
			{
				read = value;
			});
		Require(read.has_value() && read->Shapes.size() == 1 && read->Shapes[0].Shape == ECloudModifierShape::Swirl && read->Shapes[0].Strength == 2.5f &&
				read->Shapes[0].Top == 1600.0f && read->Shapes[0].Duration == 9.0f,
			"The cloud modifier didn't load as saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();
		TestFade();
		TestShapes();
		TestWorldAndView();
		TestLightningChannel();
		TestSavedAndLoaded();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All cloud modifier tests passed\n";
	return 0;
}
