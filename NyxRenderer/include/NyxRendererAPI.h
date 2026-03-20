#pragma once

#if defined(NYXRENDERER_STATIC)
    #define NYXRENDERER_API
#elif defined(_WIN32)
    #if defined(NYXRENDERER_EXPORTS)
        #define NYXRENDERER_API __declspec(dllexport)
    #else
        #define NYXRENDERER_API __declspec(dllimport)
    #endif
#else
    #define NYXRENDERER_API
#endif