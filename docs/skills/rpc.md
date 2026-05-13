# RPC

JSON-RPC over HTTP/WebSocket and gRPC. Central handler table dispatches by method name + API version. Roles: ADMIN, USER, IDENTIFIED, PROXY, FORBID.

## Key Invariants

- Handler table in `Handler.cpp`: each entry = `{name, function, role, condition, minApiVer, maxApiVer}`
- `conditionMet` checks server state (e.g., `NEEDS_CURRENT_LEDGER`) before invoking handler
- API v2.0+ errors: structured objects with `status`, `code`, `message`; earlier: flat fields in response
- Sensitive fields (`passphrase`, `secret`, `seed`, `seed_hex`) are masked in error responses
- Batch requests: `"method": "batch"` with `"params"` array; each sub-request processed independently

## Common Bug Patterns

- New handler without entry in `Handler.cpp` static array = handler silently unreachable
- Wrong `role_` on handler: USER-level handler with admin-only data leaks; ADMIN handler accessible to users = security hole
- `conditionMet` returning false causes a generic error; ensure new conditions are documented
- Resource charging: each request gets a fee via `Resource::Consumer`; missing charge allows DoS
- `maxRequestSize` (RPC::Tuning) rejection happens before JSON parsing; oversized requests get no error detail

## Adding New RPC Handler

1. Declare in `Handlers.h`: `Json::Value doMyCommand(RPC::JsonContext&);`
2. Implement in new file under `src/xrpld/rpc/handlers/`
3. Register in `Handler.cpp` static array with role, condition, version range
4. For gRPC: define in `xrp_ledger.proto`, add `CallData` in `GRPCServerImpl::setupListeners()`

## Subscriptions

- WebSocket clients can subscribe to: `server`, `ledger`, `book_changes`, `transactions`, `validations`, `manifests`, `peer_status` (admin), `consensus`
- `WSInfoSub` delivers events via weak pointer to `WSSession`; dead sessions are automatically cleaned up
- `RPCSub` delivers to remote URL endpoints with auth and SSL support

## Key Patterns

### Handler Table Registration
```cpp
// In Handler.cpp handlerArray[] — REQUIRED for every new handler:
{"my_command", byRef(&doMyCommand), Role::USER, NO_CONDITION},
// role MUST match security requirements:
//   Role::ADMIN for internal-only, Role::USER for public API
// condition: NEEDS_CURRENT_LEDGER, NEEDS_NETWORK_CONNECTION, or NO_CONDITION
```

### Version-Ranged Handler
```cpp
// New-style handler with API version range
template <> Handler handlerFrom<MyCommandHandler>()
{   return {MyCommandHandler::name, &handle<Json::Value, MyCommandHandler>,
        MyCommandHandler::role, MyCommandHandler::condition,
        MyCommandHandler::minApiVer, MyCommandHandler::maxApiVer};
}
```

## Key Files

- `src/xrpld/rpc/handlers/Handlers.h` - authoritative handler list
- `src/xrpld/rpc/detail/Handler.cpp` - handler table and dispatch
- `src/xrpld/rpc/detail/RPCHandler.cpp` - request processing pipeline
- `src/xrpld/rpc/detail/ServerHandler.cpp` - HTTP/WS entry points
- `include/xrpl/protocol/ErrorCodes.h` - error code definitions
