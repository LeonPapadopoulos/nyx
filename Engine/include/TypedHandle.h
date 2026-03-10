#pragma once

// Typed Handle meant to prevent type mismatches at compile time
template<typename Tag>
struct TypedHandle
{
	// [generation (32 bits)][index (32 bits)]
	uint64_t Value = 0;

	static constexpr TypedHandle Make(uint32_t index, uint32_t generation)
	{
		TypedHandle newHandle{};
		newHandle.Value = static_cast<uint64_t>(index) |
			(static_cast<uint64_t>(generation) << 32);
		return newHandle;
	}

	constexpr uint32_t Index() const
	{
		// Return bits 0-31 (least significant)
		return static_cast<uint32_t>(Value);
	}

	constexpr uint32_t Generation() const
	{	
		// Return bits 32-64 (most significant)
		return static_cast<uint32_t>(Value >> 32);
	}

	constexpr bool IsValid() const
	{
		return Value != 0;
	}

	constexpr explicit operator bool() const
	{
		return IsValid();
	}

	// requires at least std:c++20
	//friend constexpr bool operator==(TypedHandle, TypedHandle) = default;

	friend constexpr bool operator==(const TypedHandle& left, const TypedHandle& right)
	{
		return left.Value == right.Value;
	}

	friend constexpr bool operator!=(const TypedHandle& left, const TypedHandle& right)
	{
		return left.Value != right.Value;
	}
};