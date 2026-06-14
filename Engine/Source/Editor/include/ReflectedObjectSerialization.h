#pragma once

#include "ReflectedObjectSnapshot.h"

namespace Nyx::Editor
{
	ReflectedObjectSnapshot CaptureReflectedObject(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata);

	void RestoreReflectedObject(
		void* object,
		const ReflectedObjectSnapshot& snapshot);
}