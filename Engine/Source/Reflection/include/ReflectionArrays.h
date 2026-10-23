#pragma once

#include "ReflectionTypes.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace Nyx::Reflection
{
	// ArrayMetadata's functions for a std::vector<T>; the generated code points at them
	template<typename T>
	struct VectorArrayOps
	{
		static std::vector<T>& Of(void* array)
		{
			return *static_cast<std::vector<T>*>(array);
		}

		static const std::vector<T>& Of(const void* array)
		{
			return *static_cast<const std::vector<T>*>(array);
		}

		static size_t GetSize(const void* array)
		{
			return Of(array).size();
		}

		static void Resize(void* array, size_t size)
		{
			Of(array).resize(size);
		}

		static void* GetElement(void* array, size_t index)
		{
			return &Of(array)[index];
		}

		static const void* GetConstElement(const void* array, size_t index)
		{
			return &Of(array)[index];
		}

		static void Insert(void* array, size_t index)
		{
			std::vector<T>& vector = Of(array);
			vector.insert(vector.begin() + static_cast<std::ptrdiff_t>(index), T{});
		}

		static void Erase(void* array, size_t index)
		{
			std::vector<T>& vector = Of(array);
			vector.erase(vector.begin() + static_cast<std::ptrdiff_t>(index));
		}

		static void Swap(void* array, size_t a, size_t b)
		{
			std::vector<T>& vector = Of(array);
			std::swap(vector[a], vector[b]);
		}
	};
}
