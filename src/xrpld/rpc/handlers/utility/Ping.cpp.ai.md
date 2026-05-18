# `Ping.cpp` — RPC Ping Handler

`Ping.cpp` implements `doPing`, the handler for the XRPL `ping` RPC command. The function lives under `src/xrpld/rpc/handlers/utility/` alongside `Random.cpp`, in a subdirectory reserved for stateless utility handlers that require no ledger access and produce no side effects.

## Purpose

The `ping` command exists not as a simple liveness check but as a session introspection tool. A client calling `ping` learns what privilege level the server has assigned them, who the server thinks they are, and whether their connection is exempt from rate limiting. This makes it valuable for operators and API proxies to verify their authentication setup and connection classification without issuing a real ledger query.

## Role-Conditional Response

The handler switches on `context.role`, an enum defined in `Role.h` with values `GUEST`, `USER`, `IDENTIFIED`, `ADMIN`, `PROXY`, and `FORBID`. The response is sparse by design:

- **`ADMIN`**: adds `"role": "admin"`. Admin clients have unrestricted access to all RPC commands.
- **`IDENTIFIED`**: adds `"role": "identified"`, `"username"` from `context.headers.user`, and conditionally `"ip"` from `context.headers.forwardedFor`. This role is assigned by a `secure_gateway` proxy that has authenticated a downstream user — the username is passed via HTTP header.
- **`PROXY`**: adds `"role": "proxied"` and conditionally `"ip"`. Proxy clients get a forwarded-IP hint but no username identity.
- **`GUEST` / `USER` / `FORBID`**: fall through the `default` branch, producing an empty JSON object. These callers learn nothing about their classification beyond the fact that the call succeeded.

The `forwardedFor` field is only written when non-empty, avoiding a spurious `"ip": ""` key for direct connections. This is a quiet defensive pattern rather than a strict validation — the field is already trusted from the upstream HTTP/WebSocket layer.

## WebSocket-Only `unlimited` Field

The `context.infoSub` pointer is non-null only for WebSocket sessions; HTTP requests leave it null. When present, the handler checks `infoSub->getConsumer().isUnlimited()` and sets `"unlimited": true` if the consumer is exempt from resource throttling. The null check is the only guard — there is no equivalent field for HTTP callers, so the key simply never appears in HTTP responses.

## Context Dependencies

`doPing` takes a `RPC::JsonContext` (defined in `Context.h`), which extends the base `Context` struct with a `Headers` inner struct carrying `user` and `forwardedFor` as `std::string_view`s. These views are populated by the HTTP/WebSocket dispatch layer before any handler runs, so `doPing` never needs to parse raw headers itself. The `role` field on the base `Context` is resolved by `requestRole()` (in `Role.h`) prior to dispatch, keeping authorization logic entirely outside the handler.

The use of `jss::` namespace constants (`jss::role`, `jss::username`, `jss::ip`, `jss::unlimited`) rather than raw string literals ensures that JSON key names are consistent across the entire codebase and subject to a single point of change.