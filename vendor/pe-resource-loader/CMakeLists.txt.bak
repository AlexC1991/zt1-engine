cmake_minimum_required(VERSION 3.10)

project(pe_resource_loader VERSION 1.0.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG=1")

set(SOURCES src/pe_resource_loader.c;src/tm_unicode.h)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/")
if(WIN32)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/")
else()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/")
endif()

option(BUILD_SHARED_LIBS "Build shared library." ON)
if(BUILD_SHARED_LIBS)
  add_library(pe_resource_loader_shared SHARED ${SOURCES})
  set_target_properties(pe_resource_loader_shared PROPERTIES OUTPUT_NAME pe_resource_loader)
  target_include_directories(pe_resource_loader_shared PRIVATE
    include/
  )
endif(BUILD_SHARED_LIBS)

option(BUILD_STATIC_LIBS "Build static library." ON)
if(BUILD_STATIC_LIBS)
  add_library(pe_resource_loader_static STATIC ${SOURCES})
  set_target_properties(pe_resource_loader_static PROPERTIES OUTPUT_NAME pe_resource_loader)
  target_include_directories(pe_resource_loader_static PRIVATE
    include/
  )
endif(BUILD_STATIC_LIBS)

option(PE_STRING_LOADER "If the pe_string_loader program should be build as well" ON)
if(PE_STRING_LOADER AND (BUILD_SHARED_LIBS OR BUILD_STATIC_LIBS))
  add_executable(pe_string_loader pe_string_loader/main.c)
  target_include_directories(pe_string_loader PRIVATE
    include/
  )
  if(BUILD_STATIC_LIBS)
    target_link_libraries(pe_string_loader pe_resource_loader_static)
  elseif(BUILD_SHARED_LIBS)
    target_link_libraries(pe_string_loader pe_resource_loader_shared)
  endif()
endif(PE_STRING_LOADER AND (BUILD_SHARED_LIBS OR BUILD_STATIC_LIBS))

option(PE_BITMAP_LOADER "If the pe_bitmap_loader program should be build as well" ON)
if(PE_BITMAP_LOADER AND (BUILD_SHARED_LIBS OR BUILD_STATIC_LIBS))
  add_executable(pe_bitmap_loader pe_bitmap_loader/main.c pe_bitmap_loader/stb_image_write.h)
  target_include_directories(pe_bitmap_loader PRIVATE
    include/
  )
  if(BUILD_STATIC_LIBS)
    target_link_libraries(pe_bitmap_loader pe_resource_loader_static)
  elseif(BUILD_SHARED_LIBS)
    target_link_libraries(pe_bitmap_loader pe_resource_loader_shared)
  endif()
endif(PE_BITMAP_LOADER AND (BUILD_SHARED_LIBS OR BUILD_STATIC_LIBS))

include(GNUInstallDirs)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/pe-resource-loader.pc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lib/pkgconfig/pe-resource-loader.pc" @ONLY)

if (NOT WIN32)
  if(BUILD_SHARED_LIBS)
    install(TARGETS pe_resource_loader_shared EXPORT pe-resource-loaderTargets DESTINATION lib)
    install(EXPORT pe-resource-loaderTargets FILE pe-resource-loaderTargets.cmake DESTINATION lib/cmake/pe-resource-loader)
  endif(BUILD_SHARED_LIBS)
  if(BUILD_STATIC_LIBS)
    install(TARGETS pe_resource_loader_static EXPORT pe-resource-loader-staticTargets DESTINATION lib)
    install(EXPORT pe-resource-loader-staticTargets FILE pe-resource-loader-staticTargets.cmake DESTINATION lib/cmake/pe-resource-loader)
  endif(BUILD_STATIC_LIBS)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/pe_resource_loader.h DESTINATION include/)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt DESTINATION share/licenses/pe_resource_loader/)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lib/pkgconfig/pe-resource-loader.pc DESTINATION lib/pkgconfig/)

  if(PE_STRING_LOADER)
    install(TARGETS pe_string_loader DESTINATION bin)
  endif(PE_STRING_LOADER)

  if(PE_BITMAP_LOADER)
    install(TARGETS pe_bitmap_loader DESTINATION bin)
  endif(PE_BITMAP_LOADER)
endif(NOT WIN32)