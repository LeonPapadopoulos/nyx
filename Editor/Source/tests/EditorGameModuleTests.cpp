// The editor with a game module (NyxTestModule, linked as the editor links a game's module): the
// module gives the editor where its types are declared, so the source inspector can open them.
#include "GameModule.h"
#include "Log.h"
#include "ReflectionSourceRegistry.h"
#include "TestLanternComponent.h"
#include "TestSupport.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	using Nyx::Tests::Require;

	// The line `line` (from 1) of the file
	std::string ReadLine(const char* file, uint32_t line)
	{
		std::ifstream stream(file);
		std::string text;
		for (uint32_t row = 0; row < line; ++row)
		{
			std::getline(stream, text);
		}
		return text;
	}

	void TestModuleRegistersItsSources()
	{
		const GameModule& module = GetLinkedGameModule();
		Require(std::string_view(module.Name) == "NyxTestModule", "The linked module should be the test module");
		Require(module.RegisterSources != nullptr, "The editor's module should register source locations");

		module.RegisterSources();

		const ReflectionSourceRegistry& sources = ReflectionSourceRegistry::Get();
		const Reflection::TypeMetadata& type = Reflection::GetTypeMetadata<NyxTestModule::TestLanternComponent>();
		const SourceLocation typeSource = sources.Find(type);
		Require(typeSource.IsValid(), "The module's type has no source location");
		Require(std::string_view(typeSource.File).ends_with("TestLanternComponent.h"),
			"The module's type should be declared in its own header");

		bool foundBrightness = false;
		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const Reflection::PropertyMetadata& property = type.Properties[i];
			if (std::string_view(property.Name) != "Brightness")
			{
				continue;
			}
			foundBrightness = true;
			const SourceLocation source = sources.Find(property);
			Require(source.IsValid(), "The module's property has no source location");
			Require(ReadLine(source.File, source.Line).find("float Brightness") != std::string::npos,
				"The module property's source line should be its declaration");
		}
		Require(foundBrightness, "The module's type should have its Brightness property");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestModuleRegistersItsSources();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor game module tests passed.\n";
	return 0;
}
