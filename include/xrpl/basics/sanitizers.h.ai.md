# `include/xrpl/basics/sanitizers.h`

## Purpose

This header provides a single compiler-attribute macro, `XRPL_NO_SANITIZE_ADDRESS`, that suppresses AddressSanitizer (ASan) and Hardware AddressSanitizer (HwASan) instrumentation on individual functions. It exists because both sanitizers instrument every memory access via shadow memory, and certain legitimate C++ control-flow patterns — notably `throw`, `catch`, and coroutine stack switches — can cause ASan to report false positives.

## The Macro

On GCC and Clang, `XRPL_NO_SANITIZE_ADDRESS` expands to `__attribute__((no_sanitize("address", "hwaddress")))`, placed on a function declaration to tell the compiler to skip shadow-memory instrumentation for that function only. On MSVC and other non-GCC/Clang compilers the macro expands to nothing, so annotated code compiles cleanly everywhere without `#ifdef` noise at each call site.

## Design Rationale

The suppression is deliberately **surgical rather than global**. Running a sanitizer build and then disabling ASan process-wide would defeat the entire point of the instrumentation. By confining the annotation to the specific functions that trigger false positives, the rest of the codebase remains fully checked.

The primary consumer is `contract.h`, which applies the macro to `Throw<E>()` and `Rethrow()` — the two functions that perform the actual `throw` statement in XRPL's Programming-by-Contract layer. Both functions are `[[noreturn]]` and transfer control non-linearly, which is exactly the pattern ASan struggles with. Keeping the macro in its own header rather than inside `contract.h` preserves the option to annotate other throw-sites or coroutine switch-points across the codebase without creating a circular dependency on the broader contract machinery.