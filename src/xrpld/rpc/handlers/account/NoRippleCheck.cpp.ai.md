# `NoRippleCheck.cpp` — `noripple_check` RPC Handler

## Role in the System

This file implements the `doNoRippleCheck` RPC handler, which powers the `noripple_check` command on the XRP Ledger. The command exists to help both gateway operators and ordinary users detect misconfigured trust line No Ripple flags — a common source of unintended payment routing behavior. Rather than just reporting problems, the handler can optionally generate fully-formed, ready-to-submit transaction templates that correct each issue.

The file lives alongside other account-scoped RPC handlers (`AccountLines.cpp`, `AccountObjects.cpp`, etc.) and follows the same structural contract: a single exported `do*` function receiving an `RPC::JsonContext` and returning a `Json::Value`.

## The No Ripple Model

The No Ripple flag controls whether the XRPL's path-finding engine may "ripple" through an account's trust lines when routing a payment. The correct configuration depends on the account's role:

- **Gateways (issuers)** should enable the account-level `lsfDefaultRipple` flag, which makes newly created trust lines ripple by default. They must also ensure no individual trust line has `lsfLowNoRipple` or `lsfHighNoRipple` set on their side, as this would block payments from moving through their issued currency.
- **Users** should have `lsfDefaultRipple` cleared and should set `NoRipple` on each individual trust line. Without this, a user's balances can be silently routed through by third-party payments, which most users do not intend.

The handler encodes these invariants directly: the `roleGateway` boolean controls which branch of logic applies throughout.

## Input Validation

Validation is layered. The handler first performs explicit field checks: presence of `account` and `role`, type assertion on `account`, and an allowlist check that `role` is either `"gateway"` or `"user"`. The `limit` parameter is delegated to `readLimitField` with the `RPC::Tuning::noRippleCheck` range (minimum 10, default 300, maximum 400). Ledger resolution is handled by `RPC::lookupLedger`, which abstracts away `ledger_hash` and `ledger_index` parsing.

A notable versioning quirk: the `transactions` field historically accepted any truthy value due to `Json::Value::asBool()` being permissive. Starting with API version 2, the handler explicitly rejects non-boolean values with `isBool()`. This is a backward-compatibility boundary explicitly noted in an inline comment linking to the public documentation.

## Trust Line Traversal

The core analysis uses `forEachItemAfter` to walk the account's owner directory, which is the ledger structure holding all objects owned by an account. The callback filters for `ltRIPPLE_STATE` objects — the shared bilateral trust line ledger entries.

Each `ltRIPPLE_STATE` stores both sides' limits as `sfLowLimit` and `sfHighLimit`, with "low" and "high" determined by the numeric order of the two account IDs. The handler identifies which side corresponds to the queried account via:

```cpp
bool const bLow = accountID == ownedItem->getFieldAmount(sfLowLimit).getIssuer();
```

It then reads the appropriate directional flag (`lsfLowNoRipple` if `bLow`, else `lsfHighNoRipple`). This is the key to correctly interpreting the shared trust line object from one account's perspective.

The callback returns `true` on every item processed (triggering the count toward `limit`), meaning the `limit` parameter bounds the number of trust lines inspected rather than just the number of problems reported.

## Transaction Template Generation

The `fillTransaction()` helper populates the three fields common to every generated transaction: `Sequence` (taken from the account's current ledger sequence and post-incremented to handle multiple fixes in one response), `Account`, and `Fee`. The fee is not a hardcoded value — it is computed by `scaleFeeLoad` from the current base fee and the server's active fee track, giving the client a fee estimate calibrated to present network load.

For the account-level `DefaultRipple` problem, the handler emits an `AccountSet` transaction with `SetFlag = 8` (the flag number for `asfDefaultRipple`). For each misconfigured trust line, it emits a `TrustSet` transaction. Building the `LimitAmount` for `TrustSet` requires care: the amount must express the queried account's own trust limit, but the `Issue` account field inside `STAmount` must be set to the peer account (the issuer from the queried account's perspective). The handler reconstructs this from the opposing side's `sfHighLimit`/`sfLowLimit`:

```cpp
STAmount limitAmount(ownedItem->getFieldAmount(bLow ? sfLowLimit : sfHighLimit));
limitAmount.get<Issue>().account = peer;
```

The `Flags` field is set to `tfClearNoRipple` or `tfSetNoRipple` depending on whether the existing line has the flag set and the role requires it cleared or established.

## The `dummy` Variable Pattern

When `transactions` is `false`, the code still calls `jvTransactions.append(...)` inside the trust line loop. Rather than branching on `transactions` at every append site, the handler assigns `jvTransactions` to either a real array inside `result` or to a local `dummy` Json::Value. Appending to `dummy` is a no-op that is simply discarded. The `NOLINT(misc-const-correctness)` annotation on `dummy` is there because a linter would suggest marking it `const`, but it is mutated by the appends — the annotation documents this intentional design.

## Failure Modes

`doNoRippleCheck` returns early with distinct errors for each validation failure. After `lookupLedger`, the account's `SLE` (State Ledger Entry) is fetched and checked for existence; a missing account returns `rpcACT_NOT_FOUND`. A malformed base58 account string returns `rpcACT_MALFORMED` injected into the partial result (to preserve any ledger info already populated by `lookupLedger`). The `problems` array is always present in a successful response, even if empty.