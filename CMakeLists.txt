cmake_minimum_required(VERSION 3.12)
project(zt1-engine)

# 1. UPGRADE TO C++20 (Fixes 'contains' and 'designated initializer' errors)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. FORCE DYNAMIC RUNTIME /MD (Fixes Linker/MSVCRT errors)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options(/MD)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# 3. DISABLE LIBRARY BLOAT & INSTALLS (Fixes NASM and Export errors)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "No install" FORCE)
set(SDL2IMAGE_AVIF OFF CACHE BOOL "Disable AVIF" FORCE)
set(SDL2IMAGE_WEBP OFF CACHE BOOL "Disable WEBP" FORCE)
set(SDL2IMAGE_JXL OFF CACHE BOOL "Disable JXL" FORCE)
set(SDL2IMAGE_TIF OFF CACHE BOOL "Disable TIF" FORCE)
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "Disable downloading extra stuff" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "Use lightweight STB decoder" FORCE)

# 4. ZLIB SETUP
set(ZLIB_FOUND TRUE CACHE BOOL "ZLIB Found" FORCE)
set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/zlib" "${CMAKE_BINARY_DIR}/vendor/zlib" CACHE PATH "ZLIB Include" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE FILEPATH "ZLIB Library" FORCE)

# 5. ADD LIBRARIES
add_subdirectory(vendor/SDL)
add_subdirectory(vendor/SDL_image)
add_subdirectory(vendor/SDL_mixer)
add_subdirectory(vendor/SDL_ttf)
add_subdirectory(vendor/zlib)
add_subdirectory(vendor/libzip)
add_subdirectory(vendor/pe-resource-loader)

# 6. GATHER SOURCE CODE
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

# 7. FIX LINKER PATHS FOR MSVC
if(MSVC)
    link_directories(
        "${CMAKE_BINARY_DIR}/vendor/SDL/Release"
        "${CMAKE_BINARY_DIR}/vendor/SDL_image/Release"
        "${CMAKE_BINARY_DIR}/vendor/SDL_mixer/Release"
        "${CMAKE_BINARY_DIR}/vendor/SDL_ttf/Release"
        "${CMAKE_BINARY_DIR}/vendor/zlib/Release"
        "${CMAKE_BINARY_DIR}/vendor/libzip/lib/Release"
        "${CMAKE_BINARY_DIR}/vendor/pe-resource-loader/lib/Release"
    )
endif()

# 8. CREATE EXECUTABLE
add_executable(zt1-engine ${SOURCES}
        src/UserProfile.cpp
        src/UserProfile.hpp)

# 9. FIX INCLUDE PATHS
target_include_directories(zt1-engine PRIVATE
    src
    vendor/SDL/include
    vendor/SDL_image
    vendor/SDL_image/include
    vendor/SDL_mixer
    vendor/SDL_mixer/include
    vendor/SDL_ttf
    vendor/SDL_ttf/include
    vendor/libzip/lib
    vendor/libzip/include
    vendor/pe-resource-loader
    vendor/pe-resource-loader/src
    vendor/pe-resource-loader/include
    vendor/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/vendor/libzip
)

# 10. LINK EVERYTHING TOGETHER (Single unified call with PRIVATE keyword)
target_link_libraries(zt1-engine PRIVATE
    # SDL Libraries
    SDL2-static
    SDL2_image-static
    SDL2_mixer-static
    SDL2_ttf
    # Archive/Compression
    zip
    zlibstatic
    # PE Resource Loader
    pe_resource_loader_static
)

# 11. WINDOWS SYSTEM LIBRARIES
if(WIN32)
    target_link_libraries(zt1-engine PRIVATE
        user32
        gdi32
        winmm
        imm32
        ole32
        oleaut32
        version
        uuid
        advapi32
        setupapi
        shell32
        dinput8
        shlwapi
        bcrypt
        crypt32
    )
endif()