# `GatewayBalances.cpp` — `gateway_balances` RPC Handler

## Role and Purpose

This file implements `doGatewayBalances`, the sole handler for the `gateway_balances` RPC command. The command exists to give XRPL gateways (token issuers) a complete financial snapshot: how much they owe customers in each currency, how much their hot wallets currently hold, any unusual positive balances (assets the gateway itself holds), obligations they have frozen, and funds locked in escrow. It is fundamentally an account-centric audit tool for the common XRPL issuer architecture where a cold wallet issues tokens and distributes them through hot wallets.

## Gateway Architecture Context

XRPL gateways operate with a cold/hot wallet security model. The cold wallet (identified in the `account` field) is the actual issuer and signs infrequently; hot wallets hold issued balances for operational distribution and can be compromised without exposing the issuer's key. This handler is designed around that model. The caller enumerates their hot wallets via the `hotwallet` parameter, and the handler classifies every trust line on the cold wallet accordingly.

## Input Validation

Validation proceeds in layers. First, `RPC::lookupLedger` resolves the requested ledger and returns early if it cannot be found. The account identifier accepts either `account` or `ident` as the field name — `ident` is a legacy alias. `parseBase58<AccountID>` rejects malformed addresses before any ledger access. Account existence is only enforced for API version 2 and above; in v1, querying a nonexistent account returns an empty result rather than an error, preserving backward compatibility.

The `hotwallet` field is polymorphic by design: it accepts a single string, an array of strings, or null (treated as an empty array). Each entry is parsed through the same `parseBase58<AccountID>` path. If any entry is invalid, the error code differs by API version — `rpcINVALID_HOTWALLET` in v1, `rpcINVALID_PARAMS` in v2+. This was a deliberate correction in error taxonomy: `rpcINVALID_HOTWALLET` was a domain-specific code that did not fit cleanly into the general parameter-error scheme used by v2.

After validation the handler immediately sets `context.loadType = Resource::feeHeavyBurdenRPC`, declaring this call expensive. Traversing the owner directory is O(n) in the number of trust lines, and gateways can have thousands of them.

## Trust Line Traversal and Classification

`forEachItem(*ledger, accountID, ...)` walks the cold wallet's owner directory. Every `SLE` in the directory is examined:

**Escrow handling**: If the entry is an `ltESCROW`, its `sfAmount` field is accumulated into the `locked` map, keyed by currency. MPT-denominated escrows are skipped explicitly (`escrow.holds<MPTIssue>()` guard) — the locked totals only reflect classic IOU/XRP escrows. This is a deliberate scope limitation since the `gateway_balances` model was designed before MPTs existed.

**Trust line classification**: For non-escrow entries, `PathFindTrustLine::makeItem(accountID, sle)` attempts to interpret the SLE as a trust line from the cold wallet's perspective. The `PathFindTrustLine` class (from `TrustLine.h`) normalises the low/high account asymmetry of the `RippleState` ledger object into a consistent view where `getBalance()` and `getAccountIDPeer()` are always expressed relative to the cold wallet.

With a normalised trust line available, the handler classifies it into one of four buckets based on the balance sign and peer identity:

1. **Hot wallet peer** — if the peer is in `hotWallets`, the balance is negated and placed in `hotBalances[peer]`. Negation is necessary because a negative balance on the cold wallet side means tokens are held by the counterparty.
2. **Positive balance (asset)** — a positive balance means someone owes the gateway, which is unusual. These go into `assets[peer]`.
3. **Frozen obligation** — `rs->getFreeze()` checks whether the cold wallet has frozen this specific trust line. The (negated) balance goes into `frozenBalances[peer]`.
4. **Normal obligation** — the common case: a customer holds gateway-issued tokens. These are aggregated by currency into `sums`, computing the total outstanding liabilities. Each `sums[currency]` entry is a running total across all customer trust lines in that currency.

## Overflow Handling in Accumulation

Both `sums` (obligations) and `locked` (escrow totals) use arithmetic that can overflow `STAmount`'s mantissa range. The code wraps the `+=` / `-=` operations in `try-catch (std::runtime_error)` and, on overflow, clamps to `STAmount::cMaxValue`/`STAmount::cMaxOffset`. The comment in the code is honest about the implication: very large sums are approximations anyway. This is a graceful degradation rather than a hard error — a gateway with astronomically large obligations gets a capped but still-useful report rather than a failed RPC call.

The first assignment into a zero-initialised bucket (`bal == beast::zero`) is done by direct assignment rather than addition. This is required because `STAmount` uses the issue/currency fields of the first assigned value to tag the bucket; addition against a default-constructed zero would not carry the correct currency code.

## Output Structure

The response contains up to five keys, each omitted if empty:

- `obligations` — a flat object mapping currency code to aggregate total, covering all non-hot, non-frozen customer liabilities.
- `balances` — a per-hot-wallet object, each value being an array of `{currency, value}` pairs.
- `frozen_balances` — same structure as `balances` but for frozen obligations.
- `assets` — same structure for unusual positive positions.
- `locked` — a flat object like `obligations`, mapping currency to total escrowed amount.

The `populateResult` lambda deduplicates the three per-account maps into a single output pattern, avoiding repetition in the serialisation code. Because only non-empty maps generate output keys, a gateway with no assets gets no `assets` key at all, keeping the response compact.