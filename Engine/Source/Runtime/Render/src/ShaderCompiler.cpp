#include "ShaderCompiler.h"

#include <slang/slang-com-ptr.h>
#include <slang/slang-tag-version.h>
#include <slang/slang.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// After windows.h, which it needs
#include <delayimp.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

namespace Nyx
{
	namespace
	{
		using Slang::ComPtr;
		using Clock = std::chrono::steady_clock;

		std::string BlobText(slang::IBlob* blob)
		{
			if (!blob)
			{
				return {};
			}
			return std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
		}

		std::vector<uint32_t> BlobWords(slang::IBlob* blob)
		{
			std::vector<uint32_t> words(blob->getBufferSize() / sizeof(uint32_t));
			std::memcpy(words.data(), blob->getBufferPointer(), words.size() * sizeof(uint32_t));
			return words;
		}

		// The name of the SPIR-V's first OpEntryPoint: what a pipeline stage is made with
		std::string SpirvEntryPointName(const std::vector<uint32_t>& words)
		{
			constexpr uint32_t OpEntryPoint = 15;
			for (size_t index = 5; index < words.size();)
			{
				const uint32_t wordCount = words[index] >> 16;
				if (wordCount == 0)
				{
					break;
				}
				if ((words[index] & 0xFFFF) == OpEntryPoint && wordCount > 3)
				{
					return reinterpret_cast<const char*>(&words[index + 3]);
				}
				index += wordCount;
			}
			return {};
		}

		std::optional<EShaderStage> StageOf(SlangStage stage)
		{
			switch (stage)
			{
			case SLANG_STAGE_VERTEX:
				return EShaderStage::Vertex;
			case SLANG_STAGE_FRAGMENT:
				return EShaderStage::Fragment;
			case SLANG_STAGE_COMPUTE:
				return EShaderStage::Compute;
			default:
				return std::nullopt;
			}
		}

		// The engine's GLSL: the stage comes from the extension
		std::optional<SlangStage> GlslStageOf(const std::filesystem::path& file)
		{
			const std::string extension = file.extension().string();
			if (extension == ".vert")
			{
				return SLANG_STAGE_VERTEX;
			}
			if (extension == ".frag")
			{
				return SLANG_STAGE_FRAGMENT;
			}
			if (extension == ".comp")
			{
				return SLANG_STAGE_COMPUTE;
			}
			return std::nullopt;
		}

		bool IsIdentifier(const std::string& text)
		{
			static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]*");
			return std::regex_match(text, identifier);
		}

		// "nyx.surface" as imported is the module "nyx/surface" once loaded
		std::string LoadedModuleName(const std::string& importName)
		{
			std::string name = importName;
			std::replace(name.begin(), name.end(), '.', '/');
			return name;
		}

		slang::IModule* FindLoadedModule(slang::ISession* session, const std::string& name)
		{
			for (SlangInt index = 0; index < session->getLoadedModuleCount(); ++index)
			{
				slang::IModule* module = session->getLoadedModule(index);
				if (module->getName() == name)
				{
					return module;
				}
			}
			return nullptr;
		}

		// -------------------------------------------------------------------------
		// Sessions
		// -------------------------------------------------------------------------

		enum class ESourceKind
		{
			// Materials and the shader library. Scalar layout, which is what ByteAddressBuffer.Load<T>()
			// reads, so reflection's offsets are the GPU's; column-major matrices, so mul(Model, v)
			// takes a matrix straight from glm; fields without defaults start at zero.
			Slang,
			// The engine's GLSL, with its own layout qualifiers. Slang calls GLSL's column-major
			// "row-major": with its column-major setting, every matrix would be read transposed.
			Glsl,
		};

