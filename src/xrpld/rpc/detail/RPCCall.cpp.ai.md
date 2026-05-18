# `src/xrpld/rpc/detail/RPCCall.cpp`

This file is the backbone of the XRPL command-line RPC client. It answers the question: given a user-typed command like `account_info rXYZ validated`, how does that sequence of strings become a valid HTTP POST request sent to a running `xrpld` node, and how does the response come back? It owns everything from argument parsing to HTTP framing to asynchronous response handling.

## Overall Structure

The file lives entirely in `namespace xrpl` and exports a handful of symbols through `RPCCall.h`: `RPCCall::fromCommandLine`, `RPCCall::fromNetwork`, `rpcCmdToJson`, and `rpcClient`. Internally it defines three supporting types — `RPCParser`, `RPCCallImp`, and a file-local `RequestNotParsable` exception — plus the free function `createHTTPPost`.

## `createHTTPPost` — Minimal HTTP Framing

This function produces a raw HTTP/1.0 POST string, not a proper HTTP library abstraction. The comment at the top is explicit: *"This ain't Apache."* The choice of HTTP/1.0 rather than 1.1 avoids persistent-connection complexity for what is essentially a fire-and-forget single-request client. `Content-Length` is set directly from `strMsg.size()`, and additional headers from `mapRequestHeaders` are appended as-is. No validation is performed on header key/value format — the caller is trusted to supply valid strings.

## `RPCParser` — Positional Argument to JSON Translation

`RPCParser` is the most substantial piece of the file. It converts a command name and its positional string arguments into a `Json::Value` object that matches the expected JSON-RPC parameter schema for each method. The class is instantiated per-call and carries an `apiVersion_` member plus a `beast::Journal` for tracing.

The `parseCommand` method drives dispatch through a static `constexpr` array of `Command` structs — each carrying the method name, a member-function pointer to a `parseXxx` method, a minimum parameter count, and a maximum (with `-1` meaning unbounded). Dispatch is a plain linear scan over roughly fifty entries. That's an intentional tradeoff: the array is small, entirely L1-cache-resident, and avoids the indirection of a hash map. The arity check happens before the parse function is called, so each parser can assert its preconditions without defensive parameter-count guards.

The `parseXxx` methods follow a consistent pattern: construct a `Json::Value` object from positional params, return an `rpcError(...)` JSON object on bad input rather than throwing. This keeps error handling at the caller level.

Several helper methods are worth noting:

- `jvParseLedger` is the canonical ledger identifier normalizer. It distinguishes the string sentinels `"current"`, `"closed"`, and `"validated"` (written to `ledger_index`), 64-character hex strings (written to `ledger_hash`), and numeric sequences cast via `beast::lexicalCast<uint32_t>` (which throws on non-integer input). The fact that this still carries a `// TODO New routine` comment signals that not all callers have been migrated.

- `jvParseCurrencyIssuer` uses a Boost.Regex pattern anchored to three ISO-charset characters, optionally followed by a `/` and issuer string. It returns an `RPC::make_param_error` JSON value on mismatch — again, not an exception.

- `validPublicKey` accepts both base58-encoded and hex-encoded public keys, covering both account and node key types.

- `parseJson` and `parseJson2` handle passthrough of pre-formed JSON payloads. `parseJson2` additionally validates that the batch or single-call payload carries both `jsonrpc: "2.0"` and `ripplerpc: "2.0"` fields — the latter being XRPL's own extension discriminator — and preserves error context (`id`, `jsonrpc`, `ripplerpc`) when parsing fails.

- Event-driven commands (`subscribe`, `unsubscribe`, `path_find`) are routed to `parseEvented`, which unconditionally returns `rpcNO_EVENTS`. This encodes the architectural boundary: this HTTP-based synchronous path simply does not support WebSocket-style subscriptions.

## `JSONRPCRequest` — JSON-RPC Envelope

A thin wrapper that assembles `{ "method": ..., "params": ..., "id": ... }` and appends a newline. This conforms to JSON-RPC 1.0 framing, which the comment notes is used for compatibility despite partial adoption of 1.1/2.0 conventions elsewhere.

## `RPCCallImp` — Async Callback Plumbing

`RPCCallImp` is a utility struct with only static methods, acting as a namespace for the two async callbacks passed to `HTTPClient::request`:

- `onRequest` calls `createHTTPPost` and `JSONRPCRequest` to write the raw HTTP payload into a `boost::asio::streambuf`.

- `onResponse` is where the response lifecycle is managed. An empty reply body throws `std::runtime_error` ("no response from server"). A body beginning with `"Unable to parse request"` or `"invalid_API_version"` throws the file-local `RequestNotParsable` — this distinction matters because `rpcClient` catches it separately to emit `rpcINVALID_PARAMS` instead of the generic `rpcINTERNAL`. Any body that fails JSON parsing also throws `std::runtime_error`. Only after successful parsing does it invoke the callback.

## `rpcCmdToJson` — Parser Orchestration

`rpcCmdToJson` instantiates `RPCParser`, collects all arguments after `args[0]` into a JSON array, dispatches `parseCommand`, and then injects `api_version` into every non-error result that doesn't already carry one. The injection applies element-wise for batch arrays. This is where API versioning becomes part of the wire format for outgoing CLI requests.

## `rpcClient` — Full Execution Path

`rpcClient` is the workhorse called by both the command-line entry point and unit tests. Its flow is:

1. Parse arguments via `rpcCmdToJson`.
2. If the parse returned an error JSON object, return immediately without touching the network.
3. Otherwise, attempt to read the server address from `ServerHandler::Setup` (gracefully ignoring exceptions so the client works without a config file), then override with `config.rpc_ip` if present.
4. Inject `admin_user` / `admin_password` from the client config into the request.
5. Create a local `boost::asio::io_context`, call `RPCCall::fromNetwork`, then `isService.run()` synchronously, which blocks until the one outstanding async request completes.
6. Unwrap the `"result"` key from the response, or synthesize `rpcJSON_RPC` on transport error.
7. Catch `RequestNotParsable` (from `onResponse`) → `rpcINVALID_PARAMS`; catch any other exception → `rpcINTERNAL`.

The design choice to use `io_context::run()` synchronously — creating and destroying the io_context per call — is deliberate for the CLI use case. It avoids shared-state complexity while paying a modest setup cost that is immaterial for interactive one-shot commands.

## `RPCCall::fromCommandLine` and `RPCCall::fromNetwork`

`fromCommandLine` is the outermost CLI entry point. It simply calls `rpcClient` with `RPC::apiCommandLineVersion` and prints the result to stdout as styled JSON. `fromNetwork` is the reusable async entry point. It builds the HTTP Basic authorization header from username/password with base64 encoding, sets a 256 MiB response cap (`RPC_REPLY_MAX_BYTES`) and a 30-second timeout, then hands everything to `HTTPClient::request`. Both constants are hardcoded — there is no per-request configuration for them.