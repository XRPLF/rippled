#include <xrpld/rpc/detail/Pathfinder.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathfinderUtils.h>
#include <xrpld/rpc/detail/RippleLineCache.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/join.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/RippleCalc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

/** @file
 *  Core Payment Path Discovery Engine.
 *
 *  Implements a three-phase pipeline for finding, ranking, and selecting
 *  multi-hop payment routes across the XRP Ledger:
 *
 *  1. **findPaths()** — graph traversal that enumerates candidate paths up to
 *     a configurable `searchLevel`. The payment request is categorized
 *     (XRP→XRP, XRP→non-XRP, non-XRP→XRP, same-currency, cross-currency) and
 *     each category's path shapes are defined in the static `gPathTable`.
 *
 *  2. **computePathRanks()** — runs `RippleCalc::rippleCalculate` against a
 *     read-only `PaymentSandbox` for each candidate path to measure actual
 *     liquidity. Paths with trivial liquidity are dropped; survivors are sorted
 *     by quality, then liquidity, then length.
 *
 *  3. **getBestPaths()** — fills up to `maxPaths` slots in quality order.
 *     The last slot must have enough liquidity to cover the remaining amount.
 *     If no single path covers the full amount, a `fullLiquidityPath` fallback
 *     is also returned.
 *
 *  A `searchLevel` of zero skips all searching. Extra paths may be injected
 *  via `getBestPaths()` to preserve paths from previous invocations when the
 *  search depth changes across calls.
 */

namespace xrpl {
static std::ostream&
operator<<(std::ostream& os, Pathfinder::NodeType t)
{
    return os << static_cast<int>(t);
}
static std::ostream&
operator<<(std::ostream& os, Pathfinder::PaymentType t)
{
    return os << static_cast<int>(t);
}
}  // namespace xrpl

namespace xrpl {

namespace {

/** Hard upper bound on the number of complete paths collected during traversal.
 *
 *  Prevents combinatorial explosion on well-connected ledger states. Paths
 *  found beyond this limit are silently discarded; the best routes are
 *  typically found well within the limit at normal search levels.
 */
constexpr std::size_t kPATHFINDER_MAX_COMPLETE_PATHS = 1000;

/** A candidate intermediate account for extending a payment path.
 *
 *  Candidates are scored so that accounts closer to the destination are
 *  explored first.  `kHIGH_PRIORITY` is added when the candidate is the
 *  effective destination or leads directly to it.
 */
struct AccountCandidate
{
    int priority;
    AccountID account;

    /** Priority bonus awarded to candidates that connect directly to the
     *  effective destination account.
     */
    static int const kHIGH_PRIORITY = 10000;
};

/** Strict-weak ordering for `AccountCandidate` used when sorting path
 *  extension candidates.
 *
 *  Sort order (most significant first):
 *  1. `priority` descending — higher-priority accounts are tried first.
 *  2. `account` descending — deterministic tiebreak on account ID.
 *  3. `(priority ^ seq)` ascending — XOR with the ledger sequence number
 *     provides a pseudo-random final tiebreak that varies across ledgers,
 *     preventing systematic starvation of any particular route.
 *
 *  @param seq    Current ledger sequence number, used as the pseudo-random seed.
 *  @param first  Left-hand candidate.
 *  @param second Right-hand candidate.
 *  @return `true` if `first` should sort before `second`.
 */
bool
compareAccountCandidate(
    std::uint32_t seq,
    AccountCandidate const& first,
    AccountCandidate const& second)
{
    // Primary sort key: priority descending
    if (first.priority != second.priority)
        return first.priority > second.priority;

    // Secondary sort key: account descending
    if (first.account != second.account)
        return first.account > second.account;

    // Tertiary sort key (tie-breaker): (priority ^ seq) ascending
    // Note: The primary and secondary keys are equal here.
    return (first.priority ^ seq) < (second.priority ^ seq);
}

using AccountCandidates = std::vector<AccountCandidate>;

/** A single path shape entry in the static path table, paired with its
 *  search-level cost.
 *
 *  Paths whose `searchLevel` exceeds the caller-requested search depth are
 *  skipped, so cheaper (lower-level) path shapes are always tried first.
 */
struct CostedPath
{
    int searchLevel;       ///< Minimum search depth at which this path is tried (0–10).
    Pathfinder::PathType type;  ///< Sequence of `NodeType` values encoding the route shape.
};

/** All costed paths for one `PaymentType`. */
using CostedPathList = std::vector<CostedPath>;

/** The global static path table: maps each `PaymentType` to its ordered list
 *  of route shapes and their associated search-level costs.
 */
using PathTable = std::map<Pathfinder::PaymentType, CostedPathList>;

/** Compact representation of a path used when building `gPathTable`.
 *
 *  `path` is a short string of node-type characters (`s`, `a`, `b`, `x`,
 *  `f`, `d`) that `makePath()` decodes into a `PathType`.
 */
struct PathCost
{
    int cost;            ///< Search-level cost (0–10).
    char const* path;    ///< Encoded path string; decoded by `makePath()`.
};
using PathCostList = std::vector<PathCost>;

/** Global path-shape table, populated once by `Pathfinder::initPathTable()`.
 *
 *  Keyed by `PaymentType`; each value is a list of `CostedPath` entries in
 *  ascending search-level order.  Must not be accessed before
 *  `initPathTable()` is called at startup.
 */
PathTable gPathTable;

/** Render a `PathType` as a compact string for logging.
 *
 *  Each `NodeType` maps to a single character: `s` Source, `a` Accounts,
 *  `b` Books, `x` XrpBook, `f` DestBook, `d` Destination.
 *
 *  @param type  The path type to render.
 *  @return      Human-readable string, e.g. `"sfad"`.
 */
std::string
pathTypeToString(Pathfinder::PathType const& type)
{
    std::string ret;

    for (auto const& node : type)
    {
        switch (node)
        {
            case Pathfinder::NodeType::Source:
                ret.append("s");
                break;
            case Pathfinder::NodeType::Accounts:
                ret.append("a");
                break;
            case Pathfinder::NodeType::Books:
                ret.append("b");
                break;
            case Pathfinder::NodeType::XrpBook:
                ret.append("x");
                break;
            case Pathfinder::NodeType::DestBook:
                ret.append("f");
                break;
            case Pathfinder::NodeType::Destination:
                ret.append("d");
                break;
        }
    }

    return ret;
}

/** Compute the minimum delivery amount that makes a path worth retaining.
 *
 *  A path that cannot deliver at least `amount / (maxPaths + 2)` is
 *  considered to have trivial liquidity and is dropped during ranking.
 *  The `+2` provides a small buffer above an even split across all paths.
 *
 *  @param amount    The total destination amount requested.
 *  @param maxPaths  Maximum number of paths that will be selected.
 *  @return          The per-path minimum useful delivery amount.
 */
STAmount
smallestUsefulAmount(STAmount const& amount, int maxPaths)
{
    return divide(amount, STAmount(maxPaths + 2), amount.asset());
}

/** Construct a unit-one `STAmount` representing the source asset.
 *
 *  Used to build `srcAmount_` during `Pathfinder` construction.
 *  For IOU assets the issuer is resolved as: explicit `srcIssuer` if
 *  provided, XRP account for XRP currency, or `srcAccount` otherwise.
 *  For MPT assets the issuer is implicit in the `MPTID`.
 *
 *  @param pathAsset   The source asset (IOU currency or MPT ID).
 *  @param srcIssuer   Optional explicit issuer override.
 *  @param srcAccount  The source account ID.
 *  @return            An `STAmount` of value 1 in the appropriate asset.
 */
STAmount
amountFromPathAsset(
    PathAsset const& pathAsset,
    std::optional<AccountID> const& srcIssuer,
    AccountID const& srcAccount)
{
    return pathAsset.visit(
        [&](Currency const& currency) {
            auto const& account = srcIssuer.value_or(isXRP(currency) ? xrpAccount() : srcAccount);
            return STAmount(Issue{currency, account}, 1u, 0, true);
        },
        [](MPTID const& mpt) { return STAmount(mpt, 1u, 0, true); });
}

/** Convert a `PathAsset` and issuer account into a fully-qualified `Asset`.
 *
 *  For IOU currencies the issuer is the supplied `account`; for MPT the
 *  issuer is embedded in the `MPTID`.
 *
 *  @param pathAsset  The asset variant (currency or MPT ID).
 *  @param account    Issuer account, used only for IOU currencies.
 *  @return           The corresponding `Asset`.
 */
Asset
assetFromPathAsset(PathAsset const& pathAsset, AccountID const& account)
{
    return pathAsset.visit(
        [&](Currency const& currency) { return Asset{Issue{currency, account}}; },
        [](MPTID const& mpt) { return Asset{mpt}; });
}

}  // namespace

/** Construct a `Pathfinder` and initialise all path-search state.
 *
 *  `effectiveDst_` is the gateway account when `saDstAmount` carries a
 *  non-XRP issuer that differs from `uDstAccount`; otherwise it equals
 *  `uDstAccount`.  `srcAmount_` is synthesised as a unit-one amount in the
 *  source asset so that `RippleCalc` probes start from a consistent base.
 *  `convert_all_` is `true` when `saDstAmount` is the ledger maximum,
 *  signalling a "send as much as possible" payment.
 *
 *  @param cache         Memoized trust-line and MPT view of the current ledger.
 *  @param uSrcAccount   Account sending the payment.
 *  @param uDstAccount   Ultimate destination account.
 *  @param uSrcPathAsset Source asset (IOU currency or MPT ID).
 *  @param uSrcIssuer    Optional explicit issuer for the source asset.
 *  @param saDstAmount   Amount to deliver; determines `PaymentType` and
 *      `convert_all_`.
 *  @param srcAmount     Optional explicit source amount (currently unused
 *      in path ranking but stored for future use).
 *  @param domain        Optional domain filter restricting which order books
 *      and trust lines are considered.
 *  @param app           Application context (job queue, order-book DB, etc.).
 */
Pathfinder::Pathfinder(
    std::shared_ptr<AssetCache> const& cache,
    AccountID const& uSrcAccount,
    AccountID const& uDstAccount,
    PathAsset const& uSrcPathAsset,
    std::optional<AccountID> const& uSrcIssuer,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& srcAmount,
    std::optional<uint256> const& domain,
    Application& app)
    : srcAccount_(uSrcAccount)
    , dstAccount_(uDstAccount)
    , effectiveDst_(isXRP(saDstAmount.getIssuer()) ? uDstAccount : saDstAmount.getIssuer())
    , dstAmount_(saDstAmount)
    , srcPathAsset_(uSrcPathAsset)
    , srcIssuer_(uSrcIssuer)
    , srcAmount_(amountFromPathAsset(uSrcPathAsset, uSrcIssuer, uSrcAccount))
    , convert_all_(convertAllCheck(dstAmount_))
    , domain_(domain)
    , ledger_(cache->getLedger())
    , rLCache_(cache)
    , app_(app)
    , j_(app.getJournal("Pathfinder"))
{
    XRPL_ASSERT(
        !uSrcIssuer || uSrcPathAsset.isXRP() == isXRP(uSrcIssuer.value()),
        "xrpl::Pathfinder::Pathfinder : valid inputs");
}

/** Enumerate candidate complete payment paths up to the given search depth.
 *
 *  Validates preconditions (non-zero destination, accounts exist, new-account
 *  funding rules), classifies the payment into one of the five `PaymentType`
 *  categories, then iterates over all entries in `gPathTable[paymentType]`
 *  whose `searchLevel` does not exceed `searchLevel`, calling
 *  `addPathsForType()` for each.  Stops early once
 *  `kPATHFINDER_MAX_COMPLETE_PATHS` complete paths have accumulated.
 *
 *  Returns `true` when the caller should proceed to `computePathRanks()`.
 *  Returns `false` when the search is definitively hopeless (zero destination
 *  amount, source equals destination with the same asset, missing ledger, etc.)
 *  In those cases `ledger_` is reset to release its reference.
 *
 *  @param searchLevel       Maximum cost tier to explore (0 = no search).
 *  @param continueCallback  Optional cooperative-cancellation probe; returning
 *      `false` aborts the search and causes this function to return `false`.
 *  @return `true` if path-finding should continue to ranking; `false` if the
 *      request is unsatisfiable or was cancelled.
 */
bool
Pathfinder::findPaths(int searchLevel, std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.trace()) << "findPaths start";
    if (dstAmount_ == beast::kZERO)
    {
        JLOG(j_.debug()) << "Destination amount was zero.";
        ledger_.reset();
        return false;

        // TODO(tom): why do we reset the ledger just in this case and the one
        // below - why don't we do it each time we return false?
    }

