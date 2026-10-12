cmake_minimum_required(VERSION 3.12)
project(zt1-engine)
# ZT1 Engine Remake - CMake Configuration

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
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.c")
list(FILTER SOURCES EXCLUDE REGEX "test_zoo_reader.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "Entity.cpp$")
# (src/original/: the original game's hook, built on its own below)
list(FILTER SOURCES EXCLUDE REGEX "src/original/")
# Dear ImGui (vendor/imgui): the Lab's panels (--lab)
list(APPEND SOURCES
    vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_sdl2.cpp vendor/imgui/backends/imgui_impl_sdlrenderer2.cpp)

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
        src/UserProfile.hpp
        src/PngLoader.cpp
        src/PngLoader.hpp)

# Test executable disabled - test_zoo_reader.cpp removed in architecture rebuild
# add_executable(test-zoo src/test_zoo_reader.cpp src/ZooReader.cpp src/MemoryManager.cpp)
# target_link_libraries(test-zoo PRIVATE SDL2-static zlibstatic)
# target_include_directories(test-zoo PRIVATE src vendor/SDL/include vendor/zlib ${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib)

# 9. FIX INCLUDE PATHS
target_include_directories(zt1-engine PRIVATE
    src
    vendor/imgui
    re/lab
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
        ws2_32
    )
endif()

# 12. THE ORIGINAL GAME IN THE LAB (src/original/): zoo.exe is 32-bit, so
# these are built only in a 32-bit tree (build/): zt1-original.exe starts
# zoo.exe with zt1-hook.dll loaded into it (windowed, in the Lab's Play view)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(zt1-hook SHARED src/original/Hook.cpp)
    target_link_libraries(zt1-hook PRIVATE user32 gdi32 ole32 advapi32 dxguid shlwapi)
    add_executable(zt1-original src/original/Launch.cpp)
    add_dependencies(zt1-original zt1-hook)
endif()
