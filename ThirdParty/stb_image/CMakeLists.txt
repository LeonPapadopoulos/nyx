set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/Dependencies/stb_image)

# stb_image is technically only a single header file, but we need at least 1 translation unit
# so we create a dummy one in the CMakeLists.txt file's location
add_library(stb_image STATIC
    stb_image.cpp
    ${STB_IMAGE_DIR}/stb_image.h
)

target_include_directories(stb_image
    PUBLIC
        ${STB_IMAGE_DIR}
)