    if (srcAccount_ == dstAccount_ && dstAccount_ == effectiveDst_ &&
        srcPathAsset_ == dstAmount_.asset())
    {
        JLOG(j_.debug()) << "Tried to send to same issuer";
        ledger_.reset();
        return false;
    }

    if (srcAccount_ == effectiveDst_ && srcPathAsset_ == dstAmount_.asset())
    {
        // Default path might work, but any explicit path would loop back to
        // the source; let the caller try the default path only.
        return true;
    }

    loadEvent_ = app_.getJobQueue().makeLoadEvent(JtPathFind, "FindPath");
    auto currencyIsXRP = isXRP(srcPathAsset_);

    bool const useIssuerAccount = srcIssuer_ && !currencyIsXRP && !isXRP(*srcIssuer_);
    auto& account = useIssuerAccount ? *srcIssuer_ : srcAccount_;
    auto issuer = currencyIsXRP ? AccountID() : account;
    source_ = STPathElement(account, srcPathAsset_, issuer);
    auto issuerString = srcIssuer_ ? to_string(*srcIssuer_) : std::string("none");
    JLOG(j_.trace()) << "findPaths>"
                     << " srcAccount_=" << srcAccount_ << " dstAccount_=" << dstAccount_
                     << " dstAmount_=" << dstAmount_.getFullText()
                     << " srcPathAsset_=" << srcPathAsset_ << " srcIssuer_=" << issuerString;

    if (!ledger_)
    {
        JLOG(j_.debug()) << "findPaths< no ledger";
        return false;
    }

    bool const bSrcXrp = isXRP(srcPathAsset_);
    bool const bDstXrp = isXRP(dstAmount_.asset());

    if (!ledger_->exists(keylet::account(srcAccount_)))
    {
        // We can't even start without a source account.
        JLOG(j_.debug()) << "invalid source account";
        return false;
    }

    if ((effectiveDst_ != dstAccount_) && !ledger_->exists(keylet::account(effectiveDst_)))
    {
        JLOG(j_.debug()) << "Non-existent gateway";
        return false;
    }

    if (!ledger_->exists(keylet::account(dstAccount_)))
    {
        // Can't find the destination account - we must be funding a new
        // account.
        if (!bDstXrp)
        {
            JLOG(j_.debug()) << "New account not being funded in XRP ";
            return false;
        }

        auto const reserve = STAmount(ledger_->fees().reserve);
        if (dstAmount_ < reserve)
        {
            JLOG(j_.debug()) << "New account not getting enough funding: " << dstAmount_ << " < "
                             << reserve;
            return false;
        }
    }

