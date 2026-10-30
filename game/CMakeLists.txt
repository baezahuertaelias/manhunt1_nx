cmake_minimum_required(VERSION 3.20)

# Manhunt static recompilation. Two targets from the same lifted code:
#   host (default): Linux/POSIX bring-up build on xboxrecomp's runtime;
#   Switch (devkitA64 toolchain, NINTENDO_SWITCH): NRO on libnx.
# Built by scripts/game-build.sh. Inputs:
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
file(GLOB MANHUNT_GEN_SOURCES CONFIGURE_DEPENDS "${MANHUNT_GEN_DIR}/*.c")

# Native replacements shared by both targets (see each file's header).
set(MANHUNT_GAME_SOURCES
    src/recomp_manual.c
    src/xpp_input.c
    src/d3d_fence.c
    src/dsound_hle.c
)

if(NINTENDO_SWITCH)
    # Runtime subset: kernel + platform (POSIX layer on newlib/libnx) + the
    # pushbuffer executor. No SDL2/OpenGL/OpenSSL/APU: D3D is statically
    # linked in the title, input and audio are native at API level.
    set(XR ${XBOXRECOMP_DIR}/src)
    file(GLOB MANHUNT_RUNTIME_SOURCES
        ${XR}/kernel/*.c
        ${XR}/platform/win32_compat.c
        ${XR}/platform/host_time.c
        ${XR}/platform/recomp_exit_hook.c
    )
    add_executable(manhunt
        src/main_switch.c
        src/pad_switch.c
        src/pad_script.c
        src/horizon_compat.c
        src/horizon_alloc.c
        src/nv2a_deko3d.c
        ${MANHUNT_GAME_SOURCES}
        ${MANHUNT_RUNTIME_SOURCES}
        ${MANHUNT_GEN_SOURCES}
    )
    target_include_directories(manhunt PRIVATE
        src/horizon_include
        ${MANHUNT_GEN_DIR}
        ${XBOXRECOMP_DIR}/include
        ${XR} ${XR}/kernel ${XR}/platform ${XR}/nv2a ${XR}/apu ${XR}/d3d ${XR}/audio
        ${XR}/input ${XR}/usb ${XR}/video
    )
    target_compile_definitions(manhunt PRIVATE MANHUNT_HORIZON=1)
    target_compile_options(manhunt PRIVATE -g -fno-strict-aliasing -ffp-contract=off)
    target_link_libraries(manhunt PRIVATE deko3d)
    set_source_files_properties(${MANHUNT_RUNTIME_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2;-w")
    find_program(MANHUNT_UAM uam HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
    set(MANHUNT_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/nv2a_romfs")
    dkp_add_asset_target(manhunt_romfs "${MANHUNT_ROMFS}")
    set(MANHUNT_SHADERS)
    foreach(shader nv2a_attribute_vsh nv2a_diffuse_fsh nv2a_texture_fsh)
        if(shader MATCHES "_vsh$")
            set(stage vert)
        else()
            set(stage frag)
        endif()
        set(output "${MANHUNT_ROMFS}/shaders/${shader}.dksh")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MANHUNT_ROMFS}/shaders"
            COMMAND "${MANHUNT_UAM}" -s ${stage} -o "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.glsl"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.glsl"
            VERBATIM)
        list(APPEND MANHUNT_SHADERS "${output}")
    endforeach()
    add_custom_target(manhunt_shaders DEPENDS ${MANHUNT_SHADERS})
    add_dependencies(manhunt_romfs manhunt_shaders)
    set_property(TARGET manhunt_romfs APPEND PROPERTY DKP_ASSET_FILES ${MANHUNT_SHADERS})
    nx_generate_nacp(manhunt.nacp NAME "Manhunt (recomp)" AUTHOR "Manhunt Recompilation Team" VERSION "0.2.0")
    nx_create_nro(manhunt NACP manhunt.nacp ROMFS manhunt_romfs)
else()
    add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)
    add_executable(manhunt
        src/main.c
        src/pad_script.c
        ${MANHUNT_GAME_SOURCES}
        ${MANHUNT_GEN_SOURCES}
    )
    target_include_directories(manhunt PRIVATE
        ${MANHUNT_GEN_DIR}
        ${XBOXRECOMP_DIR}/include
    )
    target_link_libraries(manhunt PRIVATE xboxrecomp m)
    target_compile_options(manhunt PRIVATE -g -fno-strict-aliasing -ffp-contract=off)
    set_source_files_properties(src/main.c src/pad_script.c PROPERTIES COMPILE_OPTIONS "-O2;-Wall")
endif()

# Generated code: ~1.5M lines. -O1 keeps compile time and memory sane on the
# 4-core host; warnings are the lifter's (unused labels etc.), not ours.
set_source_files_properties(${MANHUNT_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;${MANHUNT_GEN_OPT}")
set_source_files_properties(${MANHUNT_GAME_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2;-Wall")
