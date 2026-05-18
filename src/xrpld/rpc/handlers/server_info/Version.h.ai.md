# `Version.h` — RPC Version Discovery Handler

## Role and Purpose

`Version.h` defines `VersionHandler`, which implements the `version` RPC endpoint. This endpoint exists so that API clients can discover which API versions a running `rippled` node supports before making substantive requests. It is one of only two "new-style" class-based RPC handlers in the codebase; the other is `LedgerHandler`. All other RPC commands in `rippled` are registered as bare function pointers in the legacy `handlerArray` table in `Handler.cpp`. `VersionHandler` and `LedgerHandler` are registered separately via the `addHandler<T>()` template during `HandlerTable` construction.

## Handler Protocol

The new-style handler contract is simple: a class must expose a constructor taking `JsonContext&`, a `check()` method, a `writeResult(Json::Value&)` method, and a set of `static constexpr` metadata fields. The `handlerFrom<T>()` template function in `Handler.cpp` bridges this class shape into a `Handler` struct by wrapping it in the `handle<>` free function template, which instantiates the class, calls `check()`, and either injects an error or calls `writeResult()` depending on the result. The dispatch infrastructure therefore treats class-based and function-based handlers identically at runtime.

## What the Handler Does

The constructor captures two fields from the incoming `JsonContext`: the already-resolved `apiVersion` (an integer version number parsed from the `api_version` field of the request) and the `betaEnabled` flag from the node's config (`BETA_RPC_API`). These are the only context fields the handler needs.

`check()` unconditionally returns `Status::OK`. There is no error condition for this endpoint — a client asking "what versions do you support?" is always a valid question regardless of the node's synchronization state or ledger availability.

`writeResult()` delegates entirely to `setVersion()` defined in `ApiVersion.h`. That function's behavior branches on whether the caller is using API version 1 (the legacy default). For version-1 callers, it emits semantic version strings (`first`, `good`, `last`) all fixed at `"1.0.0"` — a compatibility shim that mirrors the old format clients expected before numbered API versions existed. For version-2 and higher callers, it emits numeric `first`/`last` bounds: `first` is always `apiMinimumSupportedVersion` (1), and `last` is either `apiMaximumSupportedVersion` (2) or `apiBetaVersion` (3) depending on whether `betaEnabled` is true.

## Version Range and the Beta Boundary

The handler's static `maxApiVer` is set to `RPC::apiMaximumValidVersion`, which equals `apiBetaVersion` (currently 3). This is deliberately wider than the `apiMaximumSupportedVersion` (2) that most handlers cap at. The reason is structural: a client running against a beta-enabled node needs to be able to reach the `version` endpoint at API version 3 to discover that version 3 is available. If `maxApiVer` were capped at 2, the `HandlerTable` lookup at version 3 would return `nullptr` and the client could never query capabilities at that version level. Setting `maxApiVer = apiMaximumValidVersion` ensures the discovery endpoint is always reachable across the full valid range.

The `addHandler<T>()` template enforces this correctness contract at compile time with three `static_assert` checks:

```cpp
static_assert(HandlerImpl::minApiVer <= HandlerImpl::maxApiVer);
static_assert(HandlerImpl::maxApiVer <= RPC::apiMaximumValidVersion);
static_assert(RPC::apiMinimumSupportedVersion <= HandlerImpl::minApiVer);
```

Any handler with out-of-range version bounds will fail to compile.

## Access Control

`role = Role::USER` and `condition = NO_CONDITION`. The `USER` role means no admin credentials are required — version discovery is public. `NO_CONDITION` means the network synchronization and ledger availability checks in `conditionMet()` are bypassed entirely. This is appropriate: a node that is still syncing or amendment-blocked can still truthfully describe what API versions it supports.