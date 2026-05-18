# `beast/type_name.h` — Human-Readable Type Name Demangling

This header provides a single template utility, `beast::type_name<T>()`, that returns a human-readable `std::string` representation of any C++ type, including its cv-qualifiers and reference category. Its purpose is purely diagnostic: producing legible type names for log messages, error strings, and counter labels rather than the raw mangled symbols that `typeid().name()` emits on GCC and Clang by default.

## The Problem It Solves

On MSVC, `typeid(T).name()` already yields something readable. On GCC and Clang, the same call returns a mangled symbol like `N4xrpl11KnownFormatsINS_18LedgerEntryFormatsEEE`, which is useless in a log file or error message. The standard C++ ABI exposes `abi::__cxa_demangle()` from `<cxxabi.h>` to reverse this — but calling it correctly (allocating and freeing the result buffer, guarding against MSVC where the header doesn't exist) requires boilerplate that consumers shouldn't repeat. `type_name.h` encapsulates that boilerplate once.

## How `type_name<T>()` Works

The function begins by stripping the reference from `T` via `std::remove_reference`, storing the result as `TR`. This is necessary because `typeid` ignores reference types entirely — `typeid(int&)` and `typeid(int)` return the same `type_info` — so applying `typeid` to the unreferenced type avoids a subtle loss of information while ensuring the demangler receives a concrete type.

On non-MSVC platforms, `abi::__cxa_demangle()` is called with the mangled name. The function returns a heap-allocated C string that the caller must `free()`. The implementation captures this pointer, copies it into a `std::string`, and immediately releases it — avoiding both a leak and a raw owning pointer lifetime issue. On MSVC, the `#ifndef _MSC_VER` guard skips this block entirely, leaving the already-readable MSVC name intact.

After demangling, the function manually reconstructs the qualifiers that were shed when the reference was stripped: `const` and `volatile` are appended as suffixes if `TR` carries them, and `&` or `&&` is appended depending on whether `T` was an lvalue or rvalue reference. This ordering — demangled base name, then cv-qualifiers, then reference category — matches the way C++ types are conventionally read right-to-left.

## Usage Across the Codebase

The function appears in four distinct contexts, each illustrating a different diagnostic use:

**`contract.h`** calls `beast::type_name<E>()` inside the `Throw<E>()` template to emit a log warning like `"Throwing exception of type std::runtime_error: ..."` before each throw. This makes every exception event a traceable log entry, surviving even broad `catch(...)` handlers that would otherwise swallow both the exception and any evidence of its type.

**`CountedObject.h`** uses `beast::type_name<Object>()` to name a function-local static `Counter` object that tracks how many instances of a class are alive. The static is initialized exactly once (C++11 thread-safe static initialization), and the demangled name is what appears in diagnostic dumps of live object counts — without it, the registry would contain meaningless mangled strings.

**`KnownFormats.h`** uses it in a CRTP base class constructor: `KnownFormats<Derived>` initializes `name_` with `beast::type_name<Derived>()`, letting the base class self-label without requiring derived classes to pass their names explicitly.

**`SlabAllocator.h`** uses it to generate a meaningful `std::runtime_error` message when a duplicate slab size is registered: `"SlabAllocatorSet<SomeType>: duplicate slab size"`.

## Design Notes

The function is not on any hot path — it is called during exception construction, counter registration (a one-time static init), or at object construction time in CRTP registries. The heap allocation inside `__cxa_demangle` is therefore acceptable.

The decision to append qualifiers after demangling rather than before is intentional: `__cxa_demangle` takes a mangled name, not a qualified type, and the mangled name already encodes the base type. Qualifiers from the `T` template parameter must be re-derived from `std::is_const`, `std::is_volatile`, and the reference traits because they were discarded by the `remove_reference` strip. This two-phase approach — demangle the stripped type, then re-attach qualifier context — correctly reconstructs names like `"std::string const&"` from a `T = const std::string&` instantiation.