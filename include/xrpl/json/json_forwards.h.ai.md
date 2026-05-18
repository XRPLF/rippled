# `json_forwards.h`

This header is a forward-declaration shim for the `Json` namespace. Its sole purpose is to let other headers reference `Json::Value`, `Json::StaticString`, and the iterator family without paying the compile-time cost of including the full `json_value.h` and all its transitive dependencies (`<map>`, `<vector>`, `<string>`, `xrpl/basics/Number.h`, etc.).

The two type aliases — `Int` as `int` and `UInt` as `unsigned int` — establish the canonical integer widths used throughout the JSON subsystem. Centralizing them here means any future platform-specific adjustment (e.g., switching to `int32_t`) is a single-point change rather than a scattered find-and-replace.

The five forward-declared entities (`StaticString`, `Value`, `ValueIteratorBase`, `ValueIterator`, `ValueConstIterator`) all receive their full definitions in `json_value.h`. Headers that only need to mention `Json::Value` in a function signature — a pointer, a reference, or a return type — can include `json_forwards.h` alone and stay insulated from the heavier type definition. Both `json_reader.h` and `json_writer.h` include this header alongside `json_value.h`, following the standard pattern of declaring the forward header first so that any intermediate consumer relying solely on `json_forwards.h` remains compatible.

Given how pervasive JSON manipulation is across the XRPL codebase — protocol serialization, RPC handling, transaction parsing — keeping this boundary clean has a measurable effect on incremental build times. The header is intentionally minimal by design.