    PaymentType paymentType = PaymentType::XrpToXrp;
    if (bSrcXrp && bDstXrp)
    {
        JLOG(j_.debug()) << "XRP to XRP payment";
        paymentType = PaymentType::XrpToXrp;
    }
    else if (bSrcXrp)
    {
        JLOG(j_.debug()) << "XRP to non-XRP payment";
        paymentType = PaymentType::XrpToNonXrp;
    }
    else if (bDstXrp)
    {
        JLOG(j_.debug()) << "non-XRP to XRP payment";
        paymentType = PaymentType::NonXrpToXrp;
    }
    else if (srcPathAsset_ == dstAmount_.asset())
    {
        JLOG(j_.debug()) << "non-XRP to non-XRP - same currency";
        paymentType = PaymentType::NonXrpToSame;
    }
    else
    {
        JLOG(j_.debug()) << "non-XRP to non-XRP - cross currency";
        paymentType = PaymentType::NonXrpToNonXrp;
    }

    for (auto const& costedPath : gPathTable[paymentType])
    {
        if (continueCallback && !continueCallback())
            return false;
        // Only use paths with at most the current search level.
        if (costedPath.searchLevel <= searchLevel)
        {
            JLOG(j_.trace()) << "findPaths trying payment type " << paymentType;
            addPathsForType(costedPath.type, continueCallback);

            if (completePaths_.size() > kPATHFINDER_MAX_COMPLETE_PATHS)
                break;
        }
    }

    JLOG(j_.debug()) << completePaths_.size() << " complete paths found";

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

/** Probe the actual liquidity available along a single path.
 *
 *  Runs `RippleCalc::rippleCalculate` against a read-only `PaymentSandbox`
 *  (so no ledger state is mutated).  A two-pass strategy is used:
 *
 *  - **Pass 1**: check whether the path can deliver at least `minDstAmount`.
 *    If not, the path is dropped and the function returns the error TER.
 *  - **Pass 2** (skipped for `convert_all_` payments): attempt to deliver
 *    the remainder of `dstAmount_` beyond what pass 1 delivered, accumulating
 *    any additional liquidity into `amountOut`.
 *
 *  For `convert_all_` payments, `partialPaymentAllowed` is set during
 *  pass 1 so that `RippleCalc` delivers as much as it can.
 *
 *  @param path          The candidate path to probe.
 *  @param minDstAmount  Minimum delivery for the path to be retained.
 *  @param amountOut     Receives total liquidity delivered by this path.
 *  @param qualityOut    Receives the initial exchange rate (out/in ratio as a
 *      64-bit fixed-point quality).
 *  @return `tesSUCCESS` if the path meets the minimum; otherwise the TER from
 *      the first `RippleCalc` call, or `tefEXCEPTION` on unexpected exceptions.
 */
TER
Pathfinder::getPathLiquidity(
    STPath const& path,            // IN:  The path to check.
    STAmount const& minDstAmount,  // IN:  The minimum output this path must
                                   //      deliver to be worth keeping.
    STAmount& amountOut,           // OUT: The actual liquidity along the path.
    uint64_t& qualityOut) const    // OUT: The returned initial quality
{
    STPathSet pathSet;
    pathSet.pushBack(path);

    path::RippleCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    PaymentSandbox sandbox(&*ledger_, TapNone);

    try
    {
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;

        auto rc = path::RippleCalc::rippleCalculate(
            sandbox,
            srcAmount_,
            minDstAmount,
            dstAccount_,
            srcAccount_,
            pathSet,
            domain_,
            app_,
            &rcInput);
        if (!isTesSuccess(rc.result()))
            return rc.result();

        qualityOut = getRate(rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        if (!convert_all_)
        {
            rcInput.partialPaymentAllowed = true;
            rc = path::RippleCalc::rippleCalculate(
                sandbox,
                srcAmount_,
                dstAmount_ - amountOut,
                dstAccount_,
                srcAccount_,
                pathSet,
                domain_,
                app_,
                &rcInput);

            if (rc.result() == tesSUCCESS)
                amountOut += rc.actualAmountOut;
        }

        return tesSUCCESS;
    }
    catch (std::exception const& e)
    {
        JLOG(j_.info()) << "checkpath: exception (" << e.what() << ") "
                        << path.getJson(JsonOptions::Values::None);
        return tefEXCEPTION;
    }
}

/** Rank all discovered paths by quality and liquidity.
 *
 *  Before ranking the discovered paths, probes the *default path* (an empty
 *  `STPathSet`) via `RippleCalc` and subtracts however much the default path
 *  can deliver from `remainingAmount_`.  This means the explicitly discovered
 *  paths only need to cover the residual, which is the typical case where a
 *  direct trust-line already handles part of the payment.
 *
 *  Delegates to `rankPaths()` which calls `getPathLiquidity()` for each
 *  complete path and sorts survivors by quality → liquidity → length.
 *
 *  Must be called after `findPaths()` returns `true` and before
 *  `getBestPaths()`.
 *
 *  @param maxPaths          Maximum number of paths that will ultimately be
 *      selected; used to compute the per-path minimum useful amount.
 *  @param continueCallback  Optional cooperative-cancellation probe.
 */
void
Pathfinder::computePathRanks(int maxPaths, std::function<bool(void)> const& continueCallback)
{
    remainingAmount_ = convertAmount(dstAmount_, convert_all_);

    try
    {
        PaymentSandbox sandbox(&*ledger_, TapNone);

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = true;
        auto rc = path::RippleCalc::rippleCalculate(
            sandbox,
            srcAmount_,
            remainingAmount_,
            dstAccount_,
            srcAccount_,
            STPathSet(),
            domain_,
            app_,
            &rcInput);

        if (rc.result() == tesSUCCESS)
        {
            JLOG(j_.debug()) << "Default path contributes: " << rc.actualAmountIn;
            remainingAmount_ -= rc.actualAmountOut;
        }
        else
        {
            JLOG(j_.debug()) << "Default path fails: " << transToken(rc.result());
        }
    }
    catch (std::exception const&)
    {
        JLOG(j_.debug()) << "Default path causes exception";
    }

    rankPaths(maxPaths, completePaths_, pathRanks_, continueCallback);
}

/** Return `true` if `path` is considered a "default path".
 *
 *  A default path consists of exactly one hop (a single path element) and
 *  represents a direct trust-line transfer without intermediaries.
 *
 *  @note This test is a known approximation — see the FIXME comment below.
 *      Default paths can in theory contain more than one account element;
 *      this heuristic may incorrectly classify some multi-element paths.
 */
static bool
isDefaultPath(STPath const& path)
{
    // FIXME: default paths can consist of more than just an account:
    //
    // JoelKatz writes:
    // So the test for whether a path is a default path is incorrect. I'm not
    // sure it's worth the complexity of fixing though. If we are going to fix
    // it, I'd suggest doing it this way:
    //
    // 1) Compute the default path, probably by using 'expandPath' to expand an
    // empty path.  2) Chop off the source and destination nodes.
    //
    // 3) In the pathfinding loop, if the source issuer is not the sender,
    // reject all paths that don't begin with the issuer's account node or match
    // the path we built at step 2.
    return path.size() == 1;
}

/** Strip the leading issuer-account node from a path.
 *
 *  When the source issuer is not the sender, discovered paths begin with the
 *  issuer's account node.  The payment engine already implies this node from
 *  the transaction metadata, so it must be removed before returning the path
 *  to callers.
 *
 *  @param path  A path whose first element is the implicit source issuer.
 *  @return      The same path with the first element removed.
 */
static STPath
removeIssuer(STPath const& path)
{
    // This path starts with the issuer, which is already implied
    // so remove the head node
    STPath ret;

    for (auto it = path.begin() + 1; it != path.end(); ++it)
        ret.pushBack(*it);

    return ret;
}

/** Evaluate each path in `paths` and build a sorted `rankedPaths` vector.
 *
 *  For each non-empty path, calls `getPathLiquidity()` to probe actual
 *  deliverable liquidity.  Paths that cannot deliver the minimum useful amount
 *  are dropped.  Survivors are sorted ascending by:
 *  1. Quality (exchange rate — lower cost is better); ignored for
 *     `convert_all_` payments where quality is irrelevant.
 *  2. Liquidity descending (more is better).
 *  3. Path length ascending (shorter is better).
 *  4. Index descending as a final tie-breaker.
 *
 *  @param maxPaths          Used to compute the per-path minimum amount via
 *      `smallestUsefulAmount()` (or `largestAmount()` for `convert_all_`).
 *  @param paths             Input candidate paths (typically `completePaths_`
 *      or `extraPaths`).
 *  @param rankedPaths       Output: cleared and filled with ranked survivors.
 *  @param continueCallback  Optional cooperative-cancellation probe.
 */
void
Pathfinder::rankPaths(
    int maxPaths,
    STPathSet const& paths,
    std::vector<PathRank>& rankedPaths,
    std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.trace()) << "rankPaths with " << paths.size() << " candidates, and " << maxPaths
                     << " maximum";
    rankedPaths.clear();
    rankedPaths.reserve(paths.size());

    auto const saMinDstAmount = [&]() -> STAmount {
        if (!convert_all_)
        {
            // Ignore paths that move only very small amounts.
            return smallestUsefulAmount(dstAmount_, maxPaths);
        }

        // On convert_all_ partialPaymentAllowed will be set to true
        // and requiring a huge amount will find the highest liquidity.
        return largestAmount(dstAmount_);
    }();

    for (int i = 0; i < paths.size(); ++i)
    {
        if (continueCallback && !continueCallback())
            return;
        auto const& currentPath = paths[i];
        if (!currentPath.empty())
        {
            STAmount liquidity;
            uint64_t uQuality = 0;
            auto const resultCode =
                getPathLiquidity(currentPath, saMinDstAmount, liquidity, uQuality);
            if (!isTesSuccess(resultCode))
            {
                JLOG(j_.debug()) << "findPaths: dropping : " << transToken(resultCode) << ": "
                                 << currentPath.getJson(JsonOptions::Values::None);
            }
            else
            {
                JLOG(j_.debug()) << "findPaths: quality: " << uQuality << ": "
                                 << currentPath.getJson(JsonOptions::Values::None);

                rankedPaths.push_back({uQuality, currentPath.size(), liquidity, i});
            }
        }
    }

    std::ranges::sort(
        rankedPaths, [&](Pathfinder::PathRank const& a, Pathfinder::PathRank const& b) {
            if (!convert_all_ && a.quality != b.quality)
                return a.quality < b.quality;

            if (a.liquidity != b.liquidity)
                return a.liquidity > b.liquidity;

            if (a.length != b.length)
                return a.length < b.length;

            return a.index > b.index;
        });
}

/** Select up to `maxPaths` best paths and an optional full-liquidity fallback.
 *
 *  Merges `pathRanks_` (ranked discovered paths) and `extraPathRanks` (ranked
 *  client-injected `extraPaths`) in a single linear pass, picking the
 *  better-ranked path at each step.  When both iterators are at the same
 *  quality *and* liquidity, both paths are consumed so neither is silently
 *  dropped.
 *
 *  Selection rules:
 *  - Up to `maxPaths - 1` slots are filled in quality order regardless of
 *    individual liquidity.
 *  - The last slot is only filled if the path's liquidity covers `remaining`
 *    (ensuring the full payment can succeed assuming no liquidity overlap).
 *  - After slots are full, the function continues scanning for a
 *    `fullLiquidityPath`: a single path whose liquidity ≥ `dstAmount_` that
 *    can be returned as a covering fallback.
 *
 *  When `srcIssuer` is not the sender, only paths whose first node is
 *  `srcIssuer` are eligible; the issuer node is stripped via `removeIssuer()`
 *  before adding to the result.
 *
 *  @param maxPaths           Maximum number of paths to return.
 *  @param fullLiquidityPath  Output: set to a single path that alone can cover
 *      `dstAmount_`, if one exists; left empty otherwise.
 *  @param extraPaths         Previously found paths to merge with discovered
 *      paths (typically preserved across `path_find` subscription updates).
 *  @param srcIssuer          Issuer of the source asset; if not the sender,
 *      filters and strips the leading issuer node.
 *  @param continueCallback   Optional cooperative-cancellation probe.
 *  @return                   The selected best paths (may be empty).
 */
STPathSet
Pathfinder::getBestPaths(
    int maxPaths,
    STPath& fullLiquidityPath,
    STPathSet const& extraPaths,
    AccountID const& srcIssuer,
    std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.debug()) << "findPaths: " << completePaths_.size() << " paths and " << extraPaths.size()
                     << " extras";

