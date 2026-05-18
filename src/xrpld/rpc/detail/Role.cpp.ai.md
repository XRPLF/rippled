# `src/xrpld/rpc/detail/Role.cpp` — RPC Access Control and Role Assignment

This file implements the access-control layer that sits at the entry point of every inbound RPC and WebSocket connection. Its job is to classify a connecting client into one of five privilege levels — `GUEST`, `PROXY`, `IDENTIFIED`, `ADMIN`, or `FORBID` — and then hand off to the resource-management layer with the appropriate throttling policy. It is the first line of defence against unauthorized use of privileged RPC commands and against resource exhaustion.

## The Role Taxonomy

The `Role` enum (declared in `Role.h`) establishes a hierarchy of trust:

- **GUEST** — an anonymous connection from an unrecognized IP. Subject to resource limits.
- **PROXY** — a connection forwarded through a configured `secure_gateway` reverse proxy, but without a user identity in the forwarded headers. Subject to limits based on the real client IP.
- **IDENTIFIED** — a connection through a trusted proxy that *did* supply a user identity. Granted unlimited resources, but still restricted from a few administrative RPC commands.
- **ADMIN** — a connection that passed both the IP whitelist and optional credential check. Unlimited resources and full RPC access.
- **FORBID** — returned when an operation required `ADMIN` but the caller did not qualify. This sentinel value lets callers short-circuit without separate admin-check logic.

## Role Determination Pipeline

The central function is `requestRole()`. It evaluates the incoming connection in strict priority order:

1. Call `isAdmin()`, which in turn calls `ipAllowed()` against `port.admin_nets_v4`/`admin_nets_v6` and then `passwordUnrequiredOrSentCorrect()`. If both pass, return `Role::ADMIN` immediately.
2. If the `required` parameter is `Role::ADMIN` but step 1 failed, return `Role::FORBID`. This prevents privilege escalation without exposing a detailed reason.
3. Call `ipAllowed()` against `port.secure_gateway_nets_v4`/`secure_gateway_nets_v6`. If the remote IP belongs to a trusted proxy network, return `Role::IDENTIFIED` when a non-empty `user` string was extracted from the request headers, or `Role::PROXY` otherwise.
4. Fall through to `Role::GUEST`.

The `required` parameter is a declared minimum. The caller asserts what privilege level its operation needs; `requestRole()` either meets or denies that bar. This is more composable than scattering `isAdmin()` calls across handlers.

## IP Subnet Matching

`ipAllowed()` avoids simple string comparison in favour of proper CIDR semantics. It converts the remote address to a host-prefix network (`/32` for IPv4, `/128` for IPv6) using `boost::asio::ip::make_network_v4/v6`, then tests `is_subnet_of(configured_net) || (host_net == configured_net)` against every entry in the configured lists. The dual check is needed because Boost's `is_subnet_of` treats a /32 as a proper subnet of a /24 but two identical /32s as equal-but-not-a-subnet; the `||` handles the edge case where the admin network itself is configured as a single host address.

## Credential Validation

`passwordUnrequiredOrSentCorrect()` reflects an important operational choice: credentials are optional. If neither `admin_user` nor `admin_password` is set on the port, then any request from an admin-whitelisted IP is accepted. If either field is set, both must be present in the JSON `params` as string values matching the port configuration exactly. The function carries a precondition enforced by `XRPL_ASSERT`: it should only be called when the admin net list is non-empty, which is guaranteed by the `isAdmin()` call chain.

Credentials are sent in-band in the JSON-RPC body (`params["admin_user"]`, `params["admin_password"]`), not in HTTP headers. The `isString()` guard before the comparison ensures a type-confusion attack through JSON cannot bypass the check with a non-string value.

## Resource Consumer Allocation

`requestInboundEndpoint()` bridges role determination to the `Resource::Manager`. Unlimited roles (`ADMIN` and `IDENTIFIED`) receive a `newUnlimitedEndpoint` consumer that bypasses all rate-limiting accounting. All others go through `newInboundEndpoint`, which takes a boolean flag indicating whether the connection is proxied and the `forwardedFor` string so that rate limiting is applied to the *real* client IP rather than the proxy's address.

## Forwarded-For Header Parsing

`forwardedFor()` and the file-local `extractIpAddrFromField()` exist because reverse proxies are first-class citizens in the XRPL deployment model. The function supports both standards:

- **RFC 7239 `Forwarded:`** — scans for `for=` (case-insensitively), then extracts up to the next `,` or `;` separator to isolate the first hop.
- **Legacy `X-Forwarded-For:`** — takes only the first comma-delimited entry, which by convention is the originating client.

`extractIpAddrFromField()` is deliberately defensive. It handles leading/trailing whitespace (including CRLF), double-quoted addresses (legal in RFC 7239), IPv6 addresses wrapped in square brackets, and IPv4 addresses with an appended port number (`:8080`). The IPv6 detection heuristic — skip leading hex digits, and if the next character is a colon it must be IPv6 — correctly avoids stripping the address when it has no port suffix. Invalid or malformed inputs return an empty `string_view` rather than throwing, so a malformed header never breaks the role-assignment path.

## Relationship to the Broader RPC System

In `ServerHandler.cpp`, every new HTTP and WebSocket connection immediately calls `requestRole()` followed by `requestInboundEndpoint()`, embedding the resulting `Resource::Consumer` into the session object before any RPC dispatch occurs. This ensures throttling is installed unconditionally and cannot be bypassed by arriving on an unusual code path. Individual RPC command handlers subsequently inspect the role to gate privileged operations, but they depend entirely on this file having set the role correctly at connection time.