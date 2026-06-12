#[===================================================================[
  Lean 4 FFI integration for xrpld.

  Links xrpld against a locally built xrpl-lean4 static library and the
  Lean runtime shipped with the elan toolchain. Silently skipped when
  either the toolchain headers or the static library are not present.
#]===================================================================]

if(NOT TARGET xrpld)
    return()
endif()

set(LEAN_TOOLCHAIN "$ENV{HOME}/.elan/toolchains/leanprover--lean4---v4.28.0")
set(XRPL_LEAN4 "$ENV{HOME}/Documents/workspace-clion/rippled-formal-verification/formal_verification")
set(LEAN_STATIC_LIB "${XRPL_LEAN4}/.lake/build/lib/libXRPL_XRPL.a")

if(
    tests
    AND EXISTS "${LEAN_TOOLCHAIN}/include/lean/lean.h"
    AND EXISTS "${LEAN_STATIC_LIB}"
)
    target_include_directories(xrpld PRIVATE ${LEAN_TOOLCHAIN}/include)
    target_link_libraries(
        xrpld
        ${LEAN_STATIC_LIB}
        ${XRPL_LEAN4}/.lake/build/lib/libLeanDeps.a
        ${LEAN_TOOLCHAIN}/lib/lean/libLake.a
        ${LEAN_TOOLCHAIN}/lib/lean/libleanshared.dylib
        /opt/homebrew/opt/gmp/lib/libgmp.a
    )
else()
    message(STATUS "Lean FFI: skipped (toolchain or static lib not found)")
endif()