    if (completePaths_.empty() && extraPaths.empty())
        return completePaths_;

    XRPL_ASSERT(
        fullLiquidityPath.empty(), "xrpl::Pathfinder::getBestPaths : first empty path result");
    bool const issuerIsSender = isXRP(srcPathAsset_) || (srcIssuer == srcAccount_);

    std::vector<PathRank> extraPathRanks;
    rankPaths(maxPaths, extraPaths, extraPathRanks, continueCallback);

    STPathSet bestPaths;

    STAmount remaining = remainingAmount_;

    auto pathsIterator = pathRanks_.begin();
    auto extraPathsIterator = extraPathRanks.begin();

    while (pathsIterator != pathRanks_.end() || extraPathsIterator != extraPathRanks.end())
    {
        if (continueCallback && !continueCallback())
            break;
        bool usePath = false;
        bool useExtraPath = false;

        if (pathsIterator == pathRanks_.end())
        {
            useExtraPath = true;
        }
        else if (extraPathsIterator == extraPathRanks.end())
        {
            usePath = true;
        }
        else if (extraPathsIterator->quality < pathsIterator->quality)
        {
            useExtraPath = true;
        }
        else if (extraPathsIterator->quality > pathsIterator->quality)
        {
            usePath = true;
        }
        else if (extraPathsIterator->liquidity > pathsIterator->liquidity)
        {
            useExtraPath = true;
        }
        else if (extraPathsIterator->liquidity < pathsIterator->liquidity)
        {
            usePath = true;
        }
        else
        {
            // Identical quality and liquidity — consume both so neither is dropped.
            useExtraPath = true;
            usePath = true;
        }

        auto& pathRank = usePath ? *pathsIterator : *extraPathsIterator;

        auto const& path = usePath ? completePaths_[pathRank.index] : extraPaths[pathRank.index];

        if (useExtraPath)
            ++extraPathsIterator;

        if (usePath)
            ++pathsIterator;

        auto iPathsLeft = maxPaths - bestPaths.size();
        if (iPathsLeft <= 0 && !fullLiquidityPath.empty())
            break;

        if (path.empty())
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::Pathfinder::getBestPaths : path not found");
            continue;
            // LCOV_EXCL_STOP
        }

        bool startsWithIssuer = false;

        if (!issuerIsSender && usePath)
        {
            // Need to make sure path matches issuer constraints
            if (isDefaultPath(path) || path.front().getAccountID() != srcIssuer)
            {
                continue;
            }

            startsWithIssuer = true;
        }

