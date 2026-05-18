# `src/libxrpl/protocol/Asset.cpp`

## Role in the System

`Asset.cpp` provides the implementation body for XRPL's unified asset abstraction layer. The XRPL protocol historically distinguished between two token categories — XRP (native) and IOU (issued) — both of which were modelled by the `Issue` type. With the introduction of Multi-Purpose Tokens (MPT), a second distinct representation, `MPTIssue`, entered the picture. Rather than propagate a two-track conditional through every ledger subsystem, the codebase introduced `Asset` as a single polymorphic handle that can hold either type without exposing the distinction at call sites.

This file is the out-of-line companion to `include/xrpl/protocol/Asset.h`. The header holds everything that can be inlined or templated; this `.cpp` contains the handful of methods whose implementations are non-trivial enough to keep out of the header, plus two free functions (`validJSONAsset`, `assetFromJson`) that gate JSON ingestion.

## The Variant Core and `std::visit` Dispatch

`Asset` stores a `std::variant<Issue, MPTIssue>` named `issue_`. Every method in this file delegates to the active alternative through `std::visit`, never branching on a type tag manually. This is intentional: it makes adding a third asset type a compile-error-driven process (the visitor won't compile until all alternatives are handled) rather than a silent runtime gap caused by a missed `if`-branch.

`getIssuer()`, `getText()`, and `setJson()` are all single-line `std::visit` calls that forward to the same-named method on whichever underlying type is active. Both `Issue` and `MPTIssue` expose this interface deliberately — `MPTIssue`'s comment in its header says it "adapts MPTID to provide the same interface as Issue," enabling this static polymorphism without a virtual dispatch table.

The `Asset::visit()` method in the header (not in the `.cpp`) wraps `detail::visit` from `Concepts.h`, which implements the *overloaded* pattern: multiple lambdas are combined into a single callable via `CombineVisitors : Ts...` with `using Ts::operator()...`. This gives call sites the ability to pass per-type lambdas inline without constructing a separate visitor struct.

## JSON Boundary: `validJSONAsset` and `assetFromJson`

These two free functions form the JSON ingestion gate. Their separation of concerns is deliberate:

- `validJSONAsset(Json::Value const&)` is a **pure predicate** — it returns `bool` and throws nothing. Its job is to enforce mutual exclusion: if `mpt_issuance_id` is present, neither `currency` nor `issuer` may appear, because those fields belong to the legacy `Issue` representation. If `mpt_issuance_id` is absent, `currency` must be present. This check is typically called before attempting to construct an `Asset`, letting callers reject invalid input with a clean error rather than relying on a constructor exception.

- `assetFromJson(Json::Value const&)` is the **constructor proxy** — it throws `std::runtime_error` (via the `Throw<>` helper from `xrpl/basics/contract.h`) if neither `currency` nor `mpt_issuance_id` is present, then dispatches to either `issueFromJson` or `mptIssueFromJson`. The dispatch is a simple `if (v.isMember(jss::currency))` test, with `currency` taking priority. Deeper field validation (format, consistency) is delegated to those two sub-parsers.

The two-function design avoids throwing from a predicate while still providing a single authoritative construction path. Code that already validated with `validJSONAsset` can call `assetFromJson` with confidence.

## Call-Operator Factory: `operator()(Number const&)`

```cpp
STAmount Asset::operator()(Number const& number) const
{
    return STAmount{*this, number};
}
```

This is a convenience factory: given an `Asset` and a `Number`, it produces a fully-typed `STAmount`. The intent is ergonomic — `myAsset(someQuantity)` reads like a constructor call and avoids repetitive `STAmount{asset, value}` syntax at call sites. No arithmetic happens here; it purely forwards into `STAmount`'s constructor.

## Ordering and Identity

The header defines comparison operators using `std::visit` with a two-argument lambda. `operator==` returns `false` immediately if the two active alternatives differ in type — an `Issue` can never equal an `MPTIssue`. `operator<=>` establishes a total order where `Issue` variants sort *after* `MPTIssue` variants when the types differ (`std::weak_ordering::greater` for `Issue` vs. `MPTIssue`), ensuring a deterministic ordering for sorted containers even across the type boundary.

`equalTokens()` is a weaker comparison that ignores issuer identity for `Issue` — it asks only whether two assets share the same currency code (for IOUs) or the same `MPTID` (for MPTs). This is used in contexts like offer matching where the token denomination matters but the specific issuer pathway does not.

## Implicit vs. Explicit Conversion Policy

The header comments document a deliberate asymmetry: conversions *to* `Asset` are implicit (any `Issue` or `MPTIssue` silently becomes an `Asset`), while conversions *from* `Asset` to a concrete type are explicit (callers must call `get<Issue>()` or `get<MPTIssue>()`, which throws `std::logic_error` on type mismatch). This makes `Asset` the widening type in the system — functions written to accept `Asset` work for all token kinds — while preventing accidental silent narrowing that would lose the type-safety the variant provides.