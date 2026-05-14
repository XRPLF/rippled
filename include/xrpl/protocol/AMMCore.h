/** @file
 *  Protocol-level constants, LP-token identity derivation, input-validation
 *  helpers, and fee-conversion utilities for the XRP Ledger Automated Market
 *  Maker (AMM) feature.
 *
 *  Every AMM transactor (`AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMBid`,
 *  `AMMVote`) and `AMMHelpers.h` depend on this header as the single
 *  authoritative source for numeric parameter encoding and preflight checks.
 */

#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Maximum trading fee, in tenths of a basis point.
 *
 *  Fee integers are in the range `[0, kTRADING_FEE_THRESHOLD]` where
 *  1 unit = 0.001% (1/10 bps) and 1000 = 1%.
 */
std::uint16_t constexpr kTRADING_FEE_THRESHOLD = 1000;  // 1%

// --- Auction slot parameters ---

/** Duration of a single auction slot window, in seconds (24 hours). */
std::uint32_t constexpr kTOTAL_TIME_SLOT_SECS = 24 * 3600;

/** Number of equal time intervals the 24-hour auction window is divided into.
 *
 *  The slot index (0–19) determines how much of the bid price is refunded to
 *  the outgoing holder when a new bidder takes over mid-window.
 */
std::uint16_t constexpr kAUCTION_SLOT_TIME_INTERVALS = 20;

/** Maximum number of additional accounts a slot holder may authorise to trade
 *  at the discounted fee.
 */
std::uint16_t constexpr kAUCTION_SLOT_MAX_AUTH_ACCOUNTS = 4;

/** Divisor used to convert a fee integer to the fee fraction `f`.
 *
 *  `f = tfee / kAUCTION_SLOT_FEE_SCALE_FACTOR`.  Chosen so that
 *  `kTRADING_FEE_THRESHOLD / kAUCTION_SLOT_FEE_SCALE_FACTOR == 0.01` (1%).
 */
std::uint32_t constexpr kAUCTION_SLOT_FEE_SCALE_FACTOR = 100000;

/** Denominator for the slot holder's discounted fee.
 *
 *  The effective fee for a slot holder is `tradingFee / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION`.
 */
std::uint32_t constexpr kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION = 10;

/** Denominator used to compute the minimum bid price for the auction slot.
 *
 *  Minimum bid = `lptAMMBalance × tradingFee / kAUCTION_SLOT_MIN_FEE_FRACTION`.
 */
std::uint32_t constexpr kAUCTION_SLOT_MIN_FEE_FRACTION = 25;

/** Duration of one auction slot interval, in seconds (72 minutes).
 *
 *  Derived as `kTOTAL_TIME_SLOT_SECS / kAUCTION_SLOT_TIME_INTERVALS`.
 */
std::uint32_t constexpr kAUCTION_SLOT_INTERVAL_DURATION =
    kTOTAL_TIME_SLOT_SECS / kAUCTION_SLOT_TIME_INTERVALS;

// --- Fee-governance vote parameters ---

/** Maximum number of simultaneous fee-vote records in an AMM object. */
std::uint16_t constexpr kVOTE_MAX_SLOTS = 8;

/** Scale factor for LP vote weights.
 *
 *  Each LP's proportional vote weight is stored as an integer in
 *  `[0, kVOTE_WEIGHT_SCALE_FACTOR]`, avoiding division until the
 *  weighted-average fee is computed.
 */
std::uint32_t constexpr kVOTE_WEIGHT_SCALE_FACTOR = 100000;

class STObject;
class STAmount;
class Rules;

/** Derive the deterministic LP token `Currency` code for an asset pair.
 *
 *  The two assets are sorted canonically before hashing, so
 *  `ammLPTCurrency(a, b) == ammLPTCurrency(b, a)` for any asset pair.
 *  The resulting 20-byte currency has `0x03` as its first byte (the AMM
 *  currency sentinel), followed by 19 bytes taken from
 *  `sha512Half(canonicalId(min), canonicalId(max))`.  For IOU/XRP assets the
 *  canonical identifier is the `Currency` field; for MPT assets it is the
 *  `MPTID`.
 *
 *  @param asset1  One of the two pool assets.
 *  @param asset2  The other pool asset.
 *  @return A `Currency` value that uniquely identifies the LP token for this
 *      pair on the ledger and is distinct from any normal IOU or XRP currency.
 */
Currency
ammLPTCurrency(Asset const& asset1, Asset const& asset2);

/** Construct the full LP token `Issue` (currency + issuer) for an asset pair.
 *
 *  Combines the deterministic currency from `ammLPTCurrency` with the AMM
 *  account's `AccountID` to produce the `Issue` that `STAmount` operations
 *  require.
 *
 *  @param asset1       One of the two pool assets.
 *  @param asset2       The other pool asset.
 *  @param ammAccountID The `AccountID` of the AMM ledger object.
 *  @return An `Issue` identifying the LP token for this AMM pool.
 */
Issue
ammLPTIssue(Asset const& asset1, Asset const& asset2, AccountID const& ammAccountID);

