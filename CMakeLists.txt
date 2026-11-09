cmake_minimum_required(VERSION 3.12)
# (the C++ runtime chosen by CMAKE_MSVC_RUNTIME_LIBRARY, below)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(zt1-engine)
# ZT1 Engine Remake - CMake Configuration

# 1. UPGRADE TO C++20 (Fixes 'contains' and 'designated initializer' errors)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. THE C++ RUNTIME: as a DLL (/MD, the default - fixes linker/MSVCRT
# errors), or built in (/MT, ZT1_STATIC_RUNTIME: releases, so players need
# no Visual C++ Redistributable)
option(ZT1_STATIC_RUNTIME "Build the C++ runtime into the exe (/MT)" OFF)
if(MSVC)
    if(ZT1_STATIC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        add_compile_options(/MD)
    endif()
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# The release's version and whether it's official (src/Updater.hpp): set by
# the release workflow. A build of one's own is "dev", and never updates
set(ZT1_VERSION "dev" CACHE STRING "The release's version, e.g. 2026.10.19")
option(ZT1_OFFICIAL_BUILD "A release made by the release workflow: it may update itself" OFF)
# A test release (make_release.py --test): the official release in every way
# but that it never looks for updates, and keeps its settings, saves and mods
# apart from the published copy's - so a release can be tried, installed,
# before it's put up
option(ZT1_TEST_BUILD "A release made to be tried before it's published: it never updates" OFF)

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
# ours: the port to SDL3 (its GPU API: one modern graphics path for new
# effects - Direct3D 12, Vulkan, Metal - in place of the Direct3D 11 and
# OpenGL twins), built in its own folder while it's made (build-sdl3:
# -DZT1_SDL3=ON); off, SDL2 as always
option(ZT1_SDL3 "Build on SDL3 (the port in progress); off, SDL2" OFF)
if(ZT1_SDL3)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/SDL3)
    # (images: the built-in decoders only, as the SDL2 build)
    foreach(off SDLIMAGE_VENDORED SDLIMAGE_AVIF SDLIMAGE_JXL SDLIMAGE_TIF SDLIMAGE_WEBP SDLIMAGE_BACKEND_WIC
                SDLIMAGE_PNG_LIBPNG SDLIMAGE_SAMPLES SDLIMAGE_TESTS SDLIMAGE_INSTALL SDLIMAGE_DEPS_SHARED)
        set(${off} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    add_subdirectory(vendor/SDL3_image)
    # (sound: the decoders built into SDL3_mixer - WAVE, dr_mp3, stb_vorbis,
    # dr_flac, timidity - none downloaded)
    foreach(off SDLMIXER_VENDORED SDLMIXER_FLAC_LIBFLAC SDLMIXER_GME SDLMIXER_MOD SDLMIXER_MP3_MPG123
                SDLMIXER_MIDI_FLUIDSYNTH SDLMIXER_OPUS SDLMIXER_VORBIS_VORBISFILE SDLMIXER_WAVPACK
                SDLMIXER_TESTS SDLMIXER_EXAMPLES SDLMIXER_INSTALL SDLMIXER_DEPS_SHARED)
        set(${off} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory(vendor/SDL3_mixer)
    # (text: FreeType from the copy SDL2_ttf brought, in SDL3_ttf/external;
    # no HarfBuzz or colour emoji, as the SDL2 build)
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    foreach(off SDLTTF_HARFBUZZ SDLTTF_PLUTOSVG SDLTTF_SAMPLES SDLTTF_INSTALL)
        set(${off} OFF CACHE BOOL "" FORCE)
    endforeach()
    # (FreeType's own: no compressed or colour fonts - as SDL2_ttf set them)
    foreach(on FT_DISABLE_ZLIB FT_DISABLE_BZIP2 FT_DISABLE_PNG FT_DISABLE_BROTLI FT_DISABLE_HARFBUZZ)
        set(${on} ON CACHE BOOL "" FORCE)
    endforeach()
    # (zlib is found for libzip, below; FreeType goes by ZLIB_FOUND, so it's
    # hidden from SDL3_ttf, then shown again)
    set(ZLIB_FOUND FALSE)
    add_subdirectory(vendor/SDL3_ttf)
    unset(ZLIB_FOUND)
else()
add_subdirectory(vendor/SDL)
add_subdirectory(vendor/SDL_image)
add_subdirectory(vendor/SDL_mixer)
add_subdirectory(vendor/SDL_ttf)
endif()
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
    vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp)
if(ZT1_SDL3)
    list(APPEND SOURCES vendor/imgui/backends/imgui_impl_sdl3.cpp vendor/imgui/backends/imgui_impl_sdlrenderer3.cpp)
else()
    list(APPEND SOURCES vendor/imgui/backends/imgui_impl_sdl2.cpp vendor/imgui/backends/imgui_impl_sdlrenderer2.cpp)
endif()

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
if(ZT1_SDL3)
    # (the targets bring their own include folders: <SDL3/...>, <SDL3_image/...>)
    target_link_libraries(zt1-engine PRIVATE SDL3::SDL3-static SDL3_image::SDL3_image-static
        SDL3_mixer::SDL3_mixer-static SDL3_ttf::SDL3_ttf-static)
    target_compile_definitions(zt1-engine PRIVATE ZT1_SDL3=1)
    # (the SDL2 calls whose meaning changed, kept as the engine used them:
    # before every source file)
    if(MSVC)
        target_compile_options(zt1-engine PRIVATE "/FI${CMAKE_SOURCE_DIR}/src/Sdl3Compat.hpp")
    else()
        target_compile_options(zt1-engine PRIVATE -include "${CMAKE_SOURCE_DIR}/src/Sdl3Compat.hpp")
    endif()
else()
    target_link_libraries(zt1-engine PRIVATE SDL2-static SDL2_image-static SDL2_mixer-static SDL2_ttf)
endif()
target_link_libraries(zt1-engine PRIVATE
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
        winhttp
    )
endif()
target_compile_definitions(zt1-engine PRIVATE ZT1_VERSION="${ZT1_VERSION}"
    ZT1_OFFICIAL_BUILD=$<BOOL:${ZT1_OFFICIAL_BUILD}> ZT1_TEST_BUILD=$<BOOL:${ZT1_TEST_BUILD}>)

# addition: a small independent updater window can remain open while the
# application files are replaced. It needs no game art or installed DLLs.
if(WIN32)
    add_executable(zt1-update-helper WIN32 tools/update-helper.cpp)
    target_compile_definitions(zt1-update-helper PRIVATE UNICODE _UNICODE)
    target_link_libraries(zt1-update-helper PRIVATE user32 gdi32 shell32 bcrypt crypt32)
    if(MSVC)
        set_property(TARGET zt1-update-helper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(zt1-update-helper PRIVATE /MT)
    endif()
    add_dependencies(zt1-engine zt1-update-helper)
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
