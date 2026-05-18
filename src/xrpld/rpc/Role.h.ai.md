# `src/xrpld/rpc/Role.h` — RPC Access Control and Privilege Classification

This header is the gatekeeper for XRPL's RPC permission model. Every inbound JSON-RPC or WebSocket request enters the server with an unknown privilege level; this file defines the classification logic that converts a raw IP address, a `Port` configuration, and optional HTTP credentials into one of six discrete roles, which downstream code then uses to gate commands and enforce resource limits.

## The `Role` Enum

```cpp
enum class Role { GUEST, USER, IDENTIFIED, ADMIN, PROXY, FORBID };
```

The six values represent a coarse privilege ladder, but not a strictly linear one:

- `GUEST` — unauthenticated external caller; subject to rate limiting, cannot run admin commands.
- `USER` — defined but currently unused in the assignment logic; reserved for future use.
- `IDENTIFIED` — a caller arriving through a trusted reverse proxy (`secure_gateway`) that has forwarded a user identity via the `X-User` HTTP header. Granted unlimited resources like `ADMIN`, but cannot run all admin-only commands. This models a multi-tenant operator where each end-user is metered at the proxy level, not the node.
- `ADMIN` — caller from an admin-listed IP (and optionally matching a username/password); has full command access and unlimited resources.
- `PROXY` — a trusted reverse proxy connection where no user identity header was provided. The proxy itself is unlimited for the purpose of forwarding, but individual end users are tracked by their forwarded IP address.
- `FORBID` — access denied; returned when a command requires `ADMIN` but the caller failed the IP/credential check.

The critical design choice here is separating `IDENTIFIED` from `ADMIN`. A secure gateway (such as a load balancer or API gateway you control) can get resource-exempt throughput for its users without those users having the ability to call `stop`, `ledger_request`, or other administrative commands. This prevents a privileged pipe from becoming a backdoor.

## `requestRole()` — The Central Decision Function

The implementation in `detail/Role.cpp` evaluates three independent checks in order:

1. **Admin check**: `ipAllowed(remoteIp, port.admin_nets_v4, port.admin_nets_v6)` AND `passwordUnrequiredOrSentCorrect(port, params)`. If both pass, return `ADMIN` immediately.
2. **Required-but-denied**: If the calling code required `ADMIN` but step 1 failed, return `FORBID`.
3. **Secure gateway check**: If the remote IP falls within `port.secure_gateway_nets_v4/v6`, the result depends on whether a non-empty `user` string arrived in the `X-User` HTTP header — `IDENTIFIED` if present, `PROXY` if absent.
4. **Fallthrough**: `GUEST`.

The `user` parameter comes from the HTTP layer (`JsonContext::Headers::user`), which is populated only after `ipAllowed()` confirms the request came through a trusted secure gateway. This means untrusted callers cannot self-elevate to `IDENTIFIED` by simply sending the `X-User` header — the IP gate must pass first.

## `ipAllowed()` — Subnet Membership

Rather than exact-IP comparison, `ipAllowed()` treats the remote address as a `/32` (IPv4) or `/128` (IPv6) host network, then checks whether it is a subnet of or equal to each configured CIDR block. The dual-vector approach (`nets4` / `nets6`) keeps the two address families completely separate, avoiding any cross-family confusion. The function serves double duty: it's called for both the `admin_nets` check and the `secure_gateway_nets` check.

## `requestInboundEndpoint()` — Connecting Role to the Resource Manager

This function translates the assigned role into a `Resource::Consumer`, the object that tracks and enforces rate limits for the lifetime of the connection:

- `ADMIN` and `IDENTIFIED` (via `isUnlimited`) → `manager.newUnlimitedEndpoint(remoteAddress)` — bypasses all throttling.
- `PROXY` → `manager.newInboundEndpoint(remoteAddress, /*isProxied=*/true, forwardedFor)` — uses the forwarded IP for per-client accounting rather than the proxy's own address.
- `GUEST` → `manager.newInboundEndpoint(remoteAddress, false, {})` — standard rate-limited endpoint tracked by the actual remote IP.

This is where the `PROXY` role's semantics become concrete: the resource manager uses the `X-Forwarded-For` / `Forwarded` IP to create a consumer entry that is distinct from the proxy's own IP, so many end users behind the same proxy each get their own rate-limit bucket rather than sharing (and potentially DoS'ing) one.

## `forwardedFor()` — Defensive Header Parsing

The `forwardedFor()` function (implemented in `detail/Role.cpp`) is more complex than it first appears. It handles two header standards in priority order:

1. **RFC 7239 `Forwarded` field** — finds the first `for=` token (case-insensitively), then delegates to `extractIpAddrFromField()`.
2. **De-facto `X-Forwarded-For` field** — takes the first comma-delimited entry.

`extractIpAddrFromField()` is a careful parser that strips leading/trailing whitespace, handles optional double-quote wrapping, removes `[...]` brackets from IPv6 literals, and strips any appended port number for IPv4 addresses. The care taken here reflects the real-world messiness of proxy header formats and the security sensitivity: this value is used for per-user rate limiting, so both false negatives (losing the real IP) and false positives (accepting a spoofed value) have consequences.

## Usage Context

`Role.h` is included by `Context.h`, which embeds a `Role` field directly into `RPC::Context` — the struct passed to every handler. This means every handler can call `isUnlimited(context.role)` or branch on `context.role == Role::ADMIN` without having to repeat any access-control logic. The `WSInfoSub` class in `detail/WSInfoSub.h` demonstrates the WebSocket path: it calls `ipAllowed()` directly on construction to decide whether to capture the `X-User` and `X-Forwarded-For` headers at all, ensuring that only gateway-sourced WebSocket subscriptions carry an identity.