#pragma once
#include "NyxCoreAPI.h"

// Implementation based on 'Game Engine Architecture, 3rd Edition, 
// page 127 - 130, by Gregory Jason


// ------------------------------------------------------------
// Helper macros
// ------------------------------------------------------------
#define ASSERT_GLUE2(a, b) a##b
#define ASSERT_GLUE(a, b) ASSERT_GLUE2(a, b)

// ------------------------------------------------------------
// Debug break
// ------------------------------------------------------------
#if defined(_MSC_VER)
#define debugBreak() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define debugBreak() __builtin_trap()
#else
#include <signal.h>
#define debugBreak() raise(SIGTRAP)
#endif

// ------------------------------------------------------------
// Assertion reporting
// ------------------------------------------------------------
NYXCORE_API void reportAssertionFailure(const char* expr, const char* file, int line);

// ------------------------------------------------------------
// Runtime assert
// ------------------------------------------------------------

#ifdef ASSERTIONS_ENABLED
#define ASSERT(expr)                                                     \
        do                                                                   \
        {                                                                    \
            if (!(expr))                                                     \
            {                                                                \
                reportAssertionFailure(#expr, __FILE__, __LINE__);           \
                debugBreak();                                                \
            }                                                                \
        } while (0)
#else
#define ASSERT(expr) ((void)0)
#endif

// ------------------------------------------------------------
// Static assert
// ------------------------------------------------------------
#ifdef __cplusplus
    #if __cplusplus >= 201103L
        #define STATIC_ASSERT(expr)                                          \
                    static_assert((expr), "static assert failed: " #expr)
    #else
        template<bool> class TStaticAssert;
        template<> class TStaticAssert<true> {};

        #define STATIC_ASSERT(expr)                                          \
                    enum                                                             \
                    {                                                                \
                        ASSERT_GLUE(g_static_assert_, __LINE__) =                    \
                            sizeof(TStaticAssert< !!(expr) >)                        \
                    }
    #endif

#endif