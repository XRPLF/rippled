# `include/xrpl/server/Port.h` — Server Port Configuration

`Port.h` defines the core data structures that describe how a rippled node listens for incoming connections. Every network endpoint the server opens — whether for HTTP/JSON-RPC, WebSocket, HTTPS, WSS, or the peer-to-peer protocol — is governed by one `Port` instance. This file is the contract between the configuration layer and the runtime server machinery.

## The Two-Struct Design: `ParsedPort` and `Port`

The header declares two related but deliberately distinct structs. `ParsedPort` is the *mutable parsing target*, holding an `std::optional<boost::asio::ip::address>` and `std::optional<std::uint16_t>` for `ip` and `port`. `Port` is the *resolved runtime configuration* where those same fields are non-optional bare values. This asymmetry is intentional and reflects the two-phase config loading pattern used in `ServerHandler.cpp`.

Rippled's config file allows a `[server]` section to define common defaults, and individual `[port_rpc]`/`[port_peer]`/etc. sections to override them. The `parse_Port()` function writes into a `ParsedPort`, so a common-defaults instance can be copied into a per-port instance before `parse_Port()` is called again — inherited optionals remain unset if the specific section doesn't override them, but they were set by the common section copy. Once parsing is complete, `to_Port()` in `ServerHandler.cpp` transfers fields from `ParsedPort` into a fully-validated `Port`, throwing if the non-negotiable fields (`ip`, `port`, `protocol`) are still absent.

`Port` alone holds the live `std::shared_ptr<boost::asio::ssl::context>`. This context can only be constructed once the SSL key, cert, and chain paths are finalised; it doesn't belong in the partially-built `ParsedPort` stage.

## Protocol Set and Case-Insensitive Comparison

Both structs store the protocol list as `std::set<std::string, boost::beast::iless>`. The `iless` comparator makes protocol name matching case-insensitive, so `"WS"` and `"ws"` are treated as the same entry. The `parse_Port()` implementation splits the comma-separated `protocol =` config value using RFC 2616 comma parsing and inserts each token. The resulting set drives two query helpers on `Port`:

- `websockets()` — returns `true` if any WebSocket protocol (`ws`, `wss`, `ws2`, `wss2`) is enabled. Used by the server's `Door` to decide whether to upgrade connections.
- `secure()` — returns `true` if any TLS-requiring protocol (`https`, `wss`, `wss2`, `peer`) is listed. The server uses this to decide whether to initialise the `ssl::context` and wrap connections in TLS.
- `protocols()` — returns a comma-joined string for logging (used by `operator<<`).

## Network Access Control: Admin and Secure-Gateway Nets

Two independent IP allowlists control elevated privilege. `admin_nets_v4` / `admin_nets_v6` restrict which source addresses can issue administrative RPC commands. `secure_gateway_nets_v4` / `secure_gateway_nets_v6` identify trusted reverse-proxy addresses whose `X-Forwarded-For` headers are trusted for rate-limiting and auth-bypass.

Both sets are populated by the static `populate()` helper in Port.cpp. The parsing logic handles three forms of input:

1. **Bare unspecified address** (`0.0.0.0` or `::`): expands to `0.0.0.0/0` and `::/0` immediately, then stops processing further entries — subsequent entries would be redundant.
2. **Single IP address**: promoted to a `/32` (IPv4) or `/128` (IPv6) host-route so it fits the `network_vN` container.
3. **CIDR subnet**: parsed directly, but validated against its canonical form. A misconfigured entry like `10.1.2.3/24` — where the host bits are non-zero — triggers an error showing both the invalid input and the correct canonical address `10.1.2.0/24`.

Keeping both IPv4 and IPv6 in separate vectors avoids type-erasure overhead and allows direct CIDR membership tests against typed endpoints later.

## Connection Limits

`limit` bounds the total number of simultaneous connections on the port. The value `0` means unlimited, and the config parser accepts the string `"unlimited"` as a synonym. `ws_queue_limit` caps the size of the per-WebSocket-connection outbound send queue; if the queue fills — typically because a slow client is not reading — the server disconnects. Its default when omitted from config is 100 messages, and zero is explicitly rejected as invalid.

## WebSocket Compression

`pmd_options` of type `boost::beast::websocket::permessage_deflate` gives fine-grained control over per-message DEFLATE compression. `parse_Port()` maps seven individual config keys onto this struct: `permessage_deflate` (master enable), `compress_level`, `memory_level`, `client_max_window_bits`, `server_max_window_bits`, `client_no_context_takeover`, and `server_no_context_takeover`. The defaults — compression enabled, window bits 15, compress level 8, memory level 4 — favour throughput for large JSON responses at the cost of some memory.

## Relationship to `Server` and `ServerImpl`

`Server::ports()` in `Server.h` accepts `std::vector<Port>` and returns an `Endpoints` map (named endpoint → TCP endpoint). `ServerImpl` stores those ports as `std::vector<Port> ports_` and spawns one `Door<Handler>` per entry. The `Door` reads the port's protocol set and SSL context to decide which connection types to accept, meaning `Port` is the single source of truth for all per-listener policy from config parse time through the full connection lifecycle.