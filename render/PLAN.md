# Render Library Implementation Plan

## engine/libs/render/ — Shared rendering infrastructure for CNC TD + RA

**Created:** 2026-03-29

---

## Directory Layout

```
engine/libs/render/
  CMakeLists.txt               Build: render_core + render_gl static libs
  PLAN.md                      This file
  AGENTS.md                    Agent guidelines
  stb_image.h                  stb_image v2.30 (upstream, TGA-only build)
  sprite_provider.h            ISpriteProvider interface + SpriteFrame struct
  legacy_sprite_provider.h/.cpp  SHP/MIX adapter (wraps engine/libs/graphics/)
  hd_sprite_provider.h/.cpp    MEG/TGA adapter (remastered HD assets)
  meg_reader.h/.cpp            MEG v2 archive parser
  meta_parser.h/.cpp           .META JSON crop/size parser
  texture_atlas.h/.cpp         Bin-packing atlas builder
  render_layer.h               Layer IDs and descriptors
  render_compositor.h/.cpp     Composites layers to final output
  gl/
    gl_renderer.h/.cpp         OpenGL ES 2.0 context + shader management
    gl_sprite_batch.h/.cpp     Batched quad renderer
    gl_house_color.h/.cpp      Green chroma key → house hue shift shader
```

## Design Principles

1. **No changes to modern.src/ until stable** — this library is opt-in, linked alongside existing code
2. **Shared between TD and RA** — game-specific differences handled by the provider, not the renderer
3. **Legacy path preserved** — LegacySpriteProvider wraps existing Build_Frame()/CC_Draw_Shape() pipeline
4. **GL is optional** — CPU compositing works without GL; GL backend compiled only when available
5. **Incremental adoption** — each module can be integrated independently

---

## Implementation Order

### Step 1: MEG Reader + META Parser (foundation, no engine changes) ✓

**Files:** `meg_reader.h/.cpp`, `meta_parser.h/.cpp`
**Status:** Implemented 2026-03-29

**Tasks:**
- [x] Port MEG v2 header/table parsing from `truth.src/.../Megafile.cs` to C++
- [x] Implement CRC-32 filename lookup (case-insensitive, binary search on sorted table)
- [x] Implement file read (buffer + alloc variants)
- [x] Implement META JSON parser (extract `size` and `crop` arrays)
- [ ] Write standalone test: open remastered MEG, list contents, read a TGA + META pair

**Implementation notes:**
- Standard CRC-32 (IEEE 802.3 polynomial 0xEDB88320) with uppercase filename convention
- MEG v2 header detection: magic 0xFFFFFFFF or 0x8FFFFFFF
- SubFileData: 20-byte packed struct (pack=2), file table sorted by CRC for binary search
- META parser is hand-rolled (no JSON library dependency); scans for `"size"` and `"crop"` arrays
- Uses Pimpl (MegReader::Impl) to keep FILE* and vectors out of the header

**Dependencies:** None
**Validates:** Can we access remastered HD assets at all?

---

### Step 2: ISpriteProvider + LegacySpriteProvider (abstraction layer) ✓

**Files:** `sprite_provider.h`, `legacy_sprite_provider.h/.cpp`
**Status:** Implemented 2026-03-29

**Tasks:**
- [x] Define SpriteFrame struct (pixels, dimensions, origin, format)
- [x] Implement LegacySpriteProvider wrapping Build_Frame() + Get_Build_Frame_Width/Height()
- [x] Handle BigShapeBuffer cache integration (return cached frame pointers)
- [ ] Test: call LegacySpriteProvider::Get_Frame() for a known SHP, verify pixel data matches direct Build_Frame() call

**Implementation notes:**
- Handles both SHP and KeyFrame formats (auto-detected via offset table heuristic, same as CC_Draw_Shape)
- KeyFrame path: Build_Frame → Get_Shape_Header_Data → returns BigShapeBuffer-backed pixels
- SHP path: Extract_Shape → LCW_Uncompress into reusable decompression buffer
- Compiled per engine target (not in render_core lib) since it depends on engine symbols via extern
- SpriteFrame is a view; KeyFrame pixels valid until BigShapeBuffer flush, SHP pixels valid until next Get_Frame call

**Dependencies:** Existing engine/libs/graphics/ (shp.cpp, compress.cpp)
**Validates:** Abstraction doesn't break legacy rendering

---

### Step 3: HDSpriteProvider (remastered asset loading) ✓

