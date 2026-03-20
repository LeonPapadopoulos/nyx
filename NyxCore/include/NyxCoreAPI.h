#pragma once

#if defined(NYXCORE_STATIC)
    #define NYXCORE_API
#elif defined(_WIN32)
    #if defined(NYXCORE_EXPORTS)
        #define NYXCORE_API __declspec(dllexport)
    #else
        #define NYXCORE_API __declspec(dllimport)
    #endif
#else
    #define NYXCORE_API
#endif