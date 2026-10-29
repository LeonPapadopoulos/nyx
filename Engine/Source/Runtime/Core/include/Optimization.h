#pragma once

// Turns the compiler's optimisation off for the functions of a file from here on, and back on to
// the file's own optimisation: to step through one file of an optimised build (Development, or the
// engine in DebugGame).
//
//	NYX_DISABLE_OPTIMIZATION
//	... the functions to step through ...
//	NYX_ENABLE_OPTIMIZATION
//
// NYX_ENABLE_OPTIMIZATION only goes back to what the file is compiled with: it can't optimise a
// file compiled unoptimised, such as a game's in DebugGame. There, MSVC's #pragma optimize leaves
// inlining off (/Ob0), and the inline functions of the headers above it, such as glm's, are
// compiled unoptimised already. nyx_optimize_game_sources() (the top CMakeLists.txt) does that.
// MSVC only: with other compilers both are nothing.

#if defined(_MSC_VER)
	#define NYX_DISABLE_OPTIMIZATION __pragma(optimize("", off))
	#define NYX_ENABLE_OPTIMIZATION	 __pragma(optimize("", on))
#else
	#define NYX_DISABLE_OPTIMIZATION
	#define NYX_ENABLE_OPTIMIZATION
#endif
