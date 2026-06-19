cmake_minimum_required(VERSION 3.10)
project(pacman256)

set(CMAKE_C_STANDARD 99)

if(EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s WASM=1 -s INITIAL_MEMORY=33554432 -s ALLOW_MEMORY_GROWTH=1")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

# Sokol headers configuration mapping include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sokol)

# Main executable asset target compilation structure
add_executable(pacman256 pacman256.c sokol/sokol.c)

if(EMSCRIPTEN)
    set_target_properties(pacman256 PROPERTIES SUFFIX ".html")
else()
    if(APPLE)
        # Force the Sokol backend implementation to compile as Objective-C 
        # so it can parse AppKit, Foundation, and Metal structures cleanly.
        set_source_files_properties(sokol/sokol.c PROPERTIES COMPILE_OPTIONS "-x;objective-c")
        
        find_library(COCOA_LIBRARY Cocoa)
        find_library(QUARTZ_CORE_LIBRARY QuartzCore)
        find_library(METAL_LIBRARY Metal)
        target_link_libraries(pacman256 ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${METAL_LIBRARY})
    elseif(LINUX)
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_link_libraries(pacman256 ${X11_LIBRARIES} ${OPENGL_LIBRARIES})
    elseif(WIN32)
        target_link_libraries(pacman256 kernel32 user32 gdi32)
    endif()
endif()