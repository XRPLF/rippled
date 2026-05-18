# `comparators.h` — MSVC Compatibility Shims for `boost::bimap`

This file exists to paper over a specific compiler/library incompatibility: MSVC 2019 (16.9.0+) added `[[nodiscard]]` to the `operator()` of `std::less` and `std::equal_to`, which collides with how `boost::bimap` validates comparators.

### The Problem

`boost::bimap` checks that a provided comparator satisfies the `BinaryFunction` concept by invoking `operator()` and discarding the return value. When MSVC decorated those operators with `[[nodiscard]]`, that validation step began emitting warnings-treated-as-errors (or outright compilation failures), since the entire point of the check is to call the function and throw the result away. The two behaviors are fundamentally at odds: Boost deliberately ignores the return value, and MSVC now insists you don't.

### The Solution

Under `_MSC_VER`, `xrpl::less<T>` and `xrpl::equal_to<T>` are thin wrapper structs whose `operator()` delegates to the standard counterparts but is itself *not* marked `[[nodiscard]]`. The `result_type = bool` member alias satisfies the older Boost `BinaryFunction` concept checks, which sometimes inspect this typedef directly. On any non-MSVC compiler, the types are simple `using` aliases to `std::less<T>` and `std::equal_to<T>`, meaning there is zero overhead or behavioral difference on GCC, Clang, or other targets.

### Usage in the Codebase

Both known consumers — `Bootcache.h` in the PeerFinder subsystem and `ledgers.h` in the consensus simulation framework — include this header specifically because they use `boost::bimap` with ordered collection types. By substituting `xrpl::less` where `std::less` would ordinarily appear, those files compile cleanly across MSVC and non-MSVC toolchains without any conditional compilation at the call site. The fix is fully transparent to the caller.

### Design Notes

The `#else` branch uses `using` aliases rather than repeating the struct definitions — the right call, since it keeps non-MSVC builds on the real standard types with all their optimizations and specializations (including the transparent `void` specializations). The MSVC wrapper structs handle the default `T = void` template argument but do not attempt to re-implement the transparent comparator `void` specialization; this is acceptable because `boost::bimap` collection types always require explicitly typed comparators anyway.

This is a narrow, surgical fix. The file makes no attempt to be a general comparator utility — it solves exactly one problem (stripped `[[nodiscard]]`) in exactly the context where it appears (ordered `boost::bimap` collections), and nothing more.