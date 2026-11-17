cmake_minimum_required(VERSION 3.25)
project(nox_vita_viewer VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone target: deliberately excludes the desktop client, editors, HD assets,
# content packs, simulation and network dependencies. The original codecs stay shared.
set(NOX_SDL_SOURCE "" CACHE PATH "Optional SDL3 source tree; otherwise use installed SDL3")
if(NOX_SDL_SOURCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NOX_SDL_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 3.4 CONFIG REQUIRED)
endif()

set(CORE "${CMAKE_CURRENT_LIST_DIR}/../noxcore")
add_library(nox_vita_assets STATIC
    ${CORE}/src/crypto/blowfish.cpp
    ${CORE}/src/crypto/blowfish_tables.cpp
    ${CORE}/src/crypto/crc.cpp
    ${CORE}/src/crypto/noxcrypt.cpp
    ${CORE}/src/io/byte_reader.cpp
    ${CORE}/src/io/file.cpp
    ${CORE}/src/map/map_file.cpp
    ${CORE}/src/map/sections_geometry.cpp
    ${CORE}/src/map/sections_markers.cpp
    ${CORE}/src/map/objects.cpp
    ${CORE}/src/things/thing_db.cpp
    ${CORE}/src/things/object_class.cpp
    ${CORE}/src/things/spell_ids.cpp
    ${CORE}/src/video/video_bag.cpp
    ${CORE}/src/video/sprite.cpp
    ${CORE}/src/video/nxz.cpp
)
target_include_directories(nox_vita_assets PUBLIC "${CORE}/include")
target_compile_definitions(nox_vita_assets PRIVATE NOX_STOCK_ASSETS_ONLY)
target_compile_options(nox_vita_assets PRIVATE -ffp-contract=off -g1)
add_executable(nox_vita_viewer src/main.cpp src/viewer.cpp)
target_link_libraries(nox_vita_viewer PRIVATE nox_vita_assets SDL3::SDL3)
target_compile_options(nox_vita_viewer PRIVATE -Wall -Wextra -Wpedantic -g1)
if(VITA)
    target_compile_definitions(nox_vita_viewer PRIVATE NOX_VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    target_link_libraries(nox_vita_viewer PRIVATE pthread SceSysmem_stub)
    vita_create_self(eboot.bin nox_vita_viewer)
    vita_create_vpk(nox-vita-viewer.vpk NXVM00001 eboot.bin
        VERSION 01.01 NAME "Nox Map Viewer")
endif()
