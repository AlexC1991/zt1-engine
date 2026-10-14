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
add_subdirectory(vendor/pe-resource-loader)

# libzip. The copy under vendor/libzip is patched for MSVC -- lib/zip_fdopen.c
# tests HAVE_UNISTD_H and then includes the Windows <io.h>, which is backwards,
# and lib/zip_close.c calls _stricmp unconditionally -- so it does not build
# anywhere else. Off Windows, take the same library from the system, where it
# is already built for this platform. ours: Linux port.
if(WIN32)
    add_subdirectory(vendor/libzip)
    set(ZT1_LIBZIP_TARGET zip)
    set(ZT1_LIBZIP_INCLUDE_DIRS
        vendor/libzip/lib
        vendor/libzip/include
        ${CMAKE_CURRENT_BINARY_DIR}/vendor/libzip)
else()
    find_path(LIBZIP_INCLUDE_DIR zip.h)
    find_library(LIBZIP_LIBRARY NAMES zip)
    if(NOT LIBZIP_INCLUDE_DIR OR NOT LIBZIP_LIBRARY)
        message(FATAL_ERROR
            "libzip not found. Install it (Debian/Ubuntu: libzip-dev, Arch: libzip, Fedora: libzip-devel).")
    endif()
    message(STATUS "Using the system libzip: ${LIBZIP_LIBRARY}")
    set(ZT1_LIBZIP_TARGET ${LIBZIP_LIBRARY})
    set(ZT1_LIBZIP_INCLUDE_DIRS ${LIBZIP_INCLUDE_DIR})
endif()

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
    # (the SDL_* static libraries are named with a -static suffix in this build,
    # and reach the linker by file name through the directories just above)
    set(ZT1_SDL_IMAGE SDL2_image-static)
    set(ZT1_SDL_MIXER SDL2_mixer-static)
else()
    # (elsewhere the same libraries are ordinary CMake targets -- SDL_image and
    # SDL_mixer each define exactly one, without the -static suffix -- and a
    # target carries its own path, so there is nothing to look up)
    set(ZT1_SDL_IMAGE SDL2_image)
    set(ZT1_SDL_MIXER SDL2_mixer)
endif()

# 8. CREATE EXECUTABLE
add_executable(zt1-engine ${SOURCES}
        src/UserProfile.cpp
        src/UserProfile.hpp
        src/PngLoader.cpp
        src/PngLoader.hpp)

# 8b. THE FILES THE ENGINE WANTS BESIDE IT
# The engine reads its own files from the folder it is running in
# (Paths::appDir()): the only one is fonts/. Without them the game starts,
# loads everything, and draws no text anywhere -- buttons, panels, money, all
# of it blank -- with nothing in the log to say why. ours: Linux port.
add_custom_command(TARGET zt1-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:zt1-engine>/fonts"
    COMMENT "Copying the engine's fonts beside the executable")

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
    ${ZT1_LIBZIP_INCLUDE_DIRS}
    vendor/pe-resource-loader
    vendor/pe-resource-loader/src
    vendor/pe-resource-loader/include
    vendor/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib
)

# 10. LINK EVERYTHING TOGETHER (Single unified call with PRIVATE keyword)
target_link_libraries(zt1-engine PRIVATE
    # SDL Libraries
    SDL2-static
    ${ZT1_SDL_IMAGE}
    ${ZT1_SDL_MIXER}
    SDL2_ttf
    # Archive/Compression
    ${ZT1_LIBZIP_TARGET}
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
    ZT1_OFFICIAL_BUILD=$<BOOL:${ZT1_OFFICIAL_BUILD}>)

# 12. THE ORIGINAL GAME IN THE LAB (src/original/): zoo.exe is 32-bit, so
# these are built only in a 32-bit tree (build/): zt1-original.exe starts
# zoo.exe with zt1-hook.dll loaded into it (windowed, in the Lab's Play view)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(zt1-hook SHARED src/original/Hook.cpp)
    target_link_libraries(zt1-hook PRIVATE user32 gdi32 ole32 advapi32 dxguid shlwapi)
    add_executable(zt1-original src/original/Launch.cpp)
    add_dependencies(zt1-original zt1-hook)
endif()
