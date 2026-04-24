cmake_minimum_required(VERSION 3.16)
project(render_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Parent render library source
set(RENDER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RENDER_GL_SRC ${RENDER_SRC}/gl)

# ─── Step 1: MEG reader + META parser (standalone, no engine deps) ───

add_executable(test_meg_reader
    test_meg_reader.cpp
    ${RENDER_SRC}/meg_reader.cpp
)
target_include_directories(test_meg_reader PRIVATE ${RENDER_SRC})

add_executable(test_meta_parser
    test_meta_parser.cpp
    ${RENDER_SRC}/meta_parser.cpp
)
target_include_directories(test_meta_parser PRIVATE ${RENDER_SRC})

add_executable(test_zip_reader
    test_zip_reader.cpp
    ${RENDER_SRC}/dds_reader.cpp
    ${RENDER_SRC}/zip_reader.cpp
    ${RENDER_SRC}/dds_reader.cpp
)
target_include_directories(test_zip_reader PRIVATE ${RENDER_SRC})

add_executable(test_bui_reader
    test_bui_reader.cpp
    ${RENDER_SRC}/bui_reader.cpp
    ${RENDER_SRC}/meg_reader.cpp
    ${RENDER_SRC}/zip_reader.cpp
)
target_include_directories(test_bui_reader PRIVATE ${RENDER_SRC})

# ─── Step 2: ISpriteProvider + Legacy adapter ────────────────────────

add_executable(test_sprite_provider
    test_sprite_provider.cpp
    ${RENDER_SRC}/legacy_sprite_provider.cpp
)
target_include_directories(test_sprite_provider PRIVATE ${RENDER_SRC})

# ─── Step 3: HD sprite provider ─────────────────────────────────────

add_executable(test_hd_sprite_provider
    test_hd_sprite_provider.cpp
    ${RENDER_SRC}/hd_sprite_provider.cpp
    ${RENDER_SRC}/meg_reader.cpp
    ${RENDER_SRC}/meta_parser.cpp
    ${RENDER_SRC}/zip_reader.cpp
    ${RENDER_SRC}/dds_reader.cpp
)
target_include_directories(test_hd_sprite_provider PRIVATE ${RENDER_SRC})

# ─── Step 4: Texture atlas ──────────────────────────────────────────

add_executable(test_texture_atlas
    test_texture_atlas.cpp
    ${RENDER_SRC}/texture_atlas.cpp
)
target_include_directories(test_texture_atlas PRIVATE ${RENDER_SRC})

# ─── Step 5: Render layers + compositor ──────────────────────────────

add_executable(test_render_layer
    test_render_layer.cpp
    ${RENDER_SRC}/render_compositor.cpp
)
target_include_directories(test_render_layer PRIVATE ${RENDER_SRC})

# ─── Step 8: House color definitions ─────────────────────────────────

add_executable(test_gl_house_color
    test_gl_house_color.cpp
    ${RENDER_GL_SRC}/gl_house_color.cpp
)
target_include_directories(test_gl_house_color PRIVATE ${RENDER_SRC})

# ─── Steps 6-7: GL tests (need SDL3 + GL) ───────────────────────────
# These are optional — only built if SDL3 is found.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 QUIET sdl3)
endif()

if(SDL3_FOUND)
    add_executable(test_gl_renderer
        test_gl_renderer.cpp
        ${RENDER_GL_SRC}/gl_renderer.cpp
    )
    target_include_directories(test_gl_renderer PRIVATE ${RENDER_SRC})
    target_link_libraries(test_gl_renderer ${SDL3_LIBRARIES} GLESv2)
    target_include_directories(test_gl_renderer PRIVATE ${SDL3_INCLUDE_DIRS})

    add_executable(test_gl_sprite_batch
        test_gl_sprite_batch.cpp
        ${RENDER_GL_SRC}/gl_sprite_batch.cpp
        ${RENDER_GL_SRC}/gl_renderer.cpp
    )
    target_include_directories(test_gl_sprite_batch PRIVATE ${RENDER_SRC})
    target_link_libraries(test_gl_sprite_batch ${SDL3_LIBRARIES} GLESv2)
    target_include_directories(test_gl_sprite_batch PRIVATE ${SDL3_INCLUDE_DIRS})

    message(STATUS "GL tests enabled (SDL3 found)")
else()
    message(STATUS "GL tests skipped (SDL3 not found)")
endif()

# ─── CTest registration ─────────────────────────────────────────────

enable_testing()
add_test(NAME meg_reader       COMMAND test_meg_reader)
add_test(NAME meta_parser      COMMAND test_meta_parser)
add_test(NAME zip_reader       COMMAND test_zip_reader)
add_test(NAME bui_reader       COMMAND test_bui_reader)
add_test(NAME sprite_provider  COMMAND test_sprite_provider)
add_test(NAME hd_sprite_provider COMMAND test_hd_sprite_provider)
add_test(NAME texture_atlas    COMMAND test_texture_atlas)
add_test(NAME render_layer     COMMAND test_render_layer)
add_test(NAME gl_house_color   COMMAND test_gl_house_color)

if(SDL3_FOUND)
    add_test(NAME gl_renderer     COMMAND test_gl_renderer)
    add_test(NAME gl_sprite_batch COMMAND test_gl_sprite_batch)
endif()

add_executable(test_hd_terrain_provider test_hd_terrain_provider.cpp
    ${RENDER_SRC}/hd_terrain_provider.cpp
    ${RENDER_SRC}/meg_reader.cpp
    ${RENDER_SRC}/meta_parser.cpp
    ${RENDER_SRC}/dds_reader.cpp
)
target_include_directories(test_hd_terrain_provider PRIVATE ${RENDER_SRC})