		ComPtr<slang::ISession> MakeSession(slang::IGlobalSession* global, ESourceKind kind, const std::vector<std::filesystem::path>& searchPaths)
		{
			slang::TargetDesc target{};
			target.format = SLANG_SPIRV;
			target.profile = global->findProfile("spirv_1_6");
			target.forceGLSLScalarBufferLayout = kind == ESourceKind::Slang;

			std::vector<slang::CompilerOptionEntry> options;
			const auto addOption = [&options](slang::CompilerOptionName name, int value)
			{
				slang::CompilerOptionEntry entry{};
				entry.name = name;
				entry.value.kind = slang::CompilerOptionValueKind::Int;
				entry.value.intValue0 = value;
				options.push_back(entry);
			};
			// Entry points keep their names, so one SPIR-V module can hold several
			addOption(slang::CompilerOptionName::VulkanUseEntryPointName, 1);
			if (kind == ESourceKind::Slang)
			{
				addOption(slang::CompilerOptionName::ZeroInitialize, 1);
			}
			else
			{
				addOption(slang::CompilerOptionName::Language, SLANG_SOURCE_LANGUAGE_GLSL);
			}

			std::vector<std::string> pathTexts;
			for (const std::filesystem::path& path : searchPaths)
			{
				pathTexts.push_back(path.generic_string());
			}
			std::vector<const char*> paths;
			for (const std::string& text : pathTexts)
			{
				paths.push_back(text.c_str());
			}

			slang::SessionDesc desc{};
			desc.targets = &target;
			desc.targetCount = 1;
			desc.defaultMatrixLayoutMode = kind == ESourceKind::Slang ? SLANG_MATRIX_LAYOUT_COLUMN_MAJOR : SLANG_MATRIX_LAYOUT_ROW_MAJOR;
			desc.allowGLSLSyntax = kind == ESourceKind::Glsl;
			desc.searchPaths = paths.data();
			desc.searchPathCount = static_cast<SlangInt>(paths.size());
			desc.compilerOptionEntries = options.data();
			desc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

			ComPtr<slang::ISession> session;
			global->createSession(desc, session.writeRef());
			return session;
		}

		// -------------------------------------------------------------------------
		// Reflection into ShaderLayout
		// -------------------------------------------------------------------------

		std::string FullTypeName(slang::TypeReflection* type)
		{
			ComPtr<slang::IBlob> name;
			if (type && SLANG_SUCCEEDED(type->getFullName(name.writeRef())))
			{
				return BlobText(name);
			}
			return type && type->getName() ? type->getName() : "";
		}

		std::vector<ShaderAttribute> ReadAttributes(slang::VariableReflection* variable)
		{
			std::vector<ShaderAttribute> attributes;
			for (unsigned index = 0; index < variable->getUserAttributeCount(); ++index)
			{
				slang::Attribute* source = variable->getUserAttributeByIndex(index);
				ShaderAttribute attribute;
				attribute.Name = source->getName();
				for (uint32_t argumentIndex = 0; argumentIndex < source->getArgumentCount(); ++argumentIndex)
				{
					ShaderAttributeArgument argument;
					size_t size = 0;
					float floatValue = 0.0f;
					int intValue = 0;
					if (const char* text = source->getArgumentValueString(argumentIndex, &size))
					{
						argument.bText = true;
						argument.Text.assign(text, size);
					}
					else if (SLANG_SUCCEEDED(source->getArgumentValueFloat(argumentIndex, &floatValue)))
					{
						argument.Number = floatValue;
					}
					else if (SLANG_SUCCEEDED(source->getArgumentValueInt(argumentIndex, &intValue)))
					{
						argument.Number = intValue;
					}
					attribute.Arguments.push_back(std::move(argument));
				}
				attributes.push_back(std::move(attribute));
			}
			return attributes;
		}

		EShaderScalarType ScalarTypeOf(slang::TypeReflection::ScalarType scalar)
		{
			switch (scalar)
			{
			case slang::TypeReflection::ScalarType::Float32:
				return EShaderScalarType::Float;
			case slang::TypeReflection::ScalarType::Int32:
				return EShaderScalarType::Int;
			case slang::TypeReflection::ScalarType::UInt32:
				return EShaderScalarType::UInt;
			case slang::TypeReflection::ScalarType::Bool:
				return EShaderScalarType::Bool;
			default:
				return EShaderScalarType::None;
			}
		}

		void ReadFields(slang::TypeLayoutReflection* layout, std::vector<ShaderField>& fields);

		// What a field's type is: kind, scalar, shape, and for structs their members. An array
		// is described by its element.
		void ReadType(slang::TypeLayoutReflection* layout, ShaderField& field)
		{
			using Kind = slang::TypeReflection::Kind;
			if (layout->getKind() == Kind::Array)
			{
				field.ArrayCount = static_cast<uint32_t>(layout->getElementCount());
				field.ArrayStride = static_cast<uint32_t>(layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM));
				layout = layout->getElementTypeLayout();
			}

