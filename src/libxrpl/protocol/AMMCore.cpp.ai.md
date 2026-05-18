# `src/libxrpl/protocol/AMMCore.cpp`

## Role in the System

This file is the shared validation and utility foundation for all AMM (Automated Market Maker) transaction logic in the XRPL. Every AMM transaction handler — `AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMBid`, `AMMVote`, and `AMMDelete` — calls into these functions during their `preflight` phase to perform stateless input validation before touching ledger state. The file also handles LP token identity derivation and auction slot time arithmetic.

The functions are deliberately low-level: no ledger access, no state mutation, pure computation over in-memory data. This placement in `libxrpl/protocol` (rather than `xrpld`) makes them available to both the server and external client libraries.

## LP Token Currency Derivation

`ammLPTCurrency()` computes a deterministic `Currency` identifier for the liquidity provider token of an asset pair. The algorithm has three notable design choices.

First, it uses `std::minmax` on the two `Asset` arguments before hashing. This canonical ordering ensures that `ammLPTCurrency(A, B)` and `ammLPTCurrency(B, A)` produce the same result — a necessary property since an AMM pool is unordered. The `Asset` comparison operators in `Asset.h` provide a consistent total order across `Issue` and `MPTIssue` variants (with `Issue` types ordered greater than `MPTIssue` types).

Second, the hash input is type-dispatched via a `std::visit` lambda using C++20 template parameters (`auto&&` with `if constexpr`). For a classic `Issue` the hash input is the `Currency` field; for an `MPTIssue` it is the `MPTID` (a 192-bit identifier). This means the same function handles both legacy token types and the newer Multi-Purpose Token standard transparently.

Third, the resulting currency uses the prefix byte `0x03`. Standard XRPL IOU currencies occupy bytes where the first byte is `0x00`; XRP is the all-zeros special case; the `0x03` prefix is reserved specifically for AMM LP tokens. This allows any participant to identify an LP token currency just by inspecting the first byte, without needing to look up the associated AMM account.

`ammLPTIssue()` is a thin wrapper that bundles the computed currency with the AMM's account ID to form a complete `Issue`.

## Asset Validation Hierarchy

The three `invalid*` functions form a composable validation stack that returns `NotTEC` — a sub-type of TER restricted to `tem*` malformed-transaction codes. Returning `NotTEC` (rather than plain `TER`) documents at the type level that these checks can only fail due to structural malformedness, never due to transient ledger state.

`invalidAMMAsset()` is the atomic validator. It uses `Asset::visit()` to dispatch separately on `MPTIssue` and `Issue`:

- For `MPTIssue`: rejects a zero issuer (`temBAD_MPT`), which would denote an uninitialised or invalid MPT issuance.
- For `Issue`: rejects the sentinel `badCurrency()` value (`temBAD_CURRENCY`) and rejects any `Issue` that claims XRP but carries a non-zero issuer (`temBAD_ISSUER`) — a structurally impossible combination since XRP has no issuer on the XRPL.

The optional `pair` argument adds a membership check: if the caller supplies the known pool's asset pair, the function also verifies the asset belongs to that pool (`temBAD_AMM_TOKENS`). This single parameter makes `invalidAMMAsset()` useful both as a general format check (no pair) and as a context-sensitive pool membership check (with pair).

`invalidAMMAssetPair()` adds the distinctness constraint — the two assets must not be equal — then delegates each individual asset to `invalidAMMAsset`. Equality between assets of different types is always false (the `Asset::operator==` falls through to `false` on type mismatch in `std::visit`), so this naturally prevents a same-type self-pairing.

`invalidAMMAmount()` validates an `STAmount` by first extracting its asset and passing it through `invalidAMMAsset`, then separately checking the numeric value. The `validZero` flag deserves attention: most AMM operations disallow zero amounts (depositing zero liquidity is meaningless), but the flag is set `true` in contexts like specifying a minimum bid of zero (`AMMBid`), where zero is a valid sentinel meaning "no minimum."

## Auction Slot Time Arithmetic

`ammAuctionTimeSlot()` maps a ledger close timestamp to a slot index within the 24-hour auction window, returning `std::nullopt` if the timestamp falls outside the current slot's window.

The auction slot is parameterised by the constants defined in `AMMCore.h`: `TOTAL_TIME_SLOT_SECS = 86400` (one day), `AUCTION_SLOT_TIME_INTERVALS = 20`, giving `AUCTION_SLOT_INTERVAL_DURATION = 4320` seconds (72 minutes) per sub-interval. The slot starts at `expiration - TOTAL_TIME_SLOT_SECS` and ends at `expiration`. Dividing the elapsed seconds within the window by `AUCTION_SLOT_INTERVAL_DURATION` yields the interval index (0–19).

The guard `expiration >= TOTAL_TIME_SLOT_SECS` is a defensive sanity check accompanied by an `XRPL_ASSERT`. The code comment acknowledges this should be structurally impossible, but the check prevents underflow on the subtraction `expiration - TOTAL_TIME_SLOT_SECS` if somehow a corrupted ledger object were processed.

## Feature Gate

`ammEnabled()` gates all AMM functionality behind two amendments: `featureAMM` (the primary AMM feature flag) and `fixUniversalNumber`. The second requirement is non-obvious. AMM math throughout the codebase relies on the `Number` type from `xrpl/basics/Number.h`, which provides unified arithmetic across integer and floating-point domains. The `fixUniversalNumber` amendment corrects edge cases in that type's behavior. Tying `ammEnabled()` to both amendments means AMM transactions are unavailable unless the numeric foundation is also sound, preventing subtle calculation bugs on ledgers that enabled `featureAMM` before `fixUniversalNumber` was applied.