cmake_minimum_required(VERSION 3.20)

# Manhunt static recompilation, POSIX host build (milestone 2 bring-up).
# Built by scripts/game-build.sh inside manhunt-recompiler. Inputs:
#   XBOXRECOMP_DIR  patched copy of xboxrecomp (game/patches/xboxrecomp/*.patch)
#   MANHUNT_GEN_DIR lifted C from tools.recomp (game-derived: gitignored)
project(manhunt_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/xboxrecomp" CACHE PATH "patched xboxrecomp tree")
set(MANHUNT_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/gen" CACHE PATH "lifted C")
# -O0 makes every guest register access a memory access, so gdb probes read
# exact values (scripts/game-build.sh debug -> game/build/host-O0).
set(MANHUNT_GEN_OPT "-O1" CACHE STRING "optimisation level for generated code")

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB MANHUNT_GEN_SOURCES CONFIGURE_DEPENDS "${MANHUNT_GEN_DIR}/*.c")

add_executable(manhunt
    src/main.c
    src/recomp_manual.c
    src/xpp_input.c
    src/pad_script.c
    src/d3d_fence.c
    ${MANHUNT_GEN_SOURCES}
)
target_include_directories(manhunt PRIVATE
    ${MANHUNT_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)
target_link_libraries(manhunt PRIVATE xboxrecomp m)
target_compile_options(manhunt PRIVATE -g -fno-strict-aliasing -ffp-contract=off)

# Generated code: 1.36M lines. -O1 keeps compile time and memory sane on the
# 4-core host; warnings are the lifter's (unused labels etc.), not ours.
set_source_files_properties(${MANHUNT_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;${MANHUNT_GEN_OPT}")
set_source_files_properties(src/main.c src/recomp_manual.c src/xpp_input.c src/pad_script.c src/d3d_fence.c PROPERTIES COMPILE_OPTIONS "-O2;-Wall")