**Files:** `hd_sprite_provider.h/.cpp`, `stb_image.h`
**Status:** Implemented 2026-03-29

**Tasks:**
- [x] Integrate stb_image for TGA loading (add stb_image.h to engine/libs/render/)
- [x] Implement frame loading: MEG → TGA bytes → stb_image decode → RGBA pixels
- [x] Apply META crop offset to SpriteFrame origin_x/y and canvas dimensions
- [x] Implement XML tileset parser (map entity names → TGA filename patterns)
- [x] Cache loaded frames (avoid re-reading MEG per frame)
- [ ] Test: load HD frame for a known unit, verify dimensions and pixel data

**Implementation notes:**
- stb_image v2.30 (upstream, not SDL3-patched version which uses SDL types)
- Compiled with STBI_ONLY_TGA + STBI_NO_STDIO for minimal footprint
- Entity name lookup via FNV-1a hash; shape_id is (void*)(uintptr_t)hash
- Frame cache keyed on (name_hash << 16 | frame_num) in unordered_map
- XML tileset parser is minimal: scans for `<sequence>` tags, extracts name/filename/frames attrs
- TGA filename pattern: `{pattern}-{frame}.TGA`, META: `{pattern}-{frame}.META`
- Impl stored via reinterpret_cast of meg_ member (avoids modifying header)

**Dependencies:** Step 1 (meg_reader), Step 2 (ISpriteProvider interface)
**Validates:** HD assets load correctly with proper positioning

---

### Step 4: Texture Atlas Builder (GPU preparation) ✓

**Files:** `texture_atlas.h/.cpp`
**Status:** Implemented 2026-03-29

**Tasks:**
- [x] Implement bin-packing algorithm (shelf or maxrects, for power-of-2 pages)
- [x] Accept RGBA frames from either provider (legacy after palette conversion, or HD directly)
- [x] Track AtlasRegion per frame (atlas page ID, UV coordinates)
- [x] Finalize: produce atlas page pixel buffers ready for GPU upload
- [ ] Test: pack 100+ frames into atlas, verify no overlap, correct UV lookups

**Implementation notes:**
- Shelf-based packing: simple, fast, good enough for game sprite atlases
- Best-fit shelf selection (smallest shelf height that fits the frame)
- 1-pixel padding between frames to avoid bleeding at texture edges
- Default page size 2048×2048 (configurable via Init())
- AtlasRegion stores both pixel coords and normalized UV coords
- Pages auto-allocate; Finalize() locks the atlas (no more Add_Frame calls)
- Uses Pimpl with vector<AtlasPage*> and vector<FrameRecord>

**Dependencies:** Step 2/3 (sprite providers produce frames to pack)
**Validates:** Frames from both sources can be atlased together

---

### Step 5: Render Layers + Compositor (world/UI separation) ✓

**Files:** `render_layer.h`, `render_compositor.h/.cpp`
**Status:** Implemented 2026-03-29 (CPU mode; engine integration pending)

**Tasks:**
- [x] Define layer configuration (world terrain, objects, effects, air, shroud; UI sidebar, radar, power, tab)
- [x] Implement CPU compositor: allocate per-layer buffers, blit to output with offset
- [x] World layers: apply zoom factor during compositing (scale blit)
- [x] UI layers: fixed position, no zoom
- [x] Dirty tracking: only re-composite changed layers
- [ ] Integration point: replace GScreenClass::Render() chain with compositor calls

**Implementation notes:**
- 10 layers defined in RenderLayerID enum (5 world + 5 UI)
- Per-layer RGBA buffers allocated via Configure_Layer()
- World zoom: nearest-neighbor scaling with alpha blending during composite
- UI layers: direct blit with alpha blending, no scaling
- Set_World_Zoom() automatically marks all world layers dirty
- Composite() clears output and blits all layers bottom-to-top in enum order
- Output buffer accessible for SDL_UpdateTexture or GL upload

**Engine integration (deferred — requires modifying modern.src/):**
- `gscreen.cpp` Render() → calls compositor instead of monolithic Draw_It() chain
- `display.cpp` Draw_It() → renders to world layer buffer
- `sidebar.cpp`, `radar.cpp`, `power.cpp`, `tab.cpp` → render to UI layer buffer

**Dependencies:** None (can be built in parallel with Steps 1-4)
**Validates:** World and UI render independently; zoom works at compositing level

---

### Step 6: GL ES Renderer (GPU backend) ✓

