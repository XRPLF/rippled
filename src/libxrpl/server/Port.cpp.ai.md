# `src/libxrpl/server/Port.cpp`

## Role in the System

This file implements the runtime logic for parsing and representing network port configuration for the XRPL server. It translates raw configuration text (loaded from `rippled.cfg`) into structured `Port` objects that govern how the server binds, which protocols it speaks, and which IP ranges are granted elevated access. The file is the sole implementation site for `parse_Port()`, the entry point called by `ServerHandler.cpp` during server startup.

## Two-Struct Design: `ParsedPort` vs. `Port`

The header defines two distinct structs. `ParsedPort` wraps `ip` and `port` as `std::optional<>`, while `Port` holds them as concrete values. This asymmetry is deliberate. The `ServerHandler.cpp` calling code first parses the top-level `[server]` section into a `ParsedPort common`, then iterates over each named port section, copies `common` into a fresh `ParsedPort`, and calls `parse_Port()` again to apply per-port overrides. Keeping IP and port optional allows the common section to supply defaults that individual port sections can leave absent without raising a parse error. Once all parsing completes, a `to_Port()` function in `ServerHandler.cpp` converts the `ParsedPort` into the final concrete `Port` used at runtime.

## `parse_Port()`: Field-by-Field Validation

`parse_Port()` works by sequentially extracting fields from the `Section` abstraction and validating each one before assigning it. The pattern throughout is: read with `section.get()`, validate inside a `try/catch`, log a human-readable error message referencing the field name and section, and call `Rethrow()` or `Throw<std::exception>()` to propagate failures. This pattern means a misconfigured port causes a clean error at startup rather than a silent default or undefined behaviour later.

Notable validation rules baked in here:
- **Port 0 is forbidden for `[server]`**: the root server section uses port 0 as a sentinel for "not configured," so the code explicitly rejects it as a literal value.
- **`send_queue_limit` of 0 is rejected**: a WebSocket queue with zero capacity would immediately disconnect every client; the parser treats it as an error. The default of 100 is set here if the key is absent.
- **`limit`**: connection limits accept the string `"unlimited"` (case-insensitive via `boost::iequals`) and map it to the integer 0; any other value must be a valid `uint16_t`.

Protocol names are split by RFC 2616 comma rules and inserted into a case-insensitive `std::set`. `Port::secure()` then checks this set for `"peer"`, `"https"`, `"wss"`, or `"wss2"` — all protocols that require TLS — without needing to worry about case normalisation.

The permessage-deflate (WebSocket per-message compression) options are read with `value_or()` throughout, so all seven PMD parameters have sensible defaults (compression enabled, 15-bit window sizes, level 8, memory level 4) that only need to appear in config when overriding.

## `populate()`: IP and Subnet Parsing

The most complex logic in the file lives in the file-static `populate()` helper, called twice by `parse_Port()` — once for the `admin` field and once for `secure_gateway`. Both fields accept a comma-separated list of IPv4 addresses, IPv6 addresses, or CIDR subnets, and the helper parses each entry uniformly into dual `network_v4`/`network_v6` vectors.

The parsing uses a deliberate two-pass approach. It first calls `beast::IP::Endpoint::from_string_checked()`, which returns an `optional` rather than throwing. If the result is valid, the input was a bare IP address (no prefix length). In that case:
- A **wildcard address** (`0.0.0.0` or `::`) triggers immediate expansion to `0.0.0.0/0` and `::/0` in both vectors, then breaks out of the loop entirely — there is no point processing further entries since every address is already covered.
- A **single concrete IP** is promoted to a host-route network by appending `/32` (IPv4) or `/128` (IPv6) before constructing the CIDR object. This unifies the data model so callers only ever deal with `network_v4`/`network_v6` objects, never raw addresses.

If `from_string_checked()` returns empty, the input is assumed to be in CIDR subnet notation. The code tries `make_network_v4()` first; if that throws `boost::system::system_error`, it falls back to `make_network_v6()`. If that also throws, the outer catch block logs the entry and re-throws.

A subtle but important correctness check follows subnet parsing: the constructed network is compared against its own `canonical()` form. `10.1.2.3/24` has a canonical form of `10.1.2.0/24` — the host bits must be zero for a network address. If the configured value is non-canonical, `populate()` logs a descriptive message identifying both the configured form and the correct canonical form, then throws. This prevents admins from accidentally granting access to a broader subnet than intended due to a typo in the host portion.

## Access Tiers: `admin` and `secure_gateway`

The two networks parsed by `populate()` serve distinct roles in the XRPL server's request-routing logic. Networks listed in `admin` grant callers full administrative API access, subject to IP-level verification at connection time. Networks listed in `secure_gateway` identify trusted proxy or gateway nodes whose HTTP headers (`X-Forwarded-For`, etc.) can be used for further identity assertions — this is how XRPL deployments behind load balancers can still propagate client identity to the node.

## `operator<<` for Logging

The stream operator outputs the port name, bound IP and port number, then iterates the admin and secure_gateway network vectors (both IPv4 and IPv6), and finally the protocol list via `protocols()`. The format is intentionally human-readable for startup logs and debugging; it does not attempt to reconstruct config syntax.