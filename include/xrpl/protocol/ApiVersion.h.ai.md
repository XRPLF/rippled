# `include/xrpl/protocol/ApiVersion.h`

## Role in the System

This header is the single source of truth for XRPL's RPC API versioning scheme. It defines the integer constants that govern which API surface a client can access, provides the JSON parsing and serialization utilities that enforce those version boundaries on every incoming request, and exposes compile-time iteration primitives that let the rest of the codebase safely generate version-aware code without duplication. Every RPC handler that changes behavior between versions, every subscription publisher that formats data differently per client, and every test that validates multi-version correctness traces back to the constants and templates defined here.

## Version Model

The versioning system is built on a small set of named compile-time constants inside `namespace xrpl::RPC`. Rather than raw integers, they are typed as `std::integral_constant<unsigned, N>` instances, which enables overload resolution and template specialisation to distinguish versions at compile time while still implicitly decaying to `unsigned` in arithmetic and comparison contexts.

The constants form a strict linear ordering that is enforced by `static_assert` at translation time:

- `apiInvalidVersion` (0) — a sentinel returned when parsing fails or a version is out of range.
- `apiMinimumSupportedVersion` (1) — the oldest version still accepted from network clients.
- `apiMaximumSupportedVersion` (2) — the newest stable version. Normal production requests are capped here.
- `apiBetaVersion` (3) — an experimental version gated behind the `[beta_rpc_api]` config flag.
- `apiMaximumValidVersion` — always equal to `apiBetaVersion`; the absolute ceiling for template range loops.
- `apiVersionIfUnspecified` (1) — the implicit version assigned when a request omits `api_version`. It is fixed at 1 because the design rule is that any request at version 2 or above must carry an explicit field; omitting it is treated as a version-1 request, not an error.
- `apiCommandLineVersion` (1) — the version used for command-line invocations, with a `TODO` comment noting it should eventually be bumped to 2.

The `static_assert` block at the bottom of the constant declarations is load-bearing, not decorative. It guarantees that invariants like `apiVersionIfUnspecified` lying within `[min, max]` and `apiBetaVersion >= apiMaximumSupportedVersion` hold whenever a developer adjusts these values. The companion test file `ApiVersion_test.cpp` validates the current actual values (e.g., min < 2, max ≥ 2, beta ≥ 3) so that a mistaken bump causes an immediately visible CI failure.

## `getAPIVersionNumber()`

This function is called at the RPC ingress point in `ServerHandler.cpp` on every incoming HTTP or WebSocket request. It inspects the incoming `Json::Value` for a top-level `api_version` field. If the field is absent the function returns `apiVersionIfUnspecified`. If the field is present but not an integer, or its integer value falls outside `[apiMinimumSupportedVersion, maxVersion]`, it returns `apiInvalidVersion`. Callers treat `apiInvalidVersion` as a signal to reject the request immediately with an appropriate error before any handler dispatch occurs.

The `betaEnabled` parameter reflects the per-server configuration flag `BETA_RPC_API`. When false, `maxVersion` is `apiMaximumSupportedVersion` (2); when true, `maxVersion` is `apiBetaVersion` (3). This is the only mechanism through which experimental API surface is accessible, and it keeps the feature completely invisible to clients connecting to a production node that has not opted in.

## `setVersion()`

`setVersion()` populates the `version` sub-object in an RPC response. Its behaviour diverges sharply based on the negotiated version because version 1 used a legacy semver-string format (`first`/`good`/`last` keys whose values are `"1.0.0"` strings), while version 2 and above switched to simple integers. This bifurcation is why the function exists at all rather than being inlined at the call site: it encapsulates a backwards-compatibility shim so that callers can remain version-agnostic. The v1 path constructs static `beast::SemanticVersion` objects to avoid repeated string parsing; the v2+ path emits `apiMinimumSupportedVersion.value` as `first` and either `apiBetaVersion` or `apiMaximumSupportedVersion` as `last` depending on the beta flag. The handler in `src/xrpld/rpc/handlers/server_info/Version.h` is the primary consumer.

## Compile-Time Version Iteration: `forApiVersions` and `forAllApiVersions`

These two function templates are the most architecturally interesting part of the file. Their purpose is to call a callable object once for each API version in a range, passing the version as an `std::integral_constant<unsigned int, N>` so that the callable can use the version as a template parameter, enabling compile-time branching without virtual dispatch or runtime switches.

The implementation expands the range `[minVer, maxVer]` into a parameter pack using `std::make_index_sequence<maxVer + 1 - minVer>` and then folds over it with a comma-expression. Each element of the fold instantiates the callable with `std::integral_constant<unsigned int, minVer + offset>`. Because every instantiation carries a distinct type, the compiler produces a separate code path per version. This means that if a handler uses `if constexpr (Version >= 2)` in its lambda body, only the v2+ instantiations include that branch — dead branches are eliminated at compile time.

The C++20 `requires` clause on `forApiVersions` enforces three constraints statically: the range must be non-empty, `minVer` must be at least `apiMinimumSupportedVersion`, and `maxVer` must be at most `apiMaximumValidVersion`. If a caller tries to iterate outside the known valid range the constraint fails to compile rather than producing a runtime out-of-bounds error.

`forAllApiVersions` is a thin wrapper that fixes the range to `[apiMinimumSupportedVersion, apiMaximumValidVersion]`, which today spans versions 1 through 3. It is the standard way for test cases to run a scenario against every version and for publishers to prepare data for all potential subscribers. For example, `NetworkOPs.cpp` uses it to build a `MultiApiJson` fan-out when notifying subscribers of new transactions, calling `insertDeliverMax` only for the versions where that field is defined.

## Relationship to `MultiApiJson`

`MultiApiJson` (defined in `MultiApiJson.h`) depends directly on `ApiVersion.h`. It is parameterised on `[RPC::apiMinimumSupportedVersion, RPC::apiMaximumValidVersion]` and stores an `std::array` of `Json::Value` objects, one per version. The `forAllApiVersions` loop is the standard way to populate all slots of a `MultiApiJson` in a single pass, with each iteration receiving a typed version constant that the lambda can use to decide what fields to emit. The version constants in `ApiVersion.h` are therefore not just configuration — they dictate the size and index mapping of every `MultiApiJson` array in the system, so changing them automatically adjusts every data structure that stores per-version output.