        if (iPathsLeft > 1 || (iPathsLeft > 0 && pathRank.liquidity >= remaining))
        {
            --iPathsLeft;
            remaining -= pathRank.liquidity;
            bestPaths.pushBack(startsWithIssuer ? removeIssuer(path) : path);
        }
        else if (iPathsLeft == 0 && pathRank.liquidity >= dstAmount_ && fullLiquidityPath.empty())
        {
            fullLiquidityPath = (startsWithIssuer ? removeIssuer(path) : path);
            JLOG(j_.debug()) << "Found extra full path: "
                             << fullLiquidityPath.getJson(JsonOptions::Values::None);
        }
        else
        {
            JLOG(j_.debug()) << "Skipping a non-filling path: "
                             << path.getJson(JsonOptions::Values::None);
        }
    }

    if (remaining > beast::kZERO)
    {
        XRPL_ASSERT(
            fullLiquidityPath.empty(), "xrpl::Pathfinder::getBestPaths : second empty path result");
        JLOG(j_.info()) << "Paths could not send " << remaining << " of " << dstAmount_;
    }
    else
    {
        JLOG(j_.debug()) << "findPaths: RESULTS: " << bestPaths.getJson(JsonOptions::Values::None);
    }
    return bestPaths;
}

/** Return `true` if `asset` is the same asset as the source and issued by the
 *  source (or its explicit issuer).
 *
 *  Used during order-book traversal to prevent cycles back to the origin.
 *  An asset matches the origin when both the asset type/currency *and* the
 *  issuer match: XRP always matches as issuer; otherwise the issuer must be
 *  `srcIssuer_` (if set) or `srcAccount_`.
 *
 *  @param asset  The candidate order-book output asset to test.
 *  @return `true` if adding a hop to this asset would loop back to the source.
 */
bool
Pathfinder::issueMatchesOrigin(Asset const& asset)
{
    bool const matchingAsset = (asset == srcPathAsset_);
    bool const matchingAccount = isXRP(asset) || (srcIssuer_ && asset.getIssuer() == srcIssuer_) ||
        asset.getIssuer() == srcAccount_;

    return matchingAsset && matchingAccount;
}

/** Count the number of viable outgoing paths from `account` for `pathAsset`.
 *
 *  The result is memoized in `pathsOutCountMap_` (keyed by the fully-qualified
 *  `Asset`) to avoid repeated ledger reads for the same account/asset pair
 *  during a single path-find run.
 *
 *  Counts order-book entries that accept `pathAsset`, then adds contributions
 *  from trust lines (IOU) or MPT holdings:
 *  - Each non-frozen, non-noRipple trust line with positive available balance
 *    contributes +1; a line directly to `dstAccount` when `isDstAsset` is
 *    `true` contributes +10,000 (`kHIGH_PRIORITY`).
 *  - Equivalent rules apply for MPT holders.
 *
 *  A globally-frozen account or issuer returns 0.
 *
 *  @param pathAsset         The asset currency/ID being routed.
 *  @param account           The account at the current path endpoint.
 *  @param direction         Whether to check outgoing or incoming lines
 *      (callers pass `Incoming` when `isNoRippleOut` is set).
 *  @param isDstAsset        `true` when `pathAsset` equals the destination asset,
 *      enabling the destination-priority bonus.
 *  @param dstAccount        Effective destination account for the bonus check.
 *  @param continueCallback  Optional cooperative-cancellation probe (unused
 *      internally but accepted for API consistency).
 *  @return                  Estimated count of viable outgoing paths (≥ 0).
 */
int
Pathfinder::getPathsOut(
    PathAsset const& pathAsset,
    AccountID const& account,
    LineDirection direction,
    bool isDstAsset,
    AccountID const& dstAccount,
    std::function<bool(void)> const& continueCallback)
{
    Asset const asset = assetFromPathAsset(pathAsset, account);

    auto [it, inserted] = pathsOutCountMap_.emplace(asset, 0);

    // If it was already present, return the stored number of paths
    if (!inserted)
        return it->second;

    auto sleAccount = ledger_->read(keylet::account(account));

    if (!sleAccount)
        return 0;

    auto const aFlags = sleAccount->getFieldU32(sfFlags);
    bool const bAuthRequired = [&]() {
        if (pathAsset.holds<Currency>())
            return (aFlags & lsfRequireAuth) != 0;
        return !isTesSuccess(requireAuth(*ledger_, asset.get<MPTIssue>(), account));
    }();
    bool const bFrozen = [&]() {
        if (pathAsset.holds<Currency>())
            return (aFlags & lsfGlobalFreeze) != 0;
        return isGlobalFrozen(*ledger_, asset.get<MPTIssue>());
    }();

    int count = 0;

    if (!bFrozen)
    {
        count = app_.getOrderBookDB().getBookSize(asset, domain_);

        asset.visit(
            [&](Issue const&) {
                if (auto const lines = rLCache_->getRippleLines(account, direction))
                {
                    for (auto const& rspEntry : *lines)
                    {
                        if (pathAsset.get<Currency>() != rspEntry.getLimit().get<Issue>().currency)
                        {
                        }
                        else if (
                            rspEntry.getBalance() <= beast::kZERO &&
                            (!rspEntry.getLimitPeer() ||
                             -rspEntry.getBalance() >= rspEntry.getLimitPeer() ||
                             (bAuthRequired && !rspEntry.getAuth())))
                        {
                        }
                        else if (isDstAsset && dstAccount == rspEntry.getAccountIDPeer())
                        {
                            count += 10000;  // destination peer earns high-priority bonus
                        }
                        else if (rspEntry.getNoRipplePeer())
                        {
                            // This probably isn't a useful path out
                        }
                        else if (rspEntry.getFreezePeer())
                        {
                            // Not a useful path out
                        }
                        else
                        {
                            ++count;
                        }
                    }
                }
            },
            [&](MPTIssue const&) {
                if (auto const mpts = rLCache_->getMPTs(account))
                {
                    for (auto const& mpt : *mpts)
                    {
                        if (pathAsset.get<MPTID>() != mpt.getMptID())
                        {
                        }
                        else if (mpt.isZeroBalance() || mpt.isMaxedOut())
                        {
                        }
                        else if (bAuthRequired)
                        {
                        }
                        else if (isDstAsset && dstAccount == getMPTIssuer(mpt))
                        {
                            count += 10000;
                        }
                        else if (isIndividualFrozen(*ledger_, account, MPTIssue{mpt.getMptID()}))
                        {
                        }
                        else
                        {
                            ++count;
                        }
                    }
                }
            });
    }
    it->second = count;
    return count;
}

/** Extend every path in `currentPaths` by one hop and collect results.
 *
 *  A thin fan-out wrapper: calls `addLink()` for each path in `currentPaths`,
 *  directing extension according to `addFlags`.  New partial paths land in
 *  `incompletePaths`; newly completed paths are added directly to
 *  `completePaths_`.
 *
 *  @param currentPaths      The set of partial paths to extend.
 *  @param incompletePaths   Accumulator for newly extended partial paths.
 *  @param addFlags          Bitmask controlling hop type (accounts, books, etc.).
 *  @param continueCallback  Optional cooperative-cancellation probe.
 */
void
Pathfinder::addLinks(
    STPathSet const& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags,
    std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.debug()) << "addLink< on " << currentPaths.size() << " source(s), flags=" << addFlags;
    for (auto const& path : currentPaths)
    {
        if (continueCallback && !continueCallback())
            return;
        addLink(path, incompletePaths, addFlags, continueCallback);
    }
}

/** Recursively build and memoize all partial paths for a given `PathType`.
 *
 *  Implements the recursive memoization pattern that avoids re-expanding
 *  shared path prefixes.  Given a `PathType` such as
 *  `{Source, Accounts, Books, Destination}`, it:
 *  1. Returns the cached result immediately if `paths_[pathType]` already
 *     exists.
 *  2. Strips the last `NodeType` to form `parentPathType` and recurses to
 *     obtain all partial paths for the parent (already memoized if shared
 *     with another table entry).
 *  3. Calls `addLinks()` on the parent paths to extend by the last node type,
 *     placing new complete paths into `completePaths_` and new partial paths
 *     into `paths_[pathType]`.
 *
 *  The base case (empty `PathType`) returns an empty `STPathSet`.  The
 *  `Source` node type seeds the traversal with a single empty `STPath`.
 *
 *  @param pathType          The full path type sequence to expand.
 *  @param continueCallback  Optional cooperative-cancellation probe; returns
 *      the empty-type entry if cancelled.
 *  @return                  Reference to the (now populated) partial-path set
 *      for `pathType` in `paths_`.
 */
