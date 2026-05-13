/** @file
 *  Stateless validation and utility functions shared by all AMM transactors.
 *
 *  Every AMM transaction handler (AMMCreate, AMMDeposit, AMMWithdraw, AMMBid,
 *  AMMVote, AMMDelete) calls into these functions during preflight to validate
 *  inputs before touching ledger state.  The file also provides LP token
 *  identity derivation and auction slot time arithmetic.  No ledger access or
 *  state mutation occurs here.
 */
#include <xrpl/protocol/AMMCore.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace xrpl {

/** Compute the LP token currency for an AMM asset pair.
 *
 *  Produces a deterministic `Currency` identifier whose first byte is `0x03`
 *  (the AMM LP token marker) followed by 19 bytes taken from the SHA-512 half
 *  of the canonically ordered asset pair.  The inputs are sorted via
 *  `std::minmax` before hashing, so `ammLPTCurrency(A, B)` and
 *  `ammLPTCurrency(B, A)` always return the same value.
 *
 *  For an `Issue` asset the hash covers its `Currency` field; for an
 *  `MPTIssue` asset it covers the 192-bit `MPTID`.
 *
 *  @param asset1 One asset of the pool.
 *  @param asset2 The other asset of the pool.
 *  @return A `Currency` with prefix byte `0x03` that uniquely identifies the
 *      LP token for this pair.
 */
