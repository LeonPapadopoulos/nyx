#pragma once

#include "ReflectionAst.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Nyx::HeaderTool
{
	class ReflectedTypeIndex
	{
	public:
		void Add(const ParsedType& parsedType)
		{
			QualifiedNames.insert(parsedType.QualifiedName);

			const auto [it, inserted] =
				QualifiedByShortName.emplace(parsedType.Name, parsedType.QualifiedName);

			if (!inserted && it->second != parsedType.QualifiedName)
			{
				throw std::runtime_error(
					"Ambiguous reflected type short name: " + parsedType.Name);
			}
		}

		// A type of another module (--type-root) that this module's types may hold. Its short name
		// never clashes: a type of this module's with the same short name keeps it, and the other is
		// still found by its qualified name.
		void AddReferenced(const ParsedType& parsedType)
		{
			QualifiedNames.insert(parsedType.QualifiedName);
			QualifiedByShortName.emplace(parsedType.Name, parsedType.QualifiedName);
		}

		void AddEnum(const ParsedEnum& parsedEnum)
		{
			EnumQualifiedNames.insert(parsedEnum.QualifiedName);

			const auto [it, inserted] = EnumQualifiedByShortName.emplace(parsedEnum.Name, parsedEnum.QualifiedName);
			if (!inserted && it->second != parsedEnum.QualifiedName)
			{
				throw std::runtime_error("Ambiguous reflected enum short name: " + parsedEnum.Name);
			}
		}

		// An enum of another module (--type-root), as AddReferenced() for types
		void AddReferencedEnum(const ParsedEnum& parsedEnum)
		{
			EnumQualifiedNames.insert(parsedEnum.QualifiedName);
			EnumQualifiedByShortName.emplace(parsedEnum.Name, parsedEnum.QualifiedName);
		}

		std::optional<std::string> ResolveEnum(std::string_view enumName) const
		{
			const std::string key(enumName);
			if (EnumQualifiedNames.contains(key))
			{
				return key;
			}

			const auto it = EnumQualifiedByShortName.find(key);
			return it != EnumQualifiedByShortName.end() ? std::optional<std::string>(it->second) : std::nullopt;
		}

		std::optional<std::string> Resolve(std::string_view typeName) const
		{
			const std::string key(typeName);

			if (QualifiedNames.contains(key))
			{
				return key;
			}

			const auto it = QualifiedByShortName.find(key);
			if (it != QualifiedByShortName.end())
			{
				return it->second;
			}

			return std::nullopt;
		}

	private:
		std::unordered_set<std::string> QualifiedNames;
		std::unordered_map<std::string, std::string> QualifiedByShortName;
		std::unordered_set<std::string> EnumQualifiedNames;
		std::unordered_map<std::string, std::string> EnumQualifiedByShortName;
	};
}