			switch (layout->getKind())
			{
			case Kind::Scalar:
				field.Scalar = ScalarTypeOf(layout->getScalarType());
				field.Kind = field.Scalar != EShaderScalarType::None ? EShaderFieldKind::Scalar : EShaderFieldKind::Other;
				break;
			case Kind::Vector:
				field.Scalar = ScalarTypeOf(layout->getScalarType());
				field.Columns = static_cast<uint32_t>(layout->getElementCount());
				field.Kind = field.Scalar != EShaderScalarType::None ? EShaderFieldKind::Vector : EShaderFieldKind::Other;
				break;
			case Kind::Matrix:
				field.Scalar = ScalarTypeOf(layout->getScalarType());
				field.Rows = layout->getRowCount();
				field.Columns = layout->getColumnCount();
				field.Kind = EShaderFieldKind::Matrix;
				break;
			case Kind::Struct:
				if (FullTypeName(layout->getType()) == "nyx.Texture")
				{
					field.Kind = EShaderFieldKind::Texture;
				}
				else
				{
					field.Kind = EShaderFieldKind::Struct;
					ReadFields(layout, field.Fields);
				}
				break;
			default:
				field.Kind = EShaderFieldKind::Other;
				break;
			}
		}

		void ReadFields(slang::TypeLayoutReflection* layout, std::vector<ShaderField>& fields)
		{
			for (unsigned index = 0; index < layout->getFieldCount(); ++index)
			{
				slang::VariableLayoutReflection* source = layout->getFieldByIndex(index);
				ShaderField field;
				field.Name = source->getName();
				field.TypeName = FullTypeName(source->getType());
				field.Offset = static_cast<uint32_t>(source->getOffset());
				field.Size = static_cast<uint32_t>(source->getTypeLayout()->getSize());

				slang::VariableReflection* variable = source->getVariable();
				field.Attributes = ReadAttributes(variable);
				ReadType(source->getTypeLayout(), field);

				if (variable->hasDefaultValue() && field.ArrayCount == 0 && field.Kind == EShaderFieldKind::Scalar)
				{
					float floatValue = 0.0f;
					int64_t intValue = 0;
					if (field.Scalar == EShaderScalarType::Float && SLANG_SUCCEEDED(variable->getDefaultValueFloat(&floatValue)))
					{
						field.ScalarDefault = floatValue;
					}
					else if (field.Scalar != EShaderScalarType::Float && SLANG_SUCCEEDED(variable->getDefaultValueInt(&intValue)))
					{
						field.ScalarDefault = static_cast<double>(intValue);
					}
				}
				fields.push_back(std::move(field));
			}
		}

		ShaderLayout ReadLayout(slang::TypeLayoutReflection* layout)
		{
			ShaderLayout result;
			result.TypeName = FullTypeName(layout->getType());
			result.Size = static_cast<uint32_t>(layout->getSize());
			result.Alignment = static_cast<uint32_t>(layout->getAlignment());
			ReadFields(layout, result.Fields);
			return result;
		}

		// -------------------------------------------------------------------------
		// One compile: collects what Slang says and what the files depend on
		// -------------------------------------------------------------------------

		struct Compile
		{
			std::filesystem::path File;
			ShaderCompileResult Result;
			Clock::time_point Start = Clock::now();
			std::string DiagnosticText;

			void Take(slang::IBlob* diagnostics)
			{
				DiagnosticText += BlobText(diagnostics);
			}

			// A problem of Nyx's own, not Slang's: shown like Slang's
			void Fail(const std::filesystem::path& file, const std::string& message)
			{
				ShaderDiagnostic diagnostic;
				diagnostic.File = file;
				diagnostic.Severity = EShaderDiagnosticSeverity::Error;
				diagnostic.Message = message;
				Result.Diagnostics.push_back(std::move(diagnostic));
			}

			void AddDependency(const std::filesystem::path& file)
			{
				const std::filesystem::path path = file.lexically_normal();
				if (std::find(Result.Dependencies.begin(), Result.Dependencies.end(), path) == Result.Dependencies.end())
				{
					Result.Dependencies.push_back(path);
				}
			}

			void AddDependencies(slang::IModule* module)
			{
				if (!module)
				{
					return;
				}
				for (SlangInt32 index = 0; index < module->getDependencyFileCount(); ++index)
				{
					AddDependency(module->getDependencyFilePath(index));
				}
			}

			// Links the parts, specialised with `specialization` when they are generic, and adds
			// the code of each entry point
			bool AddEntryPoints(slang::ISession* session, std::vector<slang::IComponentType*> parts, slang::TypeReflection* specialization)
			{
				ComPtr<slang::IBlob> diagnostics;
				ComPtr<slang::IComponentType> program;
				session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), program.writeRef(), diagnostics.writeRef());
				Take(diagnostics);
				if (!program)
				{
					return false;
				}
				if (program->getSpecializationParamCount() > 0)
				{
					if (!specialization)
					{
						return false;
					}
					const slang::SpecializationArg argument = slang::SpecializationArg::fromType(specialization);
					ComPtr<slang::IComponentType> specialized;
					diagnostics = nullptr;
					program->specialize(&argument, 1, specialized.writeRef(), diagnostics.writeRef());
					Take(diagnostics);
					if (!specialized)
					{
						return false;
					}
					program = specialized;
				}

				ComPtr<slang::IComponentType> linked;
				diagnostics = nullptr;
				program->link(linked.writeRef(), diagnostics.writeRef());
				Take(diagnostics);
				if (!linked)
				{
					return false;
				}

				slang::ProgramLayout* layout = linked->getLayout();
				const SlangUInt entryPointCount = layout ? layout->getEntryPointCount() : 0;
				for (SlangUInt index = 0; index < entryPointCount; ++index)
				{
					ComPtr<slang::IBlob> code;
					diagnostics = nullptr;
					linked->getEntryPointCode(static_cast<SlangInt>(index), 0, code.writeRef(), diagnostics.writeRef());
					Take(diagnostics);
					const std::optional<EShaderStage> stage = StageOf(layout->getEntryPointByIndex(index)->getStage());
					if (!code || !stage)
					{
						return false;
					}
					CompiledEntryPoint entryPoint;
					entryPoint.Spirv = BlobWords(code);
					entryPoint.Name = SpirvEntryPointName(entryPoint.Spirv);
					entryPoint.Stage = *stage;
					Result.EntryPoints.push_back(std::move(entryPoint));
				}
				return true;
			}

			ShaderCompileResult Finish(bool bSucceeded)
			{
				std::vector<ShaderDiagnostic> parsed = ParseSlangDiagnostics(DiagnosticText);
				Result.Diagnostics.insert(Result.Diagnostics.begin(), parsed.begin(), parsed.end());

				// A file that didn't load lists nothing it depends on: the file itself, and every file
				// a diagnostic names (an error in an imported file names that file), are reloaded on
				// change all the same
				if (!File.empty())
				{
					AddDependency(std::filesystem::absolute(File));
				}
				for (const ShaderDiagnostic& diagnostic : Result.Diagnostics)
				{
					if (!diagnostic.File.empty() && std::filesystem::exists(diagnostic.File))
					{
						AddDependency(std::filesystem::absolute(diagnostic.File));
					}
				}
				Result.bSucceeded = bSucceeded && !Result.HasErrors();
				if (!Result.bSucceeded)
				{
					Result.EntryPoints.clear();
				}
				Result.Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
				return std::move(Result);
			}
		};

		bool ReadTextFile(const std::filesystem::path& file, std::string& outText)
		{
			std::ifstream stream(file, std::ios::binary);
			if (!stream)
			{
				return false;
			}
			std::ostringstream text;
			text << stream.rdbuf();
			outText = text.str();
			return true;
		}

		// -------------------------------------------------------------------------
		// Materials
		// -------------------------------------------------------------------------

		// The first array among `fields` and their members, as "Field" or "Field.Member"
		std::string FindArrayField(const std::vector<ShaderField>& fields, const std::string& prefix = {})
		{
			for (const ShaderField& field : fields)
			{
				if (field.ArrayCount > 0)
				{
					return prefix + field.Name;
				}
				const std::string inner = FindArrayField(field.Fields, prefix + field.Name + ".");
				if (!inner.empty())
				{
					return inner;
				}
			}
			return {};
		}

		struct LoadedMaterial
		{
			ComPtr<slang::ISession> Session;
			slang::IModule* Module = nullptr;
			slang::TypeReflection* Type = nullptr;
		};

		// Loads the material's module in a session of its own, finds its struct and reads its layout
		// into compile.Result. False, with the reason in `compile`, when it can't be compiled.
		bool LoadMaterial(Compile& compile, slang::IGlobalSession* global, const std::vector<std::filesystem::path>& libraryPaths, const std::filesystem::path& file, LoadedMaterial& out)
		{
			const std::string typeName = file.stem().string();
			if (file.extension() != ".slang" || !IsIdentifier(typeName))
			{
				compile.Fail(file, "a material is a .slang file named like its struct, e.g. Stone.slang with struct Stone");
				return false;
			}
			if (!std::filesystem::exists(file))
			{
				compile.Fail(file, "file not found");
				return false;
			}

			std::vector<std::filesystem::path> searchPaths{ file.parent_path() };
			searchPaths.insert(searchPaths.end(), libraryPaths.begin(), libraryPaths.end());
			out.Session = MakeSession(global, ESourceKind::Slang, searchPaths);

			ComPtr<slang::IBlob> diagnostics;
			out.Module = out.Session->loadModule(typeName.c_str(), diagnostics.writeRef());
			compile.Take(diagnostics);
			compile.AddDependencies(out.Module);
			if (!out.Module)
			{
				return false;
			}
			out.Type = out.Module->getLayout()->findTypeByName(typeName.c_str());
			if (!out.Type || out.Type->getKind() != slang::TypeReflection::Kind::Struct)
			{
				compile.Fail(file, "no struct named " + typeName + ": a material's struct is named like its file");
				return false;
			}

			compile.Result.MaterialLayout = ReadLayout(out.Session->getTypeLayout(out.Type));

			// Slang 2026.1 crashes generating code for a struct with an array after two other fields,
			// once it is loaded from a buffer and handed to a function, as every material is. Refused
			// before any code is generated, so a material can't take the editor down.
			const std::string array = FindArrayField(compile.Result.MaterialLayout->Fields);
			if (!array.empty())
			{
				compile.Fail(file, array + " is an array: materials can't have arrays yet, Slang " + std::string(SLANG_TAG_VERSION) + " crashes compiling them");
				return false;
			}
			return true;
		}
	}

	std::vector<ShaderDiagnostic> ParseSlangDiagnostics(std::string_view text)
	{
		static const std::regex header(R"(^(.*)\((\d+)\): (error|warning|note|fatal error|internal error)(?: (\d+))?: (.*)$)");

		std::vector<std::string> lines;
		std::istringstream stream{ std::string(text) };
		for (std::string line; std::getline(stream, line);)
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			lines.push_back(line);
		}

		std::vector<ShaderDiagnostic> diagnostics;
		for (size_t index = 0; index < lines.size(); ++index)
		{
			std::smatch match;
			if (!std::regex_match(lines[index], match, header))
			{
				continue;
			}
			ShaderDiagnostic diagnostic;
			diagnostic.File = match[1].str().empty() ? std::filesystem::path() : std::filesystem::path(match[1].str()).lexically_normal();
			diagnostic.Line = static_cast<uint32_t>(std::stoul(match[2].str()));
			const std::string severity = match[3].str();
			diagnostic.Severity = severity == "warning" ? EShaderDiagnosticSeverity::Warning
				: severity == "note"                    ? EShaderDiagnosticSeverity::Note
														: EShaderDiagnosticSeverity::Error;
			diagnostic.Code = match[4].matched ? std::stoi(match[4].str()) : 0;
			diagnostic.Message = match[5].str();

			// The source line follows, then the caret under the column
			if (index + 2 < lines.size())
			{
				const std::string& caretLine = lines[index + 2];
				const size_t caret = caretLine.find('^');
				if (caret != std::string::npos && caretLine.find_first_not_of(" \t^~") == std::string::npos)
				{
					diagnostic.Column = static_cast<uint32_t>(caret) + 1;
				}
			}
			diagnostics.push_back(std::move(diagnostic));
		}
		return diagnostics;
	}

	// -------------------------------------------------------------------------
	// ShaderCompiler
	// -------------------------------------------------------------------------

	struct ShaderCompiler::SlangGlobal
	{
		ComPtr<slang::IGlobalSession> Session;
	};

	ShaderCompiler::ShaderCompiler(std::vector<std::filesystem::path> searchPaths)
		: SearchPaths(std::move(searchPaths))
	{
		// slang.dll is delay-loaded (Engine/Source/CMakeLists.txt): load it now, so its absence
		// is a message rather than a crash on the first call
		if (FAILED(__HrLoadAllImportsForDll("slang.dll")))
		{
			Problem = "slang.dll could not be loaded; it belongs next to the program (nyx_copy_shader_compiler_next_to())";
			return;
		}

		// GLSL support, for the engine's GLSL; it loads slang-glsl-module.dll
		SlangGlobalSessionDesc desc{};
		desc.enableGLSL = true;
		auto global = std::make_unique<SlangGlobal>();
		if (SLANG_FAILED(slang::createGlobalSession(&desc, global->Session.writeRef())) || !global->Session)
		{
			Problem = "Slang's global session could not be made";
			return;
		}
		Global = std::move(global);
	}

	ShaderCompiler::~ShaderCompiler() = default;

	bool ShaderCompiler::IsAvailable() const
	{
		return Global != nullptr;
	}

	const std::string& ShaderCompiler::GetProblem() const
	{
		return Problem;
	}

	std::string_view ShaderCompiler::GetSlangVersion()
	{
		return SLANG_TAG_VERSION;
	}

	ShaderCompileResult ShaderCompiler::CompileProgram(const std::filesystem::path& file)
	{
		Compile compile;
		compile.File = file;
		if (!IsAvailable())
		{
			compile.Fail(file, Problem);
			return compile.Finish(false);
		}

		std::vector<std::filesystem::path> searchPaths{ file.parent_path() };
		searchPaths.insert(searchPaths.end(), SearchPaths.begin(), SearchPaths.end());

		// GLSL: one stage per file, entry point "main". Compiled the way slangc does, with the
		// entry point named before the file is read: loaded as a module and its main found
		// afterwards (findAndCheckEntryPoint()), Slang 2026.1 drops layout(local_size_x = ...) in;
		// and a compute shader runs with workgroups of one.
		if (const std::optional<SlangStage> stage = GlslStageOf(file))
		{
			if (!std::filesystem::exists(file))
			{
				compile.Fail(file, "file not found");
				return compile.Finish(false);
			}
			ComPtr<slang::ISession> session = MakeSession(Global->Session, ESourceKind::Glsl, searchPaths);
			ComPtr<SlangCompileRequest> request;
			if (SLANG_FAILED(session->createCompileRequest(request.writeRef())) || !request)
			{
				compile.Fail(file, "Slang couldn't make a compile request");
				return compile.Finish(false);
			}
			const int unit = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_GLSL, nullptr);
			request->addTranslationUnitSourceFile(unit, file.generic_string().c_str());
			const int entryIndex = request->addEntryPoint(unit, "main", *stage);
			const SlangResult result = request->compile();
			if (const char* text = request->getDiagnosticOutput())
			{
				compile.DiagnosticText += text;
			}
			// Asked after a failed compile, Slang 2026.1 crashes; the file itself and the files the
			// diagnostics name are dependencies all the same (Compile::Finish())
			for (int index = 0; SLANG_SUCCEEDED(result) && index < request->getDependencyFileCount(); ++index)
			{
				if (const char* dependency = request->getDependencyFilePath(index))
				{
					compile.AddDependency(dependency);
				}
			}
			ComPtr<slang::IBlob> code;
			if (SLANG_FAILED(result) || SLANG_FAILED(request->getEntryPointCodeBlob(entryIndex, 0, code.writeRef())) || !code)
			{
				return compile.Finish(false);
			}
			CompiledEntryPoint entryPoint;
			entryPoint.Spirv = BlobWords(code);
			entryPoint.Name = SpirvEntryPointName(entryPoint.Spirv);
			entryPoint.Stage = *StageOf(*stage);
			compile.Result.EntryPoints.push_back(std::move(entryPoint));
			return compile.Finish(true);
		}

		// Slang: every entry point the file defines
		if (file.extension() != ".slang")
		{
			compile.Fail(file, "not a shader: .slang, .vert, .frag or .comp");
			return compile.Finish(false);
		}
		if (!std::filesystem::exists(file))
		{
			compile.Fail(file, "file not found");
			return compile.Finish(false);
		}
		ComPtr<slang::ISession> session = MakeSession(Global->Session, ESourceKind::Slang, searchPaths);
		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* module = session->loadModule(file.stem().string().c_str(), diagnostics.writeRef());
		compile.Take(diagnostics);
		compile.AddDependencies(module);
		if (!module)
		{
			return compile.Finish(false);
		}
		if (module->getDefinedEntryPointCount() == 0)
		{
			compile.Fail(file, "no entry points: mark one with [shader(\"vertex\")], [shader(\"fragment\")] or [shader(\"compute\")]");
			return compile.Finish(false);
		}
		std::vector<ComPtr<slang::IEntryPoint>> entryPoints(module->getDefinedEntryPointCount());
		std::vector<slang::IComponentType*> parts{ module };
		for (SlangInt32 index = 0; index < module->getDefinedEntryPointCount(); ++index)
		{
			module->getDefinedEntryPoint(index, entryPoints[index].writeRef());
			parts.push_back(entryPoints[index]);
		}
		return compile.Finish(compile.AddEntryPoints(session, parts, nullptr));
	}

	ShaderCompileResult ShaderCompiler::CompileMaterial(const std::filesystem::path& file, const ShaderDomain& domain)
	{
		Compile compile;
		compile.File = file;
		if (!IsAvailable())
		{
			compile.Fail(file, Problem);
			return compile.Finish(false);
		}
		LoadedMaterial material;
		if (!LoadMaterial(compile, Global->Session, SearchPaths, file, material))
		{
			return compile.Finish(false);
		}

		// The domain's module, as the material imported it
		slang::IModule* domainModule = FindLoadedModule(material.Session, LoadedModuleName(domain.Module));
		if (!domainModule)
		{
			compile.Fail(file, "a material of this kind imports " + domain.Module + " (import " + domain.Module + ";)");
			return compile.Finish(false);
		}

		// Each entry point on its own. The domain's module must come first in the parts: without it,
		// or after the material, Slang 2026.1 fails with an internal error while generating code.
		bool bSucceeded = true;
		for (const std::string& name : domain.EntryPoints)
		{
			ComPtr<slang::IEntryPoint> entryPoint;
			if (SLANG_FAILED(domainModule->findEntryPointByName(name.c_str(), entryPoint.writeRef())) || !entryPoint)
			{
				compile.Fail(file, domain.Module + " has no entry point " + name);
				bSucceeded = false;
				continue;
			}
			const bool bGeneric = entryPoint->getSpecializationParamCount() > 0;
			std::vector<slang::IComponentType*> parts = bGeneric ? std::vector<slang::IComponentType*>{ domainModule, material.Module, entryPoint }
																 : std::vector<slang::IComponentType*>{ domainModule, entryPoint };
			bSucceeded = compile.AddEntryPoints(material.Session, parts, material.Type) && bSucceeded;
		}
		return compile.Finish(bSucceeded);
	}

	ShaderCompileResult ShaderCompiler::CompileDefaultsWriter(const std::filesystem::path& file)
	{
		Compile compile;
		compile.File = file;
		if (!IsAvailable())
		{
			compile.Fail(file, Problem);
			return compile.Finish(false);
		}
		// The material first, so its own problems are reported as its own
		LoadedMaterial material;
		if (!LoadMaterial(compile, Global->Session, SearchPaths, file, material))
		{
			return compile.Finish(false);
		}

		// Generated per material: a generic writer, S(), fails inside Slang 2026.1
		const std::string typeName = file.stem().string();
		const std::string moduleName = typeName + "_NyxDefaults";
		const std::string source =
			"import " + typeName + ";\n"
								   "[vk::binding(3, 0)] RWByteAddressBuffer NyxDefaultsOut;\n"
								   "[shader(\"compute\")]\n"
								   "[numthreads(1, 1, 1)]\n"
								   "void writeDefaults() { NyxDefaultsOut.Store<" +
			typeName + ">(0, " + typeName + "()); }\n";
		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* writer = material.Session->loadModuleFromSourceString(moduleName.c_str(), (moduleName + ".slang").c_str(), source.c_str(), diagnostics.writeRef());
		compile.Take(diagnostics);
		if (!writer)
		{
			return compile.Finish(false);
		}
		ComPtr<slang::IEntryPoint> entryPoint;
		writer->findEntryPointByName("writeDefaults", entryPoint.writeRef());
		if (!entryPoint)
		{
			compile.Fail(file, "the defaults writer has no entry point");
			return compile.Finish(false);
		}
		return compile.Finish(compile.AddEntryPoints(material.Session, { writer, entryPoint }, nullptr));
	}
}
