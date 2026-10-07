# Startup intros drawn on the CPU (see SoftwareIntro.h): one renderer per effect.
#
# They touch every pixel of a DPI-scaled canvas sixty times a second, which an
# unoptimized build cannot sustain. This library is therefore compiled with
# optimizations in every configuration, Debug included. MSVC rejects /O2 together
# with /RTC1, so the runtime checks are removed for this directory only; the rest
# of the project keeps its usual Debug flags.
if(MSVC)
    foreach(flag /RTC1 /RTCsu /Od /Ob0)
        string(REPLACE "${flag}" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endforeach()
endif()

add_library(NyxSoftwareIntros STATIC
    SoftwareIntro.h
    StartupBannerRiftRenderer.cpp
    StartupBannerRiftRenderer.h
    StartupBannerAssembleRenderer.cpp
    StartupBannerAssembleRenderer.h
)

target_compile_features(NyxSoftwareIntros PUBLIC cxx_std_20)
target_include_directories(NyxSoftwareIntros PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(NyxSoftwareIntros PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(NyxSoftwareIntros PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

find_package(Threads REQUIRED)
target_link_libraries(NyxSoftwareIntros PRIVATE Threads::Threads)

set_property(TARGET NyxSoftwareIntros PROPERTY FOLDER "Startup")