/** Validate an `STAmount` for use in an AMM transaction (preflight check).
 *
 *  Delegates asset-level validation to `invalidAMMAsset`, then additionally
 *  rejects negative values and, unless `validZero` is true, zero values.
 *
 *  @param amount    The amount to validate.
 *  @param pair      When provided, the amount's asset must match one of the
 *      two assets in the pair; otherwise `temBAD_AMM_TOKENS` is returned.
 *  @param validZero If `false` (the default), a zero amount is rejected with
 *      `temBAD_AMOUNT`.
 *  @return `tesSUCCESS` if valid; a `tem*` error code otherwise.
 */
NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt,
    bool validZero = false);

/** Validate a single asset for use in an AMM transaction (preflight check).
 *
 *  - MPT assets with a zero issuer → `temBAD_MPT`.
 *  - XRP with a non-zero issuer → `temBAD_ISSUER`.
 *  - Malformed currency codes → `temBAD_CURRENCY`.
 *  - Asset not matching either element of `pair` (when provided) → `temBAD_AMM_TOKENS`.
 *
 *  @param asset  The asset to validate.
 *  @param pair   When provided, `asset` must equal `pair->first` or
 *      `pair->second`; used to confirm the asset belongs to a specific pool.
 *  @return `tesSUCCESS` if valid; a `tem*` error code otherwise.
 */
NotTEC
invalidAMMAsset(
    Asset const& asset,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt);

/** Validate a pair of assets for use in an AMM transaction (preflight check).
 *
 *  Rejects identical assets (`temBAD_AMM_TOKENS`) before delegating each
 *  asset to `invalidAMMAsset`.
 *
 *  @param asset1  First asset of the pair.
 *  @param asset2  Second asset of the pair.
 *  @param pair    When provided, each asset must match one element of this
 *      known-good pair; passed through to `invalidAMMAsset`.
 *  @return `tesSUCCESS` if valid; a `tem*` error code otherwise.
 */
NotTEC
invalidAMMAssetPair(
    Asset const& asset1,
    Asset const& asset2,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt);

/** Compute the zero-based time-slot index for an active auction slot.
 *
 *  Derives the slot start from `auctionSlot[sfExpiration] - kTOTAL_TIME_SLOT_SECS`,
 *  then integer-divides elapsed seconds by `kAUCTION_SLOT_INTERVAL_DURATION`.
 *  Returns `std::nullopt` when `current` is before the slot start or at or
 *  after `sfExpiration`, indicating the slot has expired or has not yet begun.
 *
 *  @param current      Current ledger time (NetClock seconds).
 *  @param auctionSlot  The `STObject` representing the AMM's auction slot;
 *      must contain `sfExpiration`.
 *  @return Slot index in `[0, kAUCTION_SLOT_TIME_INTERVALS)`, or
 *      `std::nullopt` if the slot is not currently active.
 *  @note An `XRPL_ASSERT` fires if `sfExpiration < kTOTAL_TIME_SLOT_SECS`,
 *      which is considered an impossible ledger state.
 */
std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot);

/** Return true if the network has enabled both AMM amendments.
 *
 *  Requires both `featureAMM` and `fixUniversalNumber`.  The second
 *  amendment is a hard dependency: AMM arithmetic relies on the corrected
 *  high-precision numeric library introduced by `fixUniversalNumber`, and
 *  allowing AMM transactions on networks without it would cause overflow or
 *  precision loss in intermediate swap calculations.
 *
 *  @param rules  Snapshot of currently enabled amendments.
 *  @return `true` only when both `featureAMM` and `fixUniversalNumber` are
 *      active.
 */
bool
ammEnabled(Rules const&);

/** Convert a trading fee integer to the fee fraction `f`.
 *
 *  Divides `tfee` by `kAUCTION_SLOT_FEE_SCALE_FACTOR` (100,000) to produce
 *  the dimensionless fraction used in swap arithmetic.  At the maximum fee
 *  `kTRADING_FEE_THRESHOLD = 1000`, `getFee` returns `0.01` (1%).
 *
 *  @param tfee  Trading fee integer in `[0, kTRADING_FEE_THRESHOLD]`.
 *  @return Fee fraction `f = tfee / 100000`.
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / kAUCTION_SLOT_FEE_SCALE_FACTOR;
}

/** Compute the full-fee swap multiplier `(1 - f)`.
 *
 *  Applied to the input amount when the complete trading fee is charged,
 *  i.e. during ordinary swaps.  In `AMMDeposit`, this is the `f1` factor
 *  in the single-asset constant-product formula.
 *
 *  @param tfee  Trading fee integer in `[0, kTRADING_FEE_THRESHOLD]`.
 *  @return `1 - getFee(tfee)`.
 *  @see feeMultHalf
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return 1 - getFee(tfee);
}

/** Compute the half-fee swap multiplier `(1 - f/2)`.
 *
 *  Used during single-asset deposits where only half the implied fee is
 *  deducted.  In `AMMDeposit`, the combined factor is
 *  `f2 = feeMultHalf(tfee) / feeMult(tfee)`.
 *
 *  @param tfee  Trading fee integer in `[0, kTRADING_FEE_THRESHOLD]`.
 *  @return `1 - getFee(tfee) / 2`.
 *  @see feeMult
 */
inline Number
feeMultHalf(std::uint16_t tfee)
{
    return 1 - getFee(tfee) / 2;
}

}  // namespace xrpl
