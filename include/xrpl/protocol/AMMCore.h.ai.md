# `include/xrpl/protocol/AMMCore.h`

This header is the shared kernel for the XRPL Automated Market Maker (AMM) feature. It centralises all protocol-level constants, the deterministic derivation of Liquidity Provider Token (LPT) identities, input-validation helpers, and the inline fee-conversion utilities that the constant-product swap formula depends on. Every AMM transactor (`AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMBid`, `AMMVote`) and the higher-level ledger helpers in `AMMHelpers.h` pull from this single file, making it the authoritative definition of how AMM numeric parameters are encoded and checked.

## Fee Encoding

Trading fees are stored as integers in the range `[0, 1000]`, where 1 unit equals 1/10 of a basis point (0.001%) and the maximum `TRADING_FEE_THRESHOLD = 1000` corresponds to exactly 1%. This integer-per-tenth-of-bps encoding avoids floating point in the ledger while still providing fine-grained control.

Three inline helpers translate that integer into the `Number` type used by AMM arithmetic:

- `getFee(tfee)` divides by `AUCTION_SLOT_FEE_SCALE_FACTOR` (100,000) to produce the fee fraction `f`.
- `feeMult(tfee)` returns `1 - f`, the standard swap multiplier applied when the full fee is charged.
- `feeMultHalf(tfee)` returns `1 - f/2`, used during single-asset deposits where only half the implied fee is deducted (see `AMMDeposit.cpp`, where `f1 = feeMult` and `f2 = feeMultHalf / f1` are combined in the constant-product formula).

All three are `inline` and operate directly on the `Number` type (defined in `xrpl/basics/Number.h`), keeping the translation cost at the call site while preserving the high-precision arithmetic that `fixUniversalNumber` provides.

## Auction Slot Constants

An AMM auction slot gives its holder a discounted trading fee for 24 hours. That window (`TOTAL_TIME_SLOT_SECS = 86400`) is divided into `AUCTION_SLOT_TIME_INTERVALS = 20` equal intervals of `AUCTION_SLOT_INTERVAL_DURATION = 4320` seconds (72 minutes). The slot index (0–19) determines how much of the bid price is refunded to the outgoing slot holder when a new bidder takes over.

Other auction-related constants:
- `AUCTION_SLOT_MAX_AUTH_ACCOUNTS = 4`: the maximum number of accounts a slot holder may authorise to trade at the discounted fee.
- `AUCTION_SLOT_DISCOUNTED_FEE_FRACTION = 10`: the slot holder's effective fee is `tradingFee / 10`.
- `AUCTION_SLOT_MIN_FEE_FRACTION = 25`: minimum bid is `lptAMMBalance × tradingFee / 25`.

`ammAuctionTimeSlot(current, auctionSlot)` derives the slot index from the ledger's current time and the `sfExpiration` field stored in the slot object. It subtracts the 24-hour window from the expiration to find the slot start, then integer-divides the elapsed seconds by the interval duration. It returns `std::nullopt` when the current time falls outside the active window, signalling that the slot has expired or has not yet started. An `XRPL_ASSERT` guards against an impossible expiration value smaller than `TOTAL_TIME_SLOT_SECS`, providing a diagnostic checkpoint without altering control flow in production builds.

## Vote Weight Constants

Fee-governance voting uses `VOTE_MAX_SLOTS = 8` vote records and `VOTE_WEIGHT_SCALE_FACTOR = 100000` to represent each LP's proportional weight as a scaled integer, avoiding division until the weighted average is computed.

## LP Token Identity

`ammLPTCurrency` derives a deterministic 20-byte `Currency` code for the pool's LP token:

1. The two assets are sorted via `std::minmax` to eliminate ordering ambiguity — the same pair always produces the same currency regardless of which asset was passed as `asset1` or `asset2`.
2. A `sha512Half` hash is computed over the canonical token identifiers (the `Currency` field for IOU/XRP assets, or the `MPTID` for MPT assets).
3. The first byte is set to the sentinel `0x03` (the AMM currency code), and the following 19 bytes are filled from the hash.

This construction guarantees uniqueness within the ledger: different asset pairs produce different currencies, the byte prefix distinguishes LPT currencies from normal IOUs (`0x00`) or XRP, and the canonical sort makes the derivation purely deterministic. `ammLPTIssue` wraps the currency with the AMM's `AccountID` to form the full `Issue` that `STAmount` operations require.

## Input Validation

The three `invalidAMM*` functions follow the XRPL convention of returning `NotTEC` — a type restricted to `tesSUCCESS` or `tem*` (malformed transaction) error codes, as distinct from execution-phase `tec*` failures. This signals that the checks are preconditions evaluated during preflight, before any ledger state is modified.

- `invalidAMMAsset` rejects MPT assets with a zero issuer (`temBAD_MPT`), rejects XRP with a non-zero issuer (`temBAD_ISSUER`), rejects bad currency codes (`temBAD_CURRENCY`), and optionally confirms the asset is one of the two expected pool assets (`temBAD_AMM_TOKENS`).
- `invalidAMMAssetPair` composes two `invalidAMMAsset` calls and additionally rejects a pair where both sides are the same asset.
- `invalidAMMAmount` delegates asset validation and then checks the `STAmount` value is non-negative (and non-zero unless `validZero` is explicitly permitted).

The `pair` parameter, typed as `std::optional<std::pair<Asset, Asset>>`, threads a known-good pair through validation when callers need to confirm that user-supplied assets actually refer to the AMM pool they are operating on.

## Feature Gating

`ammEnabled(Rules const&)` requires both `featureAMM` and `fixUniversalNumber` to be active on the network. The second amendment is not incidental: AMM math involves intermediate results that overflow or lose precision with the older fixed-point arithmetic. Requiring `fixUniversalNumber` prevents AMM transactions from being processed on networks that have not yet adopted the corrected numeric library, making the dependency explicit and machine-checked rather than relying on deployment order alone.