STPathSet&
Pathfinder::addPathsForType(
    PathType const& pathType,
    std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.debug()) << "addPathsForType " << CollectionAndDelimiter(pathType, ", ");
    auto it = paths_.find(pathType);
    if (it != paths_.end())
        return it->second;

    if (pathType.empty())
        return paths_[pathType];
    if (continueCallback && !continueCallback())
        return paths_[{}];

    PathType parentPathType = pathType;
    parentPathType.pop_back();

    STPathSet const& parentPaths = addPathsForType(parentPathType, continueCallback);
    STPathSet& pathsOut = paths_[pathType];

    JLOG(j_.debug()) << "getPaths< adding onto '" << pathTypeToString(parentPathType)
                     << "' to get '" << pathTypeToString(pathType) << "'";

    int const initialSize = completePaths_.size();

    auto nodeType = pathType.back();
    switch (nodeType)
    {
        case NodeType::Source:
            XRPL_ASSERT(pathsOut.empty(), "xrpl::Pathfinder::addPathsForType : empty paths");
            pathsOut.pushBack(STPath());
            break;

        case NodeType::Accounts:
            addLinks(parentPaths, pathsOut, kAF_ADD_ACCOUNTS, continueCallback);
            break;

        case NodeType::Books:
            addLinks(parentPaths, pathsOut, kAF_ADD_BOOKS, continueCallback);
            break;

        case NodeType::XrpBook:
            addLinks(parentPaths, pathsOut, kAF_ADD_BOOKS | kAF_OB_XRP, continueCallback);
            break;

        case NodeType::DestBook:
            addLinks(parentPaths, pathsOut, kAF_ADD_BOOKS | kAF_OB_LAST, continueCallback);
            break;

        case NodeType::Destination:
            // FIXME: What if a different issuer was specified on the
            // destination amount?
            // TODO(tom): what does this even mean?  Should it be a JIRA?
            addLinks(parentPaths, pathsOut, kAF_ADD_ACCOUNTS | kAF_AC_LAST, continueCallback);
            break;
    }

    if (completePaths_.size() != initialSize)
    {
        JLOG(j_.debug()) << (completePaths_.size() - initialSize) << " complete paths added";
    }

    JLOG(j_.debug()) << "getPaths> " << pathsOut.size() << " partial paths found";
    return pathsOut;
}

/** Return `true` if `toAccount` has set the noRipple flag on the trust line
 *  from `fromAccount` in `currency`.
 *
 *  Reads the `RippleState` SLE for the `(toAccount, fromAccount, currency)`
 *  triple and checks the high or low noRipple flag depending on which account
 *  holds the higher account ID.
 *
 *  @param fromAccount  The account that would be rippling *into* `toAccount`.
 *  @param toAccount    The account whose noRipple flag is being tested.
 *  @param currency     The shared currency of the trust line.
 *  @return `true` if noRipple is set on the incoming side of `toAccount`.
 */
