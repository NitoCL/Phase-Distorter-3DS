# Phase Distorter 3DS — Nintendo 3DS build (devkitPro / devkitARM + libctru).
#
#   cmake -S platform/3ds -B build-3ds \
#         -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-3ds -j
#
# Produces build-3ds/phase-distorter-3ds.3dsx for the Homebrew Launcher.
cmake_minimum_required(VERSION 3.20)
project(phase-distorter-3ds CXX C)

if(NOT NINTENDO_3DS)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# The core relies on exceptions and RTTI; some 3DS templates disable both.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexceptions> $<$<COMPILE_LANGUAGE:CXX>:-frtti>)

# Portable core only: no SDL, OpenGL, ImGui or desktop code.
set(PD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(EB_BUILD_FRONTEND OFF CACHE BOOL "" FORCE)
set(EB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EB_PREGENERATED_DIR "${PD_ROOT}/generated" CACHE PATH "" FORCE)
# One-byte phase markers in the core's hot paths for the on-device sampler.
add_compile_definitions(EB_PHASE_PROFILE=1)
add_subdirectory("${PD_ROOT}/cpp" core EXCLUDE_FROM_ALL)

add_executable(phase-distorter-3ds
    source/main.cpp
    source/bottom_ui.cpp
    source/audio_3ds.cpp)
target_include_directories(phase-distorter-3ds PRIVATE source)
target_link_libraries(phase-distorter-3ds PRIVATE
    eb_session eb_assets eb_native_asset_profiles ctru m)
target_compile_options(phase-distorter-3ds PRIVATE -Wall -Wextra)
# Shown on the bottom screen so a running build can be matched to a commit.
set(PD3DS_BUILD "local" CACHE STRING "Build identifier shown on screen")
target_compile_definitions(phase-distorter-3ds PRIVATE PD3DS_BUILD="${PD3DS_BUILD}")

ctr_generate_smdh(phase-distorter-3ds.smdh
    NAME "Phase Distorter 3DS"
    DESCRIPTION "EarthBound / Mother 2 nativo en 3DS"
    AUTHOR "Port 3DS de Phase Distorter"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
ctr_create_3dsx(phase-distorter-3ds SMDH phase-distorter-3ds.smdh)
