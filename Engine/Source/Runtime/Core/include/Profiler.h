#pragma once

// Nyx's profiler macros. They wrap Tracy (Dependencies/tracy), and this is the only file that
// includes it, so the engine can change profilers here alone. With the CMake option NYX_PROFILER
// off, every macro is nothing and Tracy is neither compiled nor linked. With it on, Tracy collects
// nothing until a viewer connects (TRACY_ON_DEMAND).
//
// The names of zones and plots are string literals: the profiler keeps the pointer, not the text.

#if NYX_PROFILER

	#include <tracy/Tracy.hpp>

	#include <string_view>

	// A zone from here to the end of the scope
	#define NYX_PROFILE_SCOPE(name) ZoneScopedN(name)

	// A zone named after the function it is in, to the end of the scope
	#define NYX_PROFILE_FUNCTION()  ZoneScoped

	// Renames the zone of this scope with text made at run time, such as a layer's type. Needs a zone
	// from NYX_PROFILE_SCOPE() or NYX_PROFILE_FUNCTION() in the same scope.
	#define NYX_PROFILE_ZONE_NAME(text)                                     \
		do                                                                  \
		{                                                                   \
			const std::string_view nyxProfileZoneName(text);                \
			ZoneName(nyxProfileZoneName.data(), nyxProfileZoneName.size()); \
		}                                                                   \
		while (0)

	// The end of a frame; once per frame, in Application::Run()
	#define NYX_PROFILE_FRAME()           FrameMark

	// Names the calling thread in the viewer
	#define NYX_PROFILE_THREAD(name)      tracy::SetThreadName(name)

	// A value over time, as a graph in the viewer: an int64_t, a float or a double
	#define NYX_PROFILE_PLOT(name, value) TracyPlot(name, value)

	// A line of text at this moment on the viewer's timeline
	#define NYX_PROFILE_MESSAGE(text)                                         \
		do                                                                    \
		{                                                                     \
			const std::string_view nyxProfileMessage(text);                   \
			TracyMessage(nyxProfileMessage.data(), nyxProfileMessage.size()); \
		}                                                                     \
		while (0)

#else

	#define NYX_PROFILE_SCOPE(name)
	#define NYX_PROFILE_FUNCTION()
	#define NYX_PROFILE_ZONE_NAME(text)   ((void)0)
	#define NYX_PROFILE_FRAME()           ((void)0)
	#define NYX_PROFILE_THREAD(name)      ((void)0)
	#define NYX_PROFILE_PLOT(name, value) ((void)0)
	#define NYX_PROFILE_MESSAGE(text)     ((void)0)

#endif