**Files:** `gl/gl_renderer.h/.cpp`
**Status:** Implemented 2026-03-29 (standalone; platform integration pending)

**Tasks:**
- [x] Create OpenGL ES 2.0 context via SDL3 (SDL_GL_CreateContext)
- [x] Compile palette lookup shader (GL_LUMINANCE indexed + 256x1 palette → RGB)
- [x] Compile RGBA shader (direct textured quad with house color hue shift)
- [x] Implement texture upload/update (indexed, palette, RGBA)
- [x] Implement Draw_Palette_Quad() for legacy world buffer
- [x] Implement Draw_RGBA_Quad() for HD sprites and UI
- [ ] Replace sdl3_present.cpp palette LUT loop with GL palette shader
- [ ] Platform integration: `platform.sdl3/sdl3_gl_context.cpp` for SDL3+GL init

**Implementation notes:**
- Targets GL ES 2.0 (GLES2/gl2.h) — works on both Android and desktop Linux (Mesa)
- Palette shader: GL_LUMINANCE for indexed data (GL ES 2.0 lacks GL_R8), palette as 256×1 RGB
- RGBA shader: src_rect/dst_rect uniforms for atlas sub-region rendering
- RGBA shader includes inline RGB↔HSV conversion for house color hue shifting
- Unit quad VBO (0,0)→(1,1) shared across both shaders
- SDL_GL_CreateContext via g_window extern; Present() calls SDL_GL_SwapWindow
- Alpha blending enabled (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)

**Dependencies:** Step 5 (layers provide input buffers to GL)
**Validates:** Frame renders correctly via GL instead of SDL_UpdateTexture

---

### Step 7: GL Sprite Batch (GPU sprite rendering) ✓

**Files:** `gl/gl_sprite_batch.h/.cpp`
**Status:** Implemented 2026-03-29 (standalone; CC_Draw_Shape integration pending)

**Tasks:**
- [x] Implement sprite batch: collect quads per frame, sort by atlas page
- [x] Vertex format: position, UV, house_hue, fade, flags
- [x] Single draw call per atlas page (minimize state changes)
- [ ] Integration: CC_Draw_Shape() → query ISpriteProvider → add to batch → flush per layer
- [ ] Handle all 16 legacy rendering modes via shader uniforms (ghost, fade, predator, shadow)

**Implementation notes:**
- BatchVertex: pos(xy) + uv(xy) + house_hue + fade = 24 bytes per vertex
- Max 4096 quads per flush (pre-allocated VBO + static IBO with 0,1,2,2,1,3 pattern)
- Sorted by atlas_id before flushing; one glDrawElements per atlas page
- Fragment shader: alpha discard, house color hue shift (green chroma key), fade-to-black
- Flip H/V via UV swap (flags bits 0 and 1)
- Lazy GL initialization on first Flush() call
- Ghost/predator/shadow modes not yet in shader — marked for future integration

**Dependencies:** Step 4 (atlas), Step 6 (GL renderer)
**Validates:** Sprites render via GPU batching; draw call count is low

---

### Step 8: House Color Shader (HD color remapping) ✓

**Files:** `gl/gl_house_color.h/.cpp`
**Status:** Implemented 2026-03-29

**Tasks:**
- [x] Implement green chroma key detection in GLSL (green_ratio threshold)
- [x] Implement RGB→HSV→RGB hue shift in GLSL
- [x] Define HouseColorParams for all factions (TD: GDI/NOD, RA: Allies/Soviet, MP colors)
- [x] Integrate into sprite batch shader as optional uniform
- [x] Legacy path: palette remap tables still work for SHP sprites (no change needed)

**Implementation notes:**
- GLSL function `apply_house_color()` returned via GL_House_Color_Shader_Source()
- Green detection uses dominance ratio (G / avg(R,B)) against configurable threshold
- smoothstep blending at detection boundary preserves anti-aliased edges
- HouseColorParams defined as constexpr in header: GDI, NOD, Allies, Soviet, 4 MP colors
- House color already composed into gl_sprite_batch.cpp and gl_renderer.cpp RGBA shaders
- The standalone GLSL source exists for composability into custom shaders

**Dependencies:** Step 7 (sprite batch shader)
**Validates:** HD sprites show correct faction colors

---

## Integration with Existing Code

### CC_Draw_Shape() Migration Path

