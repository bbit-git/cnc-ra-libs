# engine/libs/crypto — Standalone MIX file reading with Blowfish/RSA crypto
#
# Compiles the RA engine's crypto and I/O primitives without the game engine.
# Provides MixReader API for reading both plain (CNC) and encrypted (RA) MIX files.

set(RA_SRC ${CMAKE_SOURCE_DIR}/engine/ra/modern.src)
set(CRYPTO_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mixread STATIC
    # Our clean API
    ${CRYPTO_DIR}/mixread.cpp

    # RA crypto primitives (self-contained, no engine deps)
    ${RA_SRC}/blowfish.cpp
    ${RA_SRC}/blwstraw.cpp
    ${RA_SRC}/pk.cpp
    ${RA_SRC}/pkstraw.cpp
    ${RA_SRC}/straw.cpp
    ${RA_SRC}/pipe.cpp
    ${RA_SRC}/blowpipe.cpp
    ${RA_SRC}/pkpipe.cpp
    ${RA_SRC}/crc.cpp
    ${RA_SRC}/crcstraw.cpp
    ${RA_SRC}/crcpipe.cpp
    ${RA_SRC}/random.cpp
    ${RA_SRC}/rndstraw.cpp
    ${RA_SRC}/sha.cpp
    ${RA_SRC}/mp.cpp
    ${RA_SRC}/int.cpp
    ${RA_SRC}/buff.cpp

    # INI parsing + base64 (for PKey initialization)
    ${RA_SRC}/ini.cpp
    ${RA_SRC}/base64.cpp
    ${RA_SRC}/readline.cpp
    ${RA_SRC}/xpipe.cpp
    ${RA_SRC}/b64pipe.cpp
    ${RA_SRC}/b64straw.cpp
    ${RA_SRC}/cstraw.cpp
    ${RA_SRC}/fixed.cpp

    # File I/O (need compat_shim.h instead of function.h)
    ${RA_SRC}/rawfile.cpp
    ${RA_SRC}/ramfile.cpp
    ${RA_SRC}/xstraw.cpp
)

target_include_directories(mixread
    PUBLIC  ${CRYPTO_DIR}
    PRIVATE ${RA_SRC}           # RA engine headers
            ${CRYPTO_DIR}       # compat_shim.h
)

target_compile_features(mixread PUBLIC cxx_std_17)

# The RA source uses legacy C++ patterns
target_compile_options(mixread PRIVATE
    -fpermissive
    -Wno-write-strings
    -Wno-narrowing
    -Wno-register
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-deprecated
    -Wno-return-type
)

# Force compat_shim.h as a prefix header for ALL RA source files.
# This provides stubs for function.h, redirects <new.h> -> <new>, etc.
target_compile_options(mixread PRIVATE
    -include ${CRYPTO_DIR}/compat_shim.h
)

# Suppress bool/true/false redefinition from wwfile.h
target_compile_definitions(mixread PRIVATE
    TRUE_FALSE_DEFINED=1
)
