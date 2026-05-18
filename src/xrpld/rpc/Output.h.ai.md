# `src/xrpld/rpc/Output.h`

This header defines a lightweight streaming-sink abstraction for the XRPL RPC layer, mirroring the pattern established in `include/xrpl/json/Output.h` for the JSON serialization subsystem.

`Output` is a type alias for `std::function<void(boost::string_ref const&)>`. It models a streaming write sink — a callable accepting successive string fragments without any knowledge of where those bytes ultimately land. Consumers of RPC response data (network sockets, in-memory buffers, test harnesses) all satisfy the same `Output` interface, which decouples response generation from delivery.

`stringOutput()` is the sole concrete factory provided here. It captures a `std::string&` by reference and returns a lambda that appends each fragment via `s.append(b.data(), b.size())`. This is the standard sink for collecting an entire RPC response into a single string, commonly used in tests or when a caller needs a fully-materialized result.

The notable difference from its counterpart in `Json::Output` is the Boost string type used: this file depends on the older `boost::utility/string_ref` header, while `include/xrpl/json/Output.h` uses `boost::beast::string_view`. Both model the same non-owning string-view concept, but the divergence suggests this RPC-layer variant predates the migration toward Boost.Beast primitives throughout the codebase. No files in the repository currently `#include` this header directly, making it a vestigial parallel to the canonical JSON output infrastructure rather than an actively consumed interface.