```
Current (modern.src/):
  CC_Draw_Shape(shapefile, frame, x, y, window, flags, fade, ghost)
    → Build_Frame() → Buffer_Frame_To_Page() → writes to HidPage

After (incremental swap):
  CC_Draw_Shape(shapefile, frame, x, y, window, flags, fade, ghost)
    → provider->Get_Frame(shapefile, frame, &sprite)
    → if (gpu_enabled)
        batch->Add(sprite, x, y, flags, fade)  // deferred GPU draw
      else
        Buffer_Frame_To_Page(...)               // existing CPU path
```

The existing CC_Draw_Shape() signature does not change. Internally it routes through the provider and renderer.

### GScreenClass::Render() Migration Path

```
Current:
  Render() → Draw_It() [full chain] → Blit_Display() → palette LUT → SDL present

After:
  Render()
    → compositor->Invalidate_Layer(WORLD_OBJECTS)
    → DisplayClass::Draw_It() targets compositor->Get_Layer_Buffer(WORLD_OBJECTS)
    → SidebarClass::Draw_It() targets compositor->Get_Layer_Buffer(UI_SIDEBAR)
    → ...
    → compositor->Composite()
    → if (gl_enabled)
        gl_renderer->Present()
      else
        Blit_Display()  // existing CPU path
```

---

## Effort Summary

| Step | Description | Status | Notes |
|------|-------------|--------|-------|
| 1 | MEG reader + META parser | ✓ Done | Standalone test pending |
| 2 | ISpriteProvider + Legacy adapter | ✓ Done | Integration test pending |
| 3 | HD sprite provider | ✓ Done | Asset validation pending |
| 4 | Texture atlas builder | ✓ Done | Visual packing test pending |
| 5 | Render layers + compositor | ✓ Done | Engine integration pending |
| 6 | GL ES renderer | ✓ Done | Platform integration pending |
| 7 | GL sprite batch | ✓ Done | CC_Draw_Shape integration pending |
| 8 | House color shader | ✓ Done | Complete |

**All 8 steps implemented.** Remaining work is integration and testing (see below).

## Build System

Two static libraries, added via `add_subdirectory(engine/libs/render)`:

```
render_core   — meg_reader, meta_parser, hd_sprite_provider, texture_atlas, render_compositor
                No engine deps. Linked into both cnc_td and cnc_ra.
                Includes stb_image.h (upstream v2.30, TGA-only build).

render_gl     — gl/gl_renderer, gl/gl_sprite_batch, gl/gl_house_color
                Links render_core + SDL3 + GLESv2.
                Targets GL ES 2.0 (GLES2/gl2.h).

legacy_sprite_provider.cpp — compiled per engine target (needs Build_Frame, BigShapeBuffer, etc.)
                             Path exported as RENDER_LEGACY_PROVIDER_SRC CMake variable.
```

---

## Testing Strategy

Each step has a standalone validation:
- **Steps 1-3:** Unit tests with known asset files (no rendering needed)
- **Step 4:** Visual test — dump atlas page to PNG, verify packing
- **Step 5:** Integration test — existing game renders correctly through compositor (CPU mode)
- **Steps 6-8:** Visual comparison — GL output matches CPU output pixel-for-pixel (excluding filtering)

**Current test status:** All implementations compile and link. No runtime tests written yet.
Standalone tests for Steps 1-4 are the next priority before engine integration.

## Remaining Integration Work

These items require modifying `modern.src/` (deferred per AGENTS.md rule 1):

1. **CC_Draw_Shape migration** — route through ISpriteProvider + batch instead of direct Build_Frame/Buffer_Frame_To_Page
2. **GScreenClass::Render migration** — replace monolithic Draw_It chain with compositor layer targeting
3. **sdl3_present.cpp replacement** — swap SDL_UpdateTexture palette LUT with GL palette shader
4. **GL sprite batch modes** — implement ghost/predator/shadow as shader uniforms (currently only fade + house color)
5. **SDL3 GL context init** — create `platform.sdl3/sdl3_gl_context.cpp` for GL window setup

---

## Files Not in This Library

These stay where they are:
- `engine/libs/graphics/shp.cpp` — SHP decompression (wrapped by LegacySpriteProvider)
- `engine/libs/graphics/compress.cpp` — LCW/XOR codecs (called by shp.cpp)
- `platform.sdl3/*/sdl3_present.cpp` — SDL presentation (replaced by GL path, kept as fallback)
- `engine/*/modern.src/display.cpp` — game-specific rendering logic (calls into this library)