bool
Pathfinder::isNoRipple(
    AccountID const& fromAccount,
    AccountID const& toAccount,
    Currency const& currency)
{
    auto sleRipple = ledger_->read(keylet::line(toAccount, fromAccount, currency));

    auto const flag((toAccount > fromAccount) ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && ((sleRipple->getFieldU32(sfFlags) & flag) != 0u);
}

/** Return `true` if the last account node in `currentPath` has noRipple set
 *  on its outgoing trust-line direction.
 *
 *  A path that ends with a noRipple-out hop cannot be extended by another
 *  account node whose incoming side also has noRipple; the caller must query
 *  `AssetCache::getRippleLines()` with `LineDirection::Incoming` to respect
 *  this constraint.
 *
 *  Returns `false` for empty paths or paths whose last element is not an
 *  account node.
 *
 *  @param currentPath  The partial path being extended.
 *  @return `true` if noRipple is set on the last outgoing account link.
 */
bool
Pathfinder::isNoRippleOut(STPath const& currentPath)
{
    if (currentPath.empty())
        return false;

    STPathElement const& endElement = currentPath.back();
    if ((endElement.getNodeType() & STPathElement::TypeAccount) == 0u)
        return false;

    // For a single-element path the "from" account is the payment source.
    auto const& fromAccount =
        (currentPath.size() == 1) ? srcAccount_ : (currentPath.end() - 2)->getAccountID();
    auto const& toAccount = endElement.getAccountID();
    return endElement.hasCurrency() && isNoRipple(fromAccount, toAccount, endElement.getCurrency());
}

/** Append `path` to `pathSet` only if an equal path is not already present.
 *
 *  Prevents duplicate complete paths from accumulating in `completePaths_`.
 *
 *  @note This performs a linear scan, making the overall operation O(n) per
 *      insertion and O(n²) when called repeatedly — see TODO below.
 *
 *  @param pathSet  The set to append to.
 *  @param path     The candidate path to add.
 */
void
addUniquePath(STPathSet& pathSet, STPath const& path)
{
    // TODO(tom): building an STPathSet this way is quadratic in the size
    // of the STPathSet!
    for (auto const& p : pathSet)
    {
        if (p == path)
            return;
    }
    pathSet.pushBack(path);
}

/** Extend `currentPath` by one hop and collect the results.
 *
 *  This is the core graph-expansion step.  Depending on `addFlags` it either
 *  adds account hops (`kAF_ADD_ACCOUNTS`) or order-book hops (`kAF_ADD_BOOKS`):
 *
 *  **Account hops** (`kAF_ADD_ACCOUNTS`):
 *  - When the current endpoint holds XRP and the destination is XRP, the
 *    current path is complete.
 *  - Otherwise, enumerates trust-line peers (IOU) or MPT issuers (MPT) from
 *    `AssetCache`.  Peers are scored via `getPathsOut()` and collected as
 *    `AccountCandidates`.  The destination always gets `kHIGH_PRIORITY`.
 *  - Candidates are sorted by `compareAccountCandidate()` and trimmed: at most
 *    10 candidates from non-source accounts, 50 from the source account itself.
 *    When `kAF_AC_LAST` is set only the effective destination is considered.
 *  - noRipple awareness: if `isNoRippleOut()` is true, `AssetCache` is queried
 *    with `LineDirection::Incoming` to respect the noRipple constraint.
 *
 *  **Order-book hops** (`kAF_ADD_BOOKS`):
 *  - `kAF_OB_XRP`: adds a single XRP-out book hop if one exists from the
 *    current asset.
 *  - Otherwise, all books where TakerPays matches the current asset are
 *    enumerated.  For each, a book element is added; if the book output
 *    currency is XRP and the destination is XRP, the path completes; if the
 *    book output issuer is the effective destination, the path also completes.
 *    `kAF_OB_LAST` restricts books to those whose output matches
 *    the destination asset.
 *  - Consecutive `book → account → book` patterns are compacted: when the
 *    last two elements are an offer followed by an account, the account is
 *    replaced by the new book element to avoid redundant round-trips.
 *
 *  @param currentPath      The partial path to extend; empty means start from
 *      `source_`.
 *  @param incompletePaths  Accumulator for newly produced partial paths.
 *  @param addFlags         Bitmask: `kAF_ADD_ACCOUNTS`, `kAF_ADD_BOOKS`,
 *      `kAF_OB_XRP`, `kAF_OB_LAST`, `kAF_AC_LAST`.
 *  @param continueCallback Optional cooperative-cancellation probe.
 */
void
Pathfinder::addLink(
    STPath const& currentPath,   // The path to build from
    STPathSet& incompletePaths,  // The set of partial paths we add to
    int addFlags,
    std::function<bool(void)> const& continueCallback)
{
    auto const& pathEnd = currentPath.empty() ? source_ : currentPath.back();
    auto const& uEndPathAsset = pathEnd.getPathAsset();
    auto const& uEndIssuer = pathEnd.getIssuerID();
    auto const& uEndAccount = pathEnd.getAccountID();
    bool const bOnXRP = isXRP(uEndPathAsset);

    // Does pathfinding really need to get this to
    // a gateway (the issuer of the destination amount)
    // rather than the ultimate destination?
    bool const hasEffectiveDestination = effectiveDst_ != dstAccount_;

    JLOG(j_.trace()) << "addLink< flags=" << addFlags << " onXRP=" << bOnXRP
                     << " completePaths size=" << completePaths_.size();
    JLOG(j_.trace()) << currentPath.getJson(JsonOptions::Values::None);

    if ((addFlags & kAF_ADD_ACCOUNTS) != 0u)
    {
        if (bOnXRP)
        {
            if (dstAmount_.native() && !currentPath.empty())
            {
                JLOG(j_.trace()) << "complete path found ax: "
                                 << currentPath.getJson(JsonOptions::Values::None);
                addUniquePath(completePaths_, currentPath);
            }
        }
        else
        {
            auto const sleEnd = ledger_->read(keylet::account(uEndAccount));

            if (sleEnd)
            {
                bool const bRequireAuth((sleEnd->getFieldU32(sfFlags) & lsfRequireAuth) != 0u);
                bool const bIsEndAsset(uEndPathAsset == dstAmount_.asset());
                bool const bIsNoRippleOut(isNoRippleOut(currentPath));
                bool const bDestOnly((addFlags & kAF_AC_LAST) != 0u);

                AccountCandidates candidates;

                auto forAssets = [&]<typename AssetType>(AssetType const& assets) {
                    candidates.reserve(assets.size());

                    static bool constexpr kIS_LINE =
                        std::is_same_v<AssetType, std::vector<PathFindTrustLine>>;
                    static bool constexpr kIS_MPT =
                        std::is_same_v<AssetType, std::vector<PathFindMPT>>;

                    for (auto const& asset : assets)
                    {
                        if (continueCallback && !continueCallback())
                            return;
                        auto const& acct = [&]() constexpr {
                            if constexpr (kIS_LINE)
                                return asset.getAccountIDPeer();
                            // Unlike trustline, MPT is not bidirectional
                            if constexpr (kIS_MPT)
                                return getMPTIssuer(asset);
                        }();
                        auto const direction = [&]() constexpr -> LineDirection {
                            if constexpr (kIS_LINE)
                                return asset.getDirectionPeer();
                            // incoming for MPT since MPT doesn't support
                            // rippling (see LineDirection comments)
                            return LineDirection::Incoming;
                        }();

                        if (hasEffectiveDestination && (acct == dstAccount_))
                        {
                            // We skipped the gateway
                            continue;
                        }

                        bool const bToDestination = acct == effectiveDst_;

                        if (bDestOnly && !bToDestination)
                        {
                            continue;
                        }

                        auto const correctAsset = [&]() {
                            if constexpr (kIS_LINE)
                            {
                                return uEndPathAsset.get<Currency>() ==
                                    asset.getLimit().template get<Issue>().currency;
                            }
                            if constexpr (kIS_MPT)
                            {
                                return uEndPathAsset.get<MPTID>() == asset.getMptID();
                            }
                        }();
                        auto checkAsset = [&]() {
                            if constexpr (kIS_LINE)
                            {
                                return (
                                    (asset.getBalance() <= beast::kZERO &&
                                     (!asset.getLimitPeer() ||
                                      -asset.getBalance() >= asset.getLimitPeer() ||
                                      (bRequireAuth && !asset.getAuth()))) ||
                                    (bIsNoRippleOut && asset.getNoRipple()));
                            }
                            if constexpr (kIS_MPT)
                            {
                                return asset.isZeroBalance() || asset.isMaxedOut() ||
                                    requireAuth(*ledger_, MPTIssue{asset}, acct);
                            }
                        };

                        if (correctAsset && !currentPath.hasSeen(acct, uEndPathAsset, acct))
                        {
                            if (checkAsset())
                            {
                                // trust line / MPT holding is unusable (frozen, no balance, etc.)
                            }
                            else if (bToDestination)
                            {
                                if (uEndPathAsset == dstAmount_.asset())
                                {
                                    if (!currentPath.empty())
                                    {
                                        JLOG(j_.trace())
                                            << "complete path found ae: "
                                            << currentPath.getJson(JsonOptions::Values::None);
                                        addUniquePath(completePaths_, currentPath);
                                    }
                                }
                                else if (!bDestOnly)
                                {
                                    candidates.push_back({AccountCandidate::kHIGH_PRIORITY, acct});
                                }
                            }
                            else if (acct == srcAccount_)
                            {
                                // routing back to source would create a cycle
                            }
                            else
                            {
                                int const out = getPathsOut(
                                    uEndPathAsset,
                                    acct,
                                    direction,
                                    bIsEndAsset,
                                    effectiveDst_,
                                    continueCallback);
                                if (out != 0)
                                    candidates.push_back({out, acct});
                            }
                        }
                    }
                };

                uEndPathAsset.visit(
                    [&](Currency const&) {
                        if (auto const lines = rLCache_->getRippleLines(
                                uEndAccount,
                                bIsNoRippleOut ? LineDirection::Incoming : LineDirection::Outgoing))
                        {
                            forAssets(*lines);
                        }
                    },
                    [&](MPTID const&) {
                        if (auto const mpts = rLCache_->getMPTs(uEndAccount))
                        {
                            forAssets(*mpts);
                        }
                    });

                if (!candidates.empty())
                {
                    std::ranges::sort(
                        candidates,
                        std::bind(
                            compareAccountCandidate,
                            ledger_->seq(),
                            std::placeholders::_1,
                            std::placeholders::_2));

                    int count = candidates.size();
                    // Fan-out cap: more candidates allowed from the source itself.
                    if ((count > 10) && (uEndAccount != srcAccount_))
                    {
                        count = 10;
                    }
                    else if (count > 50)
                    {
                        count = 50;
                    }

                    auto it = candidates.begin();
                    while (count-- != 0)
                    {
                        if (continueCallback && !continueCallback())
                            return;
                        STPathElement const pathElement(
                            STPathElement::TypeAccount, it->account, uEndPathAsset, it->account);
                        incompletePaths.assembleAdd(currentPath, pathElement);
                        ++it;
                    }
                }
            }
            else
            {
                JLOG(j_.warn()) << "Path ends on non-existent issuer";
            }
        }
    }
    if ((addFlags & kAF_ADD_BOOKS) != 0u)
    {
        if ((addFlags & kAF_OB_XRP) != 0u)
        {
            if (!bOnXRP &&
                app_.getOrderBookDB().isBookToXRP(
                    assetFromPathAsset(uEndPathAsset, uEndIssuer), domain_))
            {
                STPathElement const pathElement(
                    STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount());
                incompletePaths.assembleAdd(currentPath, pathElement);
            }
        }
        else
        {
            bool const bDestOnly = (addFlags & kAF_OB_LAST) != 0;
            auto books = app_.getOrderBookDB().getBooksByTakerPays(
                assetFromPathAsset(uEndPathAsset, uEndIssuer), domain_);
            JLOG(j_.trace()) << books.size() << " books found from this currency/issuer";

            for (auto const& book : books)
            {
                if (continueCallback && !continueCallback())
                    return;
                if (!currentPath.hasSeen(xrpAccount(), book.out, book.out.getIssuer()) &&
                    !issueMatchesOrigin(book.out) &&
                    (!bDestOnly || equalTokens(book.out, dstAmount_.asset())))
                {
                    STPath newPath(currentPath);

                    if (isXRP(book.out))
                    {
                        newPath.emplaceBack(
                            STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount());

                        if (isXRP(dstAmount_.asset()))
                        {
                            JLOG(j_.trace()) << "complete path found bx: "
                                             << currentPath.getJson(JsonOptions::Values::None);
                            addUniquePath(completePaths_, newPath);
                        }
                        else
                        {
                            incompletePaths.pushBack(newPath);
                        }
                    }
                    else if (!currentPath.hasSeen(
                                 book.out.getIssuer(), book.out, book.out.getIssuer()))
                    {
                        auto const assetType = book.out.holds<Issue>() ? STPathElement::TypeCurrency
                                                                       : STPathElement::TypeMpt;
                        // Compact book → account → book: replace the intermediate
                        // account node with this book element to avoid redundant hops.
                        if ((newPath.size() >= 2) && (newPath.back().isAccount()) &&
                            (newPath[newPath.size() - 2].isOffer()))
                        {
                            newPath[newPath.size() - 1] = STPathElement(
                                assetType | STPathElement::TypeIssuer,
                                xrpAccount(),
                                book.out,
                                book.out.getIssuer());
                        }
                        else
                        {
                            newPath.emplaceBack(
                                assetType | STPathElement::TypeIssuer,
                                xrpAccount(),
                                book.out,
                                book.out.getIssuer());
                        }

                        if (hasEffectiveDestination && book.out.getIssuer() == dstAccount_ &&
                            equalTokens(book.out, dstAmount_.asset()))
                        {
                            // Must route through the gateway (effectiveDst_), not directly
                            // to dstAccount_; skip this path branch.
                        }
                        else if (
                            book.out.getIssuer() == effectiveDst_ &&
                            equalTokens(book.out, dstAmount_.asset()))
                        {
                            JLOG(j_.trace()) << "complete path found ba: "
                                             << currentPath.getJson(JsonOptions::Values::None);
                            addUniquePath(completePaths_, newPath);
                        }
                        else
                        {
                            incompletePaths.assembleAdd(
                                newPath,
                                STPathElement(
                                    STPathElement::TypeAccount,
                                    book.out.getIssuer(),
                                    book.out,
                                    book.out.getIssuer()));
                        }
                    }
                }
            }
        }
    }
}

namespace {

/** Decode a compact path-shape string into a `PathType` vector.
 *
 *  Each character maps to a `NodeType`:
 *  - `'s'` → `Source`
 *  - `'a'` → `Accounts`
 *  - `'b'` → `Books`
 *  - `'x'` → `XrpBook`
 *  - `'f'` → `DestBook`
 *  - `'d'` → `Destination`
 *
 *  Parsing stops at the NUL terminator.  Unrecognised characters are silently
 *  ignored (handled by the `switch` default fall-through).
 *
 *  @param string  NUL-terminated path-shape string, e.g. `"sfad"`.
 *  @return        The corresponding `PathType` vector.
 */
Pathfinder::PathType
makePath(char const* string)
{
    Pathfinder::PathType ret;

    while (true)
    {
        // NOLINTNEXTLINE(bugprone-switch-missing-default-case)
        switch (*string++)
        {
            case 's':  // source
                ret.push_back(Pathfinder::NodeType::Source);
                break;

            case 'a':  // accounts
                ret.push_back(Pathfinder::NodeType::Accounts);
                break;

            case 'b':  // books
                ret.push_back(Pathfinder::NodeType::Books);
                break;

            case 'x':  // xrp book
                ret.push_back(Pathfinder::NodeType::XrpBook);
                break;

            case 'f':  // book to final currency
                ret.push_back(Pathfinder::NodeType::DestBook);
                break;

            case 'd':
                // Destination (with account, if required and not already
                // present).
                ret.push_back(Pathfinder::NodeType::Destination);
                break;

            case 0:
                return ret;
        }
    }
}

/** Decode a `PathCostList` and append the resulting `CostedPath` entries to
 *  `gPathTable[type]`.
 *
 *  Called once per `PaymentType` during `Pathfinder::initPathTable()`.
 *  Asserts that the table entry is initially empty to catch double-init bugs.
 *
 *  @param type   The payment category to populate.
 *  @param costs  Ordered list of `{cost, path-string}` entries.
 */
void
fillPaths(Pathfinder::PaymentType type, PathCostList const& costs)
{
    auto& list = gPathTable[type];
    XRPL_ASSERT(list.empty(), "xrpl::fillPaths : empty paths");
    for (auto& cost : costs)
        list.push_back({cost.cost, makePath(cost.path)});
}

}  // namespace

/** Populate `gPathTable` with all known payment-route shapes.
 *
 *  Must be called once at startup before any `Pathfinder` is constructed.
 *  Clears and refills the global table; calling it again is safe (used in
 *  tests).
 *
 *  Search-level cost semantics:
 *  - **0** — minimum set needed to make some payments possible.
 *  - **1** — trivial paths covering the most common cases.
 *  - **4** — normal fast search.
 *  - **7** — normal slow search.
 *  - **10** — most aggressive; exhaustive exploration.
 *
 *  @note Do not add rules that reproduce the implicit default path (a direct
 *      trust-line transfer without intermediate hops); such paths are handled
 *      separately by `computePathRanks()`.
 */
void
Pathfinder::initPathTable()
{
    gPathTable.clear();
    fillPaths(PaymentType::XrpToXrp, {});
    /* cspell: disable */

    fillPaths(
        PaymentType::XrpToNonXrp,
        {{1, "sfd"},    // source -> book -> gateway
         {3, "sfad"},   // source -> book -> account -> destination
         {5, "sfaad"},  // source -> book -> account -> account -> destination
         {6, "sbfd"},   // source -> book -> book -> destination
         {8, "sbafd"},  // source -> book -> account -> book -> destination
         {9, "sbfad"},  // source -> book -> book -> account -> destination
         {10, "sbafad"}});

    fillPaths(
        PaymentType::NonXrpToXrp,
        {{1, "sxd"},   // gateway buys XRP
         {2, "saxd"},  // source -> gateway -> book(XRP) -> dest
         {6, "saaxd"},
         {7, "sbxd"},
         {8, "sabxd"},
         {9, "sabaxd"}});

    // non-XRP to non-XRP (same currency)
    fillPaths(
        PaymentType::NonXrpToSame,
        {
            {1, "sad"},   // source -> gateway -> destination
            {1, "sfd"},   // source -> book -> destination
            {4, "safd"},  // source -> gateway -> book -> destination
            {4, "sfad"},
            {5, "saad"},
            {5, "sbfd"},
            {6, "sxfad"},
            {6, "safad"},
            {6, "saxfd"},  // source -> gateway -> book to XRP -> book ->
                           // destination
            {6, "saxfad"},
            {6, "sabfd"},  // source -> gateway -> book -> book -> destination
            {7, "saaad"},
        });

    // non-XRP to non-XRP (different currency)
    fillPaths(
        PaymentType::NonXrpToNonXrp,
        {
            {1, "sfad"},
            {1, "safd"},
            {3, "safad"},
            {4, "sxfd"},
            {5, "saxfd"},
            {5, "sxfad"},
            {5, "sbfd"},
            {6, "saxfad"},
            {6, "sabfd"},
            {7, "saafd"},
            {8, "saafad"},
            {9, "safaad"},
        });
    /* cspell: enable */
}

}  // namespace xrpl
