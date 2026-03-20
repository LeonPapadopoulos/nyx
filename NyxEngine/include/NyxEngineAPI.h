#pragma once

// Defined CMake File
#ifdef NYXENGINE_EXPORTS
#define NYXENGINE_API __declspec(dllexport)
#else
#define NYXENGINE_API __declspec(dllimport)
#endif