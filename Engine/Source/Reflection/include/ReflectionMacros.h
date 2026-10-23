#pragma once

#define NYX_REFLECT(...)
#define NYX_PROPERTY(...)

// Before an enum class, for properties of its type; the header tool reads its enumerators:
//     NYX_ENUM(meta = (DisplayName = "Dead Zone Shape"))
//     enum class EDeadZoneShape : uint8_t { Axial, Radial = 1 };
// Values are saved as their numbers: only add enumerators at the end, or give them numbers.
#define NYX_ENUM(...)