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

/*

Core Pathfinding Engine

The pathfinding request is identified by category, XRP to XRP, XRP to
non-XRP, non-XRP to XRP, same currency non-XRP to non-XRP, cross-currency
non-XRP to non-XRP.  For each category, there is a table of paths that the
pathfinder searches for.  Complete paths are collected.

Each complete path is then rated and sorted. Paths with no or trivial
liquidity are dropped.  Otherwise, paths are sorted based on quality,
liquidity, and path length.

Path slots are filled in quality (ratio of out to in) order, with the
exception that the last path must have enough liquidity to complete the
payment (assuming no liquidity overlap).  In addition, if no selected path
is capable of providing enough liquidity to complete the payment by itself,
an extra "covering" path is returned.

The selected paths are then tested to determine if they can complete the
payment and, if so, at what cost.  If they fail and a covering path was
found, the test is repeated with the covering path.  If this succeeds, the
final paths and the estimated cost are returned.

The engine permits the search depth to be selected and the paths table
includes the depth at which each path type is found.  A search depth of zero
causes no searching to be done.  Extra paths can also be injected, and this
should be used to preserve previously-found paths across invocations for the
same path request (particularly if the search depth may change).

*/

namespace xrpl {

namespace {

// This is an arbitrary cutoff, and it might cause us to miss other
// good paths with this arbitrary cut off.
constexpr std::size_t kPathfinderMaxCompletePaths = 1000;

struct AccountCandidate
{
    int priority;
    AccountID account;

    static int const kHighPriority = 10000;
};

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

struct CostedPath
{
    int searchLevel;
    Pathfinder::PathType type;
};

using CostedPathList = std::vector<CostedPath>;

using PathTable = std::map<Pathfinder::PaymentType, CostedPathList>;

struct PathCost
{
    int cost;
    char const* path;
};
using PathCostList = std::vector<PathCost>;

PathTable gPathTable;

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

// Return the smallest amount of useful liquidity for a given amount, and the
// total number of paths we have to evaluate.
STAmount
smallestUsefulAmount(STAmount const& amount, int maxPaths)
{
    return divide(amount, STAmount(maxPaths + 2), amount.asset());
}

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

Asset
assetFromPathAsset(PathAsset const& pathAsset, AccountID const& account)
{
    return pathAsset.visit(
        [&](Currency const& currency) { return Asset{Issue{currency, account}}; },
        [](MPTID const& mpt) { return Asset{mpt}; });
}

}  // namespace

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
    , convertAll_(convertAllCheck(dstAmount_))
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

bool
Pathfinder::findPaths(int searchLevel, std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.trace()) << "findPaths start";
    if (dstAmount_ == beast::kZero)
    {
        // No need to send zero money.
        JLOG(j_.debug()) << "Destination amount was zero.";
        ledger_.reset();
        return false;

        // TODO(tom): why do we reset the ledger just in this case and the one
        // below - why don't we do it each time we return false?
    }

    if (srcAccount_ == dstAccount_ && dstAccount_ == effectiveDst_ &&
        srcPathAsset_ == dstAmount_.asset())
    {
        // No need to send to same account with same currency.
        JLOG(j_.debug()) << "Tried to send to same issuer";
        ledger_.reset();
        return false;
    }

