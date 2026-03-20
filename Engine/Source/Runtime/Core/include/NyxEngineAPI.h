#pragma once

#if defined(NYXENGINE_STATIC)
    #define NYXENGINE_API
#elif defined(_WIN32)
    #if defined(NYXENGINE_EXPORTS)
        #define NYXENGINE_API __declspec(dllexport)
    #else
        #define NYXENGINE_API __declspec(dllimport)
    #endif
#else
    #define NYXENGINE_API
#endif