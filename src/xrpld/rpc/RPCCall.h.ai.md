# `RPCCall.h` — RPC Command Origination and Dispatch

This header declares the entry points for *originating* XRPL RPC calls — the client-side machinery for turning a command into an HTTP request and getting a response back. It is the counterpart to the server-side `RPCHandler.h` (which handles calls arriving at a running node), sitting instead at the boundary where the `xrpld` binary or a test harness initiates communication with a node.

## The Two Dispatch Surfaces

The file exposes two fundamentally different invocation modes inside the `RPCCall` nested namespace.

`RPCCall::fromCommandLine()` is the entry point for the `xrpld` CLI. It takes a raw `std::vector<std::string>` of CLI tokens and the node's `Config`, drives the full parse-serialize-dispatch pipeline, writes the formatted response to `stdout`, and returns an integer exit code. Its implementation is deliberately thin: it calls `rpcClient()` with `RPC::apiCommandLineVersion` and prints the result.

`RPCCall::fromNetwork()` is for callers that have already structured their request as a `Json::Value` and know their target connection parameters (IP, port, credentials, path, optional HTTP headers). It accepts a `boost::asio::io_context&` and registers async `HTTPClient` callbacks through it — it does not block and does not own the event loop. The optional `callbackFuncP` receives the parsed `Json::Value` response; when omitted, the response is discarded. Inside, it injects HTTP Basic Auth into the headers map before delegating to `HTTPClient::request()` with a 256 MB response cap and a 30-second timeout — constants defined only in the implementation, not exposed here.

## The "Trusted Interface" Design Note

The comment above the `RPCCall` namespace is architecturally significant: *"This is a trusted interface, the user is expected to provide valid input… Error catching and reporting is not a requirement."* This is not negligence — it is an intentional design boundary. The CLI is operated by node administrators who understand the protocol; strict input validation and rich diagnostics belong in the server-side handler, which operates in an adversarial environment. Keeping the client path lean avoids duplicating the full validation logic of `RPCHandler.h`.

## `rpcCmdToJson()` — The Parsing Bridge

Declared outside the `RPCCall` namespace (making it a more general utility), `rpcCmdToJson()` translates a raw argument vector into a structured `Json::Value` ready for network submission. Internally it constructs an `RPCParser` — a private class in the implementation that maps each method name to a dedicated parse function via a static sorted command table. The dual-output pattern (return value + `retParams` out-parameter) separates the *translated request body* from the *raw invocation record* (`{ method, params }`). The latter is attached to error responses as `"rpc"` so callers can see exactly what was sent. The `apiVersion` parameter ensures the assembled JSON carries the correct `"api_version"` field; it is injected unconditionally unless the parse already produced an error or the request already carries a version.

## `rpcClient()` — The Shared Internal Path

`rpcClient()` is the most significant function in this interface. Its doc comment explicitly identifies it as the shared path used by both the CLI binary and unit tests, and its design reflects that dual purpose. Rather than driving I/O directly, it returns `std::pair<int, Json::Value>` — an exit code and response payload that test code can assert against without printing to stdout or managing a running server. Internally it:

1. Calls `rpcCmdToJson()` to translate the argument vector.
2. Reads server connection info from `Config` (falling back gracefully if no config is available — the `setup_ServerHandler` call is wrapped in a swallowed `std::exception` catch).
3. Spins up a *local* `boost::asio::io_context`, calls `RPCCall::fromNetwork()`, then calls `isService.run()` to block until the async operation completes. This is the pattern for "synchronous over async" — the calling thread blocks, but the underlying transport is still event-loop-driven.
4. Extracts the result from the JSON-RPC response envelope, mapping transport failures to `rpcJSON_RPC` and embedding diagnostic context (`"rpc"`, `"request_sent"`) in error outputs.

The `static_assert` at the top of the implementation (`rpcBAD_SYNTAX == 1 && rpcSUCCESS == 0`) is a guard ensuring the integer exit code contract with shell callers is not accidentally broken by changes to the error enum.

## Relationship to Sibling Files

`RPCCall.h` deliberately pulls in only `Config.h`, `ServiceRegistry.h`, `json_value.h`, and Boost.Asio — keeping the client path decoupled from the heavier server machinery. `RPCHandler.h` is included only in the `.cpp` implementation (for `setup_ServerHandler`), not in this header. `Role.h` and `Status.h`, which govern access control and error representation inside the handler pipeline, are entirely absent. `ServerHandler.h` is touched only to extract connection setup — the client has no awareness of how the server processes the request it sends.