    if (srcAccount_ == effectiveDst_ && srcPathAsset_ == dstAmount_.asset())
    {
        // Default path might work, but any path would loop
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

    // Now compute the payment type from the types of the source and destination
    // currencies.
    PaymentType paymentType = PaymentType::XrpToXrp;
    if (bSrcXrp && bDstXrp)
    {
        // XRP -> XRP
        JLOG(j_.debug()) << "XRP to XRP payment";
        paymentType = PaymentType::XrpToXrp;
    }
    else if (bSrcXrp)
    {
        // XRP -> non-XRP
        JLOG(j_.debug()) << "XRP to non-XRP payment";
        paymentType = PaymentType::XrpToNonXrp;
    }
    else if (bDstXrp)
    {
        // non-XRP -> XRP
        JLOG(j_.debug()) << "non-XRP to XRP payment";
        paymentType = PaymentType::NonXrpToXrp;
    }
    else if (srcPathAsset_ == dstAmount_.asset())
    {
        // non-XRP -> non-XRP - Same currency
        JLOG(j_.debug()) << "non-XRP to non-XRP - same currency";
        paymentType = PaymentType::NonXrpToSame;
    }
    else
    {
        // non-XRP to non-XRP - Different currency
        JLOG(j_.debug()) << "non-XRP to non-XRP - cross currency";
        paymentType = PaymentType::NonXrpToNonXrp;
    }

    // Now iterate over all paths for that paymentType.
    for (auto const& costedPath : gPathTable[paymentType])
    {
        if (continueCallback && !continueCallback())
            return false;
        // Only use paths with at most the current search level.
        if (costedPath.searchLevel <= searchLevel)
        {
            JLOG(j_.trace()) << "findPaths trying payment type " << paymentType;
            addPathsForType(costedPath.type, continueCallback);

            if (completePaths_.size() > kPathfinderMaxCompletePaths)
                break;
        }
    }

    JLOG(j_.debug()) << completePaths_.size() << " complete paths found";

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

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
        // Compute a path that provides at least the minimum liquidity.
        if (convertAll_)
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
        // If we can't get even the minimum liquidity requested, we're done.
        if (!isTesSuccess(rc.result()))
            return rc.result();

        qualityOut = getRate(rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        if (!convertAll_)
        {
            // Now try to compute the remaining liquidity.
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

            // If we found further liquidity, add it into the result.
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

void
Pathfinder::computePathRanks(int maxPaths, std::function<bool(void)> const& continueCallback)
{
    remainingAmount_ = convertAmount(dstAmount_, convertAll_);

    // Must subtract liquidity in default path from remaining amount.
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

// For each useful path in the input path set,
// create a ranking entry in the output vector of path ranks
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
        if (!convertAll_)
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

    // Sort paths by:
    //    cost of path (when considering quality)
    //    width of path
    //    length of path
    // A better PathRank is lower, best are sorted to the beginning.
    std::ranges::sort(
        rankedPaths, [&](Pathfinder::PathRank const& a, Pathfinder::PathRank const& b) {
            // 1) Higher quality (lower cost) is better
            if (!convertAll_ && a.quality != b.quality)
                return a.quality < b.quality;

            // 2) More liquidity (higher volume) is better
            if (a.liquidity != b.liquidity)
                return a.liquidity > b.liquidity;

            // 3) Shorter paths are better
            if (a.length != b.length)
                return a.length < b.length;

            // 4) Tie breaker
            return a.index > b.index;
        });
}

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

    // The best PathRanks are now at the start.  Pull off enough of them to
    // fill bestPaths, then look through the rest for the best individual
    // path that can satisfy the entire liquidity - if one exists.
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
            // Risk is high they have identical liquidity
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
        // last path must fill
        {
            --iPathsLeft;
            remaining -= pathRank.liquidity;
            bestPaths.pushBack(startsWithIssuer ? removeIssuer(path) : path);
        }
        else if (iPathsLeft == 0 && pathRank.liquidity >= dstAmount_ && fullLiquidityPath.empty())
        {
            // We found an extra path that can move the whole amount.
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

    if (remaining > beast::kZero)
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

bool
Pathfinder::issueMatchesOrigin(Asset const& asset)
{
    bool const matchingAsset = (asset == srcPathAsset_);
    bool const matchingAccount = isXRP(asset) || (srcIssuer_ && asset.getIssuer() == srcIssuer_) ||
        asset.getIssuer() == srcAccount_;

    return matchingAsset && matchingAccount;
}

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
                            rspEntry.getBalance() <= beast::kZero &&
                            (!rspEntry.getLimitPeer() ||
                             -rspEntry.getBalance() >= rspEntry.getLimitPeer() ||
                             (bAuthRequired && !rspEntry.getAuth())))
                        {
                        }
                        else if (isDstAsset && dstAccount == rspEntry.getAccountIDPeer())
                        {
                            count += 10000;  // count a path to the destination extra
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

STPathSet&
Pathfinder::addPathsForType(
    PathType const& pathType,
    std::function<bool(void)> const& continueCallback)
{
    JLOG(j_.debug()) << "addPathsForType " << CollectionAndDelimiter(pathType, ", ");
    // See if the set of paths for this type already exists.
    auto it = paths_.find(pathType);
    if (it != paths_.end())
        return it->second;

    // Otherwise, if the type has no nodes, return the empty path.
    if (pathType.empty())
        return paths_[pathType];
    if (continueCallback && !continueCallback())
        return paths_[{}];

    // Otherwise, get the paths for the parent PathType by calling
    // addPathsForType recursively.
    PathType parentPathType = pathType;
    parentPathType.pop_back();

    STPathSet const& parentPaths = addPathsForType(parentPathType, continueCallback);
    STPathSet& pathsOut = paths_[pathType];

    JLOG(j_.debug()) << "getPaths< adding onto '" << pathTypeToString(parentPathType)
                     << "' to get '" << pathTypeToString(pathType) << "'";

    int const initialSize = completePaths_.size();

    // Add the last NodeType to the lists.
    auto nodeType = pathType.back();
    switch (nodeType)
    {
        case NodeType::Source:
            // Source must always be at the start, so pathsOut has to be empty.
            XRPL_ASSERT(pathsOut.empty(), "xrpl::Pathfinder::addPathsForType : empty paths");
            pathsOut.pushBack(STPath());
            break;

        case NodeType::Accounts:
            addLinks(parentPaths, pathsOut, kAfAddAccounts, continueCallback);
            break;

        case NodeType::Books:
            addLinks(parentPaths, pathsOut, kAfAddBooks, continueCallback);
            break;

        case NodeType::XrpBook:
            addLinks(parentPaths, pathsOut, kAfAddBooks | kAfObXrp, continueCallback);
            break;

        case NodeType::DestBook:
            addLinks(parentPaths, pathsOut, kAfAddBooks | kAfObLast, continueCallback);
            break;

        case NodeType::Destination:
            // FIXME: What if a different issuer was specified on the
            // destination amount?
            // TODO(tom): what does this even mean?  Should it be a JIRA?
            addLinks(parentPaths, pathsOut, kAfAddAccounts | kAfAcLast, continueCallback);
            break;
    }

    if (completePaths_.size() != initialSize)
    {
        JLOG(j_.debug()) << (completePaths_.size() - initialSize) << " complete paths added";
    }

    JLOG(j_.debug()) << "getPaths> " << pathsOut.size() << " partial paths found";
    return pathsOut;
}

bool
Pathfinder::isNoRipple(
    AccountID const& fromAccount,
    AccountID const& toAccount,
    Currency const& currency)
{
    auto sleRipple = ledger_->read(keylet::line(toAccount, fromAccount, currency));

    auto const flag((toAccount > fromAccount) ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && sleRipple->isFlag(flag);
}

// Does this path end on an account-to-account link whose last account has
// set "no ripple" on the link?
bool
Pathfinder::isNoRippleOut(STPath const& currentPath)
{
    // Must have at least one link.
    if (currentPath.empty())
        return false;

    // Last link must be an account.
    STPathElement const& endElement = currentPath.back();
    if ((endElement.getNodeType() & STPathElement::TypeAccount) == 0u)
        return false;

    // If there's only one item in the path, return true if that item specifies
    // no ripple on the output. A path with no ripple on its output can't be
    // followed by a link with no ripple on its input.
    auto const& fromAccount =
        (currentPath.size() == 1) ? srcAccount_ : (currentPath.end() - 2)->getAccountID();
    auto const& toAccount = endElement.getAccountID();
    return endElement.hasCurrency() && isNoRipple(fromAccount, toAccount, endElement.getCurrency());
}

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

    if ((addFlags & kAfAddAccounts) != 0u)
    {
        // add accounts
        if (bOnXRP)
        {
            if (dstAmount_.native() && !currentPath.empty())
            {  // non-default path to XRP destination
                JLOG(j_.trace()) << "complete path found ax: "
                                 << currentPath.getJson(JsonOptions::Values::None);
                addUniquePath(completePaths_, currentPath);
            }
        }
        else
        {
            // search for accounts to add
            auto const sleEnd = ledger_->read(keylet::account(uEndAccount));

            if (sleEnd)
            {
                bool const bRequireAuth(sleEnd->isFlag(lsfRequireAuth));
                bool const bIsEndAsset(uEndPathAsset == dstAmount_.asset());
                bool const bIsNoRippleOut(isNoRippleOut(currentPath));
                bool const bDestOnly((addFlags & kAfAcLast) != 0u);

                AccountCandidates candidates;

                auto forAssets = [&]<typename AssetType>(AssetType const& assets) {
                    candidates.reserve(assets.size());

                    static constexpr bool kIsLine =
                        std::is_same_v<AssetType, std::vector<PathFindTrustLine>>;
                    static constexpr bool kIsMpt =
                        std::is_same_v<AssetType, std::vector<PathFindMPT>>;

                    for (auto const& asset : assets)
                    {
                        if (continueCallback && !continueCallback())
                            return;
                        auto const& acct = [&]() constexpr {
                            if constexpr (kIsLine)
                                return asset.getAccountIDPeer();
                            // Unlike trustline, MPT is not bidirectional
                            if constexpr (kIsMpt)
                                return getMPTIssuer(asset);
                        }();
                        auto const direction = [&]() constexpr -> LineDirection {
                            if constexpr (kIsLine)
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
                            if constexpr (kIsLine)
                            {
                                return uEndPathAsset.get<Currency>() ==
                                    asset.getLimit().template get<Issue>().currency;
                            }
                            if constexpr (kIsMpt)
                            {
                                return uEndPathAsset.get<MPTID>() == asset.getMptID();
                            }
                        }();
                        auto checkAsset = [&]() {
                            if constexpr (kIsLine)
                            {
                                return (
                                    (asset.getBalance() <= beast::kZero &&
                                     (!asset.getLimitPeer() ||
                                      -asset.getBalance() >= asset.getLimitPeer() ||
                                      (bRequireAuth && !asset.getAuth()))) ||
                                    (bIsNoRippleOut && asset.getNoRipple()));
                            }
                            if constexpr (kIsMpt)
                            {
                                return asset.isZeroBalance() || asset.isMaxedOut() ||
                                    requireAuth(*ledger_, MPTIssue{asset}, acct);
                            }
                        };

                        if (correctAsset && !currentPath.hasSeen(acct, uEndPathAsset, acct))
                        {
                            // path is for correct currency and has not been
                            // seen
                            if (checkAsset())
                            {
                                // Can't leave on this path
                            }
                            else if (bToDestination)
                            {
                                // destination is always worth trying
                                if (uEndPathAsset == dstAmount_.asset())
                                {
                                    // this is a complete path
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
                                    // this is a high-priority candidate
                                    candidates.push_back({AccountCandidate::kHighPriority, acct});
                                }
                            }
                            else if (acct == srcAccount_)
                            {
                                // going back to the source is bad
                            }
                            else
                            {
                                // save this candidate
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
                    // allow more paths from source
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
                        // Add accounts to incompletePaths
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
    if ((addFlags & kAfAddBooks) != 0u)
    {
        // add order books
        if ((addFlags & kAfObXrp) != 0u)
        {
            // to XRP only
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
            bool const bDestOnly = (addFlags & kAfObLast) != 0;
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
                    {  // to XRP

                        // add the order book itself
                        newPath.emplaceBack(
                            STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount());

                        if (isXRP(dstAmount_.asset()))
                        {
                            // destination is XRP, add account and path is
                            // complete
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
                        // Don't want the book if we've already seen the issuer
                        // book -> account -> book
                        if ((newPath.size() >= 2) && (newPath.back().isAccount()) &&
                            (newPath[newPath.size() - 2].isOffer()))
                        {
                            // replace the redundant account with the order book
                            newPath[newPath.size() - 1] = STPathElement(
                                assetType | STPathElement::TypeIssuer,
                                xrpAccount(),
                                book.out,
                                book.out.getIssuer());
                        }
                        else
                        {
                            // add the order book
                            newPath.emplaceBack(
                                assetType | STPathElement::TypeIssuer,
                                xrpAccount(),
                                book.out,
                                book.out.getIssuer());
                        }

                        if (hasEffectiveDestination && book.out.getIssuer() == dstAccount_ &&
                            equalTokens(book.out, dstAmount_.asset()))
                        {
                            // We skipped a required issuer
                        }
                        else if (
                            book.out.getIssuer() == effectiveDst_ &&
                            equalTokens(book.out, dstAmount_.asset()))
                        {  // with the destination account, this path is
                           // complete
                            JLOG(j_.trace()) << "complete path found ba: "
                                             << currentPath.getJson(JsonOptions::Values::None);
                            addUniquePath(completePaths_, newPath);
                        }
                        else
                        {
                            // add issuer's account, path still incomplete
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

void
fillPaths(Pathfinder::PaymentType type, PathCostList const& costs)
{
    auto& list = gPathTable[type];
    XRPL_ASSERT(list.empty(), "xrpl::fillPaths : empty paths");
    for (auto& cost : costs)
        list.push_back({cost.cost, makePath(cost.path)});
}

}  // namespace

// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most aggressive

void
Pathfinder::initPathTable()
{
    // CAUTION: Do not include rules that build default paths

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
