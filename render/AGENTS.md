# Render Library — Agent Guidelines

## Purpose

Shared rendering infrastructure for CNC TD and RA engines.
Provides ISpriteProvider abstraction for legacy SHP and remastered MEG/TGA assets,
layered compositing with zoom, and an OpenGL ES GPU backend.

## Architecture Context

```
engine/libs/render/          ← this library (shared, game-agnostic)
engine/libs/graphics/        ← legacy SHP/LCW/WSA codecs (do not modify)
engine/cnc/modern.src/       ← TD game code (stabilizing 64-bit, do not modify unless asked)
engine/ra/modern.src/        ← RA game code (stabilizing 64-bit, do not modify unless asked)
platform.sdl3/               ← SDL3 platform layer
include/cnc/ include/ra/     ← platform headers (GraphicViewPortClass etc.)
truth.src/                   ← EA original source (read-only reference)
```

## Principles

### KISS
- Solve the problem in front of you. Do not solve future problems.
- If a function can be a free function, do not make it a method.
- If a struct with public members is enough, do not make it a class.
- Prefer flat code over nested abstractions. One level of indirection is a cost; justify it.
- No "manager" classes that just forward calls. If it only delegates, inline the logic.
- Avoid templates unless they eliminate real duplication across types actually used today.

### SOLID (applied pragmatically)
- **SRP:** Each file does one thing. meg_reader reads MEG archives. meta_parser parses META JSON. Do not combine them.
- **OCP:** ISpriteProvider exists so new asset formats can be added without modifying existing providers. That is the only extension point. Do not add others speculatively.
- **LSP:** Both LegacySpriteProvider and HDSpriteProvider return a SpriteFrame. Callers must not need to know which provider they are talking to.
- **ISP:** ISpriteProvider has three methods. Keep it that way. Do not add convenience methods to the interface.
- **DIP:** Game code depends on ISpriteProvider, not on MegReader or Build_Frame() directly. But inside a provider implementation, call concrete types directly — no internal DI.

### Performance
- This is a game renderer. Every frame matters. Measure before and after.
- Prefer contiguous memory (arrays, vectors) over pointer-chasing (linked lists, maps of maps).
- Cache aggressively: decompress once, store result, return pointer. Never decompress the same frame twice in a frame.
- Minimize allocations in the render loop. Pre-allocate buffers at init. Zero per-frame heap allocations is the target.
- Atlas packing happens at load time, not at runtime. Runtime is just UV lookup + batch add.
- Sprite batch: pack quads into a vertex buffer, flush once per atlas page per layer. Minimize draw calls.
- Use `const` and pass by reference/pointer. Never copy pixel buffers.
- Profile with real game data before optimizing. Do not guess at bottlenecks.

### C++ Best Practice
- C++17. Use standard library where it helps (std::vector, std::array, std::string_view). Avoid where it costs (std::map in hot paths — use sorted arrays or flat_map).
- RAII for resources (GL textures, file handles, allocated buffers). No manual cleanup paths.
- No exceptions in render code. Return bool or error codes. Check them.
- No RTTI (typeid, dynamic_cast). The engines disable it. Use the existing RTTI enum pattern if type discrimination is needed.
- No `new`/`delete` in implementation code. Use std::unique_ptr for ownership, or pre-allocated pools.
- Mark functions `const` and `noexcept` where correct.
- Use `#pragma once` or include guards. Match existing engine style (include guards with `#ifndef`).
- Pimpl (Impl*) in headers is fine for hiding platform dependencies (GL headers). Do not use Pimpl just for compilation speed — these files are small.
- Naming: PascalCase for types, snake_case for local variables, UPPER_CASE for constants. Match engine convention.

## Rules

1. **Do not modify engine/cnc/modern.src/ or engine/ra/modern.src/ unless explicitly told to.** The 64-bit stabilization is in progress. This library is built alongside, not inside.

2. **Do not modify engine/libs/graphics/.** LegacySpriteProvider wraps it; it does not replace it.

3. **Read truth.src/ for reference only.** It contains the EA Remastered source including Megafile.cs and DLLInterface. Port logic from it, do not depend on it.

4. **Every public function needs a brief doc comment.** One line is fine. No boilerplate, no @param for obvious parameters.

5. **No dead code.** Do not stub out functions with TODO bodies. Either implement it or do not declare it yet.

6. **Test each step standalone before integrating.** MEG reader must work without the sprite provider. Atlas must work without GL. Layer compositor must work in CPU mode without GL.

7. **Think twice, implement once.** If you are unsure about an API, write the calling code first (how will CC_Draw_Shape use this?), then design the interface to match. Do not design interfaces in isolation.

8. **Use DBG() macros from dbg.h for debug logging.** Not fprintf(stderr), not std::cerr, not printf.

9. **Build with -j8.** Not -j$(nproc).

10. **Do not use --author on git commits.** Git config has the correct identity.

## Implementation Order

See PLAN.md for detailed steps. Summary:

```
Step 1: meg_reader + meta_parser       (standalone, no engine deps)
Step 2: ISpriteProvider + Legacy adapter (wraps libs/graphics/)
Step 3: HDSpriteProvider                (depends on 1+2)
Step 4: texture_atlas                   (depends on 2 or 3)
Step 5: render_layer + compositor       (parallel with 1-4, first engine touch)
Step 6: gl/gl_renderer                  (depends on 5)
Step 7: gl/gl_sprite_batch              (depends on 4+6)
Step 8: gl/gl_house_color               (depends on 7)
```

Steps 1, 2, and 5 can run in parallel. Step 5 is the first to touch engine code (gscreen.cpp, display.cpp).

## Key Design Decisions

### SpriteFrame owns nothing
SpriteFrame is a view — it points to pixels owned by the provider's cache. No copies, no refcounting. Valid until the provider is destroyed or the cache is flushed.

### ISpriteProvider is stateful
Providers load and cache data. They are created once at game init and live for the session. They are not per-frame throwaway objects.

### Atlas is built at load time
When a theater or scenario loads, all relevant SHP and TGA frames are packed into atlas pages. During gameplay, CC_Draw_Shape() does a table lookup for the AtlasRegion — no disk I/O, no decompression.

### GL is optional, not required
The compositor works in CPU mode (blit buffers like today). GL replaces the presentation path. If GL init fails, fall back to CPU. Both paths must produce correct output.

### One provider active at a time per shape
A shape is either legacy SHP or HD TGA. There is no blending or fallback between providers for the same shape. At init, the game decides: if HD assets are available for this entity, use HDSpriteProvider; otherwise LegacySpriteProvider.

## File Size Guidelines

- Headers: interface + doc comments. Under 100 lines.
- Implementations: one responsibility. Under 500 lines. If it grows beyond that, split by sub-responsibility.
- No file over 800 lines. If approaching that, the design needs revisiting.
