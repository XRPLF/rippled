# `include/xrpl/protocol/RPCErr.h`

This header is a thin compatibility shim, declaring two explicitly deprecated utility functions for RPC error construction and detection in the `xrpl` namespace. The `// VFALCO NOTE these are deprecated` comment makes the intent unambiguous: these functions exist only to avoid breaking call sites that predate the richer error-handling infrastructure now found in `ErrorCodes.h`.

`rpcError(error_code_i iError)` constructs a fresh `Json::Value` object and populates it with canonical error fields by delegating to `RPC::inject_error()`. The return-by-value produces a self-contained error object ready for direct return from an RPC handler. The modern replacement is `RPC::make_error()`, which does the same thing under a name that belongs to the `RPC` sub-namespace where the rest of the error machinery lives.

`isRpcError(Json::Value jvResult)` duck-types a JSON value as an error response by checking for membership of the `jss::error` key — the same structural sentinel that `RPC::contains_error()` uses, making it the direct modern equivalent. Notably, the parameter is taken by value rather than `const` reference, a minor inefficiency that was never corrected given the function's deprecated status.

Both functions live outside the `RPC` namespace (a design anomaly noted in `ErrorCodes.h` with its own `VFALCO NOTE`), which is precisely why they were superseded. New code should use `RPC::make_error()`, `RPC::inject_error()`, and `RPC::contains_error()` from `ErrorCodes.h` directly.