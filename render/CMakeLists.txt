# engine/libs/render — Shared rendering infrastructure for CNC TD + RA
#
# Two libraries:
#   render_core  — standalone (no engine deps, no GL): MEG reader, META parser,
#                  sprite provider interfaces, texture atlas, compositor
#   render_gl    — GL ES backend (requires SDL3 + OpenGL ES 2.0)
#
# legacy_sprite_provider.cpp is compiled separately per engine target
# because it depends on Build_Frame() and other engine symbols.

set(RENDER_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# --- render_core: standalone, no engine dependencies ---
add_library(render_core STATIC
    ${RENDER_DIR}/meg_reader.cpp
    ${RENDER_DIR}/meta_parser.cpp
    ${RENDER_DIR}/hd_sprite_provider.cpp
    ${RENDER_DIR}/dds_reader.cpp
    ${RENDER_DIR}/texture_atlas.cpp
    ${RENDER_DIR}/render_compositor.cpp
)

target_include_directories(render_core
    PUBLIC  ${RENDER_DIR}
)

target_compile_features(render_core PUBLIC cxx_std_17)

target_compile_options(render_core PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
)

# --- render_gl: OpenGL ES backend (requires SDL3) ---
add_library(render_gl STATIC
    ${RENDER_DIR}/gl/gl_renderer.cpp
    ${RENDER_DIR}/gl/gl_sprite_batch.cpp
    ${RENDER_DIR}/gl/gl_house_color.cpp
)

target_include_directories(render_gl
    PUBLIC  ${RENDER_DIR}
)

target_link_libraries(render_gl PUBLIC render_core)
target_compile_features(render_gl PUBLIC cxx_std_17)

target_compile_options(render_gl PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
)

# GL sources need SDL3 (for SDL_GL_*) and GLES2
if(TARGET ${SDL3_TARGET})
    target_link_libraries(render_gl PUBLIC ${SDL3_TARGET})
elseif(TARGET SDL3::SDL3)
    target_link_libraries(render_gl PUBLIC SDL3::SDL3)
endif()

# Link OpenGL ES 2.0
if(ANDROID)
    target_link_libraries(render_gl PUBLIC GLESv2)
else()
    find_library(GLESV2_LIB GLESv2)
    if(GLESV2_LIB)
        target_link_libraries(render_gl PUBLIC ${GLESV2_LIB})
    endif()
endif()

# --- legacy_sprite_provider source (compiled per-target, not in library) ---
# Expose the source path so game targets can add it to their own source lists
set(RENDER_LEGACY_PROVIDER_SRC ${RENDER_DIR}/legacy_sprite_provider.cpp
    CACHE INTERNAL "LegacySpriteProvider source — add to engine targets")
