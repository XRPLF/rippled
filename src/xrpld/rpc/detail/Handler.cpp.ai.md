# `src/xrpld/rpc/detail/Handler.cpp`

## Role in the System

This file is the central dispatch registry for every RPC method the XRPL server exposes. Its job is to answer one question: given a method name and API version, which handler should run, and with what access constraints? Everything that lives here serves that purpose — a static table of ~70 handler entries, a versioned lookup mechanism, and two bridging utilities that let handlers evolve their interface over time.

## The Two Handler Styles

The file manages two distinct handler shapes that coexist in the table.

**Old-style handlers** are free functions with the signature `Json::Value do<Name>(RPC::JsonContext&)`. They compute a result and return it by value. Because every handler from before the interface evolved takes this form, the `byRef()` adapter function exists to shim them into the canonical `Status(JsonContext&, Json::Value&)` signature the dispatch layer expects. `byRef()` constructs a lambda that calls the old function, assigns the returned value to the output reference, then checks that the result is a JSON object. If it isn't — which the `LCOV_EXCL_START` marker declares unreachable under correct operation — `makeObjectValue()` wraps it defensively before returning. The `UNREACHABLE` macro there signals a programming contract violation rather than a user error.

**New-style handlers** are classes with static metadata (`name`, `minApiVer`, `maxApiVer`, `role`, `condition`) and two instance methods: `check()` to validate preconditions and `writeResult()` to populate the output. `LedgerHandler` and `VersionHandler` are the only current new-style handlers. The `handle<Object, HandlerImpl>()` template drives them: it asserts the incoming API version is within the handler's declared range, constructs an instance, calls `check()`, and either injects the returned error status or calls `writeResult()`. The `handlerFrom<HandlerImpl>()` factory then wraps this invocation in a `Handler` value-struct for table storage.

The reason two styles coexist rather than migrating everything to class form is pragmatic: old-style is simpler to write for methods that don't need version-specific behavior, while new-style provides richer compile-time enforcement and cleaner two-phase dispatch for more complex methods.

## The `HandlerTable` Singleton

`HandlerTable` is an internal class that builds and owns the live dispatch table. It is a `static`-local singleton — `instance()` returns a `const&` to the single object initialized on first call, guaranteed thread-safe by C++11. Once built, the table is immutable.

The backing store is `std::multimap<std::string, Handler>`. The multimap — rather than a plain map — is the key design decision: it allows multiple entries under the same method name, each covering a non-overlapping API version range. This enables a single method name to have entirely different implementations across API versions without routing logic spread across the handlers themselves. The lookup in `getHandler()` does an `equal_range()` on the name, then finds the entry whose `[minApiVer_, maxApiVer_]` bracket contains the requested version.

Version overlap is a fatal configuration error. `overlappingApiVersion()` scans same-named entries for range intersection using the standard interval-overlap test (`a.min <= b.max && a.max >= b.min`). If any overlap is found during construction, `LogicError()` is called immediately, crashing the server before it begins accepting requests. This makes conflicting handler registrations impossible to deploy silently. The `addHandler<T>()` private method, used during construction for `LedgerHandler` and `VersionHandler`, runs the same check with `static_assert` guards at compile time for the version bounds themselves.

## Version-Gated Lookup

`getHandler()` is the public entry point for the dispatch layer. It takes three parameters: the API version of the incoming request, a `betaEnabled` flag, and the method name.

The version gate comes first: if the requested version is below `apiMinimumSupportedVersion` or above the effective maximum (either `apiMaximumSupportedVersion` or `apiBetaVersion` depending on the flag), the method returns `nullptr` immediately regardless of whether a handler exists. This makes beta-only API methods invisible to non-beta nodes at the lookup level rather than requiring each handler to check the flag internally.

## Metadata Carried Per Handler

Each `Handler` struct records:
- `name_`: the JSON method name (e.g., `"account_info"`)
- `valueMethod_`: the callable with the canonical `Status(JsonContext&, Json::Value&)` signature
- `role_`: `Role::USER` or `Role::ADMIN`, consumed by the authorization layer
- `condition_`: a bitmask from the `Condition` enum indicating required network/ledger state
- `minApiVer_` / `maxApiVer_`: the API version bracket

The `Condition` enum values — `NO_CONDITION`, `NEEDS_NETWORK_CONNECTION`, `NEEDS_CURRENT_LEDGER`, `NEEDS_CLOSED_LEDGER` — are used by `conditionMet()` in `Handler.h` to check amendment blocking, UNL expiry, operating mode, and ledger age before dispatch. For example, `fee`, `owner_info`, `ledger_accept`, and `submit` all require `NEEDS_CURRENT_LEDGER`; `tx` and `ledger_cleaner` require `NEEDS_NETWORK_CONNECTION`. Several handlers are version-bounded at registration: `ledger_header` and `tx_history` carry explicit `{1, 1}` limits in their `handlerArray` entries, making them inaccessible to clients using API version 2 or later.

## Access Patterns

Two public functions expose the table: `getHandler()` for dispatch (returns a pointer into the immutable table or `nullptr`) and `getHandlerNames()` for introspection (returns a `std::set<char const*>` of raw string pointers into the table entries). The pointer-to-static-string approach for names avoids copying but requires that callers treat them as interned constants rather than independently managed strings.