Currency
ammLPTCurrency(Asset const& asset1, Asset const& asset2)
{
    // AMM LPToken is 0x03 plus 19 bytes of the hash
    std::int32_t constexpr kAMM_CURRENCY_CODE = 0x03;
    auto const& [minA, maxA] = std::minmax(asset1, asset2);
    uint256 const hash = std::visit(
        [](auto&& issue1, auto&& issue2) {
            auto fromIss = []<ValidIssueType T>(T const& issue) {
                if constexpr (std::is_same_v<T, Issue>)
                    return issue.currency;
                if constexpr (std::is_same_v<T, MPTIssue>)
                    return issue.getMptID();
            };
            return sha512Half(fromIss(issue1), fromIss(issue2));
        },
        minA.value(),
        maxA.value());
    Currency currency;
    *currency.begin() = kAMM_CURRENCY_CODE;
    std::copy(hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

/** Construct the complete LP token `Issue` for an AMM asset pair.
 *
 *  Combines the currency derived by `ammLPTCurrency` with the AMM account
 *  to form an `Issue` that fully identifies the LP token on the ledger.
 *
 *  @param asset1      One asset of the pool.
 *  @param asset2      The other asset of the pool.
 *  @param ammAccountID Account ID of the AMM instance.
 *  @return An `Issue` identifying the LP token issued by `ammAccountID`.
 */
Issue
ammLPTIssue(Asset const& asset1, Asset const& asset2, AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(asset1, asset2), ammAccountID);
}

/** Validate a single AMM asset for structural correctness.
 *
 *  Dispatches on the asset type:
 *  - `MPTIssue`: rejects a zero issuer (`temBAD_MPT`).
 *  - `Issue`: rejects `badCurrency()` (`temBAD_CURRENCY`) and any XRP
 *    `Issue` that carries a non-zero issuer (`temBAD_ISSUER`), which is
 *    structurally impossible on the XRP Ledger.
 *
 *  When `pair` is provided the asset must also be one of the two pool assets,
 *  otherwise `temBAD_AMM_TOKENS` is returned.  Omit `pair` to perform a
 *  format-only check with no pool membership constraint.
 *
 *  @param asset The asset to validate.
 *  @param pair  Optional known asset pair for the pool; when supplied, the
 *      asset must match `pair->first` or `pair->second`.
 *  @return `tesSUCCESS` if valid; a `tem*` error code otherwise.
 */
NotTEC
invalidAMMAsset(Asset const& asset, std::optional<std::pair<Asset, Asset>> const& pair)
{
    auto const err = asset.visit(
        [](MPTIssue const& issue) -> std::optional<NotTEC> {
            if (issue.getIssuer() == beast::kZERO)
                return temBAD_MPT;
            return std::nullopt;
        },
        [](Issue const& issue) -> std::optional<NotTEC> {
            if (badCurrency() == issue.currency)
                return temBAD_CURRENCY;
            if (isXRP(issue) && issue.getIssuer().isNonZero())
                return temBAD_ISSUER;
            return std::nullopt;
        });
    if (err)
        return *err;
    if (pair && asset != pair->first && asset != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

/** Validate that two assets form a legal AMM pool pair.
 *
 *  First asserts the assets are distinct (`temBAD_AMM_TOKENS` if equal), then
 *  validates each individually via `invalidAMMAsset`.  Assets of different
 *  types are never equal, so cross-type self-pairing is implicitly prevented.
 *
 *  @param asset1 First pool asset.
 *  @param asset2 Second pool asset; must differ from `asset1`.
 *  @param pair   Optional known pool pair forwarded to `invalidAMMAsset` for
 *      pool membership checks on each asset.
 *  @return `tesSUCCESS` if both assets are valid and distinct; a `tem*`
 *      error code otherwise.
 */
NotTEC
invalidAMMAssetPair(
    Asset const& asset1,
    Asset const& asset2,
    std::optional<std::pair<Asset, Asset>> const& pair)
{
    if (asset1 == asset2)
        return temBAD_AMM_TOKENS;
    if (auto const res = invalidAMMAsset(asset1, pair))
        return res;
    if (auto const res = invalidAMMAsset(asset2, pair))
        return res;
    return tesSUCCESS;
}

/** Validate an `STAmount` for use in an AMM transaction.
 *
 *  Extracts the amount's asset and passes it through `invalidAMMAsset`, then
 *  checks the numeric value.  Negative amounts always fail (`temBAD_AMOUNT`).
 *  Zero amounts fail unless `validZero` is `true`.
 *
 *  @param amount    The amount to validate.
 *  @param pair      Optional pool asset pair forwarded to `invalidAMMAsset`;
 *      when supplied, the amount's asset must be one of the pair.
 *  @param validZero Set `true` to permit a zero amount.  Use `true` for
 *      fields that act as optional sentinels (e.g., a minimum bid of zero in
 *      `AMMBid`, or a deposit amount when an EPrice limit is provided).
 *  @return `tesSUCCESS` if the amount is valid; a `tem*` error code otherwise.
 */
NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Asset, Asset>> const& pair,
    bool validZero)
{
    if (auto const res = invalidAMMAsset(amount.asset(), pair))
        return res;
    if (amount < beast::kZERO || (!validZero && amount == beast::kZERO))
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

/** Map a ledger close timestamp to an auction slot interval index.
 *
 *  The 24-hour auction window is divided into `kAUCTION_SLOT_TIME_INTERVALS`
 *  (20) equal sub-intervals of `kAUCTION_SLOT_INTERVAL_DURATION` (4320 s /
 *  72 min) each.  The window runs from `expiration - kTOTAL_TIME_SLOT_SECS`
 *  to `expiration`.
 *
 *  Returns `std::nullopt` when `current` falls outside the window (i.e., the
 *  slot has not yet started or has already expired).  Callers treat
 *  `std::nullopt` as "no active slot holder" and `kAUCTION_SLOT_TIME_INTERVALS`
 *  (20) as the sentinel for an expired slot in RPC responses.
 *
 *  @param current     Current ledger close time as seconds since the network
 *      epoch.
 *  @param auctionSlot The `sfAuctionSlot` inner object from the AMM ledger
 *      entry; must contain `sfExpiration`.
 *  @return The zero-based interval index in [0, 19] if `current` is within the
 *      active auction window, or `std::nullopt` if outside.
 *  @note Slot index 19 is the "tailing slot": the holder is not refunded when
 *      outbid during this final interval (checked by callers with
 *      `*timeSlot < kTAILING_SLOT`).
 */
std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    // It should be impossible for expiration to be < TOTAL_TIME_SLOT_SECS,
    // but check just to be safe
    auto const expiration = auctionSlot[sfExpiration];
    XRPL_ASSERT(
        expiration >= kTOTAL_TIME_SLOT_SECS, "xrpl::ammAuctionTimeSlot : minimum expiration");
    if (expiration >= kTOTAL_TIME_SLOT_SECS)
    {
        if (auto const start = expiration - kTOTAL_TIME_SLOT_SECS; current >= start)
        {
            if (auto const diff = current - start; diff < kTOTAL_TIME_SLOT_SECS)
                return diff / kAUCTION_SLOT_INTERVAL_DURATION;
        }
    }
    return std::nullopt;
}

/** Return `true` if AMM transactions are enabled on this ledger.
 *
 *  Requires both `featureAMM` and `fixUniversalNumber` to be active.
 *  The second amendment is mandatory because AMM math relies on the `Number`
 *  type from `xrpl/basics/Number.h`; `fixUniversalNumber` corrects edge cases
 *  in that type's arithmetic.  Tying both amendments together prevents subtle
 *  calculation bugs on ledgers where `featureAMM` was enabled before the
 *  numeric foundation was sound.
 *
 *  @param rules The active ledger rules snapshot.
 *  @return `true` if both `featureAMM` and `fixUniversalNumber` are enabled.
 */
bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

}  // namespace xrpl
