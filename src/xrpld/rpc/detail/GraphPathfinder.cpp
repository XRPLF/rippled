#include <xrpld/rpc/detail/GraphPathfinder.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/PathfinderUtils.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/RippleCalc.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

//==============================================================================
// Construction
//==============================================================================

GraphPathfinder::GraphPathfinder(
    std::shared_ptr<PayGraph> const& graph,
    std::shared_ptr<AssetCache> const& cache,
    AccountID const& srcAccount,
    AccountID const& dstAccount,
    PathAsset const& srcPathAsset,
    std::optional<AccountID> const& srcIssuer,
    STAmount const& dstAmount,
    std::optional<STAmount> const& /*srcAmount unused — derived from graph*/,
    std::optional<uint256> const& domain,
    Application& app)
    : graph_(graph)
    , snap_(graph ? graph->snapshot() : nullptr)
    , srcAccount_(srcAccount)
    , dstAccount_(dstAccount)
    , effectiveDst_(isXRP(dstAmount.getIssuer()) ? dstAccount : dstAmount.getIssuer())
    , dstAmount_(dstAmount)
    , srcPathAsset_(srcPathAsset)
    , srcIssuer_(srcIssuer)
    , srcAmount_(srcPathAsset.visit(
          [&](Currency const& currency) {
              auto const& account = srcIssuer.value_or(isXRP(currency) ? xrpAccount() : srcAccount);
              return STAmount(Issue{currency, account}, 1u, 0, true);
          },
          [](MPTID const& mpt) { return STAmount(mpt, 1u, 0, true); }))
    , convertAll_(convertAllCheck(dstAmount))
    , domain_(domain)
    , ledger_(cache->getLedger())
    , cache_(cache)
    , app_(app)
    , j_(app.getJournal("GraphPathfinder"))
{
}

//==============================================================================
// findPaths
//
// Phase 1: run Yen's K-Shortest on the asset-exchange graph (microseconds).
// Phase 2: materialise each abstract path into a concrete STPath.
//==============================================================================

bool
GraphPathfinder::findPaths(std::function<bool()> const& continueCallback)
{
    if (!snap_ || !ledger_)
    {
        JLOG(j_.debug()) << "GraphPathfinder: no snapshot or ledger";
        return false;
    }

    if (dstAmount_ == beast::kZero)
    {
        JLOG(j_.debug()) << "GraphPathfinder: zero destination amount";
        return false;
    }

    if (srcAccount_ == dstAccount_ && srcPathAsset_ == dstAmount_.asset())
    {
        JLOG(j_.debug()) << "GraphPathfinder: trivial same-account same-asset";
        return true;
    }

    // Resolve source and destination assets.
    Asset const srcAsset = srcPathAsset_.visit(
        [&](Currency const& c) -> Asset {
            AccountID const& iss = srcIssuer_.value_or(isXRP(c) ? xrpAccount() : srcAccount_);
            return Issue{c, iss};
        },
        [](MPTID const& mpt) -> Asset { return MPTIssue{mpt}; });

    Asset const dstAsset = dstAmount_.asset();

    // Find abstract paths on the asset graph.
    // kMaxK = 6: we want at most 6 paths for the subscriber.
    static constexpr int kMaxK = 6;

    // Resolve graph vertex IDs for source and destination.
    //
    // The exact lookup by {currency, issuer} may fail for non-XRP sources when
    // the sender's account is used as the issuer (e.g. when PathAsset drops the
    // gateway issuer).  For the source, fall back to a currency-only search
    // constrained to issuers that the source account actually holds trust lines
    // for — this prevents false-positive paths (e.g. treating g2["HKD"] as
    // valid source when the account only holds g1["HKD"]).
    //
    // For the destination we do an unconstrained currency-only fallback: the
    // suffix logic in materialise() handles the trust-line hops from the OB
    // gateway to the actual destination account, so we must include all OB
    // vertices for that currency.
    auto findVIDsSrc = [&](Asset const& asset) -> std::vector<PayGraph::VID> {
        auto it = snap_->index.find(asset);
        if (it != snap_->index.end())
            return {it->second};

        if (asset.holds<Issue>() && !isXRP(asset.get<Issue>().currency))
        {
            Currency const& cur = asset.get<Issue>().currency;

            // Collect issuers that srcAccount_ has trust lines for.
            hash_set<AccountID> validIssuers;
            if (auto const lines = cache_->getRippleLines(srcAccount_, LineDirection::Outgoing))
            {
                for (auto const& line : *lines)
                {
                    if (line.getBalance().get<Issue>().currency != cur)
                        continue;
                    validIssuers.insert(line.getAccountIDPeer());
                }
            }

            std::vector<PayGraph::VID> vids;
            for (auto const& [a, vid] : snap_->index)
            {
                if (!a.holds<Issue>() || isXRP(a.get<Issue>().currency))
                    continue;
                if (a.get<Issue>().currency != cur)
                    continue;
                if (!validIssuers.empty() && !validIssuers.contains(a.get<Issue>().account))
                    continue;
                vids.push_back(vid);
            }
            return vids;
        }
        return {};
    };

    auto findVIDsDst = [&](Asset const& asset) -> std::vector<PayGraph::VID> {
        auto it = snap_->index.find(asset);
        if (it != snap_->index.end())
            return {it->second};

        // Unconstrained currency-only fallback for destination — materialise()
        // suffix adds the intermediate trust-line hops to dstAccount_.
        if (asset.holds<Issue>() && !isXRP(asset.get<Issue>().currency))
        {
            Currency const& cur = asset.get<Issue>().currency;
            std::vector<PayGraph::VID> vids;
            for (auto const& [a, vid] : snap_->index)
            {
                if (a.holds<Issue>() && !isXRP(a.get<Issue>().currency) &&
                    a.get<Issue>().currency == cur)
                {
                    vids.push_back(vid);
                }
            }
            return vids;
        }
        return {};
    };

    auto const srcVIDs = findVIDsSrc(srcAsset);
    auto const dstVIDs = findVIDsDst(dstAsset);

    // When the effective destination is the sender AND the source/destination
    // assets are the same (a genuine self-loop on a single asset), Yen's would
    // search for a path from a vertex back to itself — skip it.  Cross-currency
    // self-payments (e.g. bob holds XTS@gw and wants XXX@gw) still need
    // PayGraph traversal to discover the gateway-side conversion route.
    bool const repayToSelf = !isXRP(dstAmount_.asset()) && (effectiveDst_ == srcAccount_) &&
        (srcPathAsset_ == dstAmount_.asset());

    if (repayToSelf || srcVIDs.empty() || dstVIDs.empty())
    {
        JLOG(j_.debug()) << "GraphPathfinder: "
                         << (repayToSelf ? "repay-to-self, skipping PayGraph"
                                         : "src or dst asset not in graph");
    }
    else
    {
        // Yen's k-shortest paths between every (srcVID, dstVID) pair, sorted
        // by ascending cumQuality.  Materialise into concrete STPaths and
        // dedupe.
        //
        // Light oversample: rankPaths() runs expensive rippleCalculate per
        // candidate (and AMM overflows throw FlowException with stack dumps).
        // Keep the probe set small so startup/updateAll stays bounded.
        static constexpr int kOversample = 2;
        std::vector<PayGraph::AssetPath> candidates;
        for (PayGraph::VID const vSrc : srcVIDs)
        {
            for (PayGraph::VID const vDst : dstVIDs)
            {
                if (vSrc == vDst)
                    continue;
                auto paths =
                    PayGraph::kShortestPaths(*snap_, vSrc, vDst, kMaxK * kOversample, dstAmount_);
                for (auto& p : paths)
                    candidates.push_back(std::move(p));
            }
        }

        std::ranges::stable_sort(
            candidates, [](auto const& a, auto const& b) { return a.cumQuality < b.cumQuality; });

        int accepted = 0;
        // Materialise at most kMaxK concrete paths; ranking will probe these.
        int const acceptCap = kMaxK;
        for (auto const& ap : candidates)
        {
            if (accepted >= acceptCap)
                break;
            if (continueCallback && !continueCallback())
                return !completePaths_.empty();

            // Broken AMMs are excluded inside BookStep::getAMMOffer after a
            // single FlowException (noteFailedAMM).  Do not skip entire asset
            // paths here — CLOB liquidity on the same book must stay eligible.

            auto concrete = materialise(ap);
            if (!concrete || concrete->empty())
                continue;

            bool dup = false;
            for (auto const& existing : completePaths_)
            {
                if (existing == *concrete)
                {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;

            completePaths_.pushBack(*concrete);
            ++accepted;
        }

        JLOG(j_.debug()) << "GraphPathfinder: " << candidates.size()
                         << " abstract paths considered, " << accepted
                         << " concrete paths accepted (cap=" << acceptCap << ")";
    }

    // Also try the direct (no-bridge) path: src -> dst over trust lines.
    // This covers same-currency IOUs (e.g. sender holds USD from issuer A,
    // dest wants USD from issuer A) that don't go through any order book.
    {
        STPath const directPath;
        // For non-XRP to non-XRP same-currency: no intermediate nodes needed;
        // the path engine will use the default path.  We still emit an empty
        // path as a hint so rippleCalculate considers it.
        // (An empty completePath_ entry signals "try the default path".)
        if (srcPathAsset_ == dstAmount_.asset() && !isXRP(srcPathAsset_))
        {
            // Direct ripple: no path nodes needed.
            completePaths_.pushBack(directPath);
        }
    }

    // Phase 2: trust-line rippling paths.
    //
    // PayGraph only models order-book and AMM edges. Pure trust-line rippling
    // paths (e.g. alice → gateway → bob all in the same currency) require a
    // separate discovery step.
    //
    // Only applicable when the source payment asset is the same IOU currency
    // as the destination — i.e. the sender already holds the target IOU and
    // can ripple it through trust-line intermediaries.  When the source asset
    // is XRP or a different currency, the payment must cross an order book
    // first; trust-line paths would be invalid.
    //
    // We use a bidirectional fan-out: load trust lines only for srcAccount_ and
    // dstAccount_ (typically 5–20 lines each for regular users), then intersect
    // their peer sets to find 1-hop intermediaries. For 2-hop paths we probe
    // specific (A, B) pairs using O(log N) SHAMap point-lookups — we never
    // iterate over a gateway account's trust lines, which can number in the
    // millions.
    bool const srcIsTargetCcy = srcPathAsset_.holds<Currency>() &&
        !isXRP(srcPathAsset_.get<Currency>()) && dstAmount_.asset().holds<Issue>() &&
        !isXRP(dstAmount_.asset().get<Issue>().currency) &&
        srcPathAsset_.get<Currency>() == dstAmount_.asset().get<Issue>().currency;

    if (srcIsTargetCcy)
    {
        Currency const& targetCcy = dstAmount_.asset().get<Issue>().currency;

        auto const srcLines = cache_->getRippleLines(srcAccount_, LineDirection::Outgoing);
        auto const dstLines = cache_->getRippleLines(dstAccount_, LineDirection::Outgoing);

        if (srcLines && dstLines)
        {
            // Collect src peers in targetCcy (exclude frozen lines and the
            // src/dst accounts themselves).
            hash_set<AccountID> srcPeers;
            for (auto const& line : *srcLines)
            {
                if (line.getBalance().get<Issue>().currency != targetCcy)
                    continue;
                if (line.getFreeze() || line.getDeepFreeze())
                    continue;
                AccountID const& peer = line.getAccountIDPeer();
                if (peer != srcAccount_ && peer != dstAccount_)
                    srcPeers.insert(peer);
            }

            // Collect dst peers in targetCcy.
            hash_set<AccountID> dstPeers;
            for (auto const& line : *dstLines)
            {
                if (line.getBalance().get<Issue>().currency != targetCcy)
                    continue;
                if (line.getFreeze() || line.getDeepFreeze())
                    continue;
                AccountID const& peer = line.getAccountIDPeer();
                if (peer != srcAccount_ && peer != dstAccount_)
                    dstPeers.insert(peer);
            }

            // 1-hop: src → I → dst
            // I must be in both srcPeers and dstPeers.
            for (auto const& i : srcPeers)
            {
                if (continueCallback && !continueCallback())
                    return !completePaths_.empty();
                if (dstPeers.contains(i))
                {
                    STPath path;
                    path.emplaceBack(STPathElement::TypeAccount, i, xrpCurrency(), xrpAccount());
                    completePaths_.pushBack(path);
                }
            }

            // 2-hop: src → A → B → dst
            // A ∈ srcPeers, B ∈ dstPeers, A ≠ B.
            // We probe the A–B trust line via a single SHAMap point-lookup
            // (O(log N)) rather than loading A's full trust-line list.
            // Cap at kMaxHop2Probes to bound cost when src or dst is itself a
            // gateway with many same-currency peers.
            [&] {
                static constexpr std::size_t kMaxHop2Probes = 100;
                std::size_t probes = 0;
                for (auto const& a : srcPeers)
                {
                    if (continueCallback && !continueCallback())
                        return;
                    for (auto const& b : dstPeers)
                    {
                        if (probes++ >= kMaxHop2Probes)
                            return;
                        if (a == b)
                            continue;
                        if (ledger_->read(keylet::trustLine(a, b, targetCcy)))
                        {
                            STPath path;
                            path.emplaceBack(
                                STPathElement::TypeAccount, a, xrpCurrency(), xrpAccount());
                            path.emplaceBack(
                                STPathElement::TypeAccount, b, xrpCurrency(), xrpAccount());
                            completePaths_.pushBack(path);
                        }
                    }
                }
            }();

            // 3-hop: src → A → C → B → dst
            // A ∈ srcPeers; C is a peer of A (loaded from cache, capped to
            // avoid expanding large gateway trust-line lists); B ∈ dstPeers.
            // This handles the common "market maker" topology where a liquidity
            // provider C holds trust lines with two different gateways A and B.
            // kMaxGatewayPeers caps the number of A's peers we expand; if A
            // has more lines than this it is likely a large gateway and we skip
            // to avoid loading millions of trust lines into memory.
            [&] {
                static constexpr std::size_t kMaxGatewayPeers = 50;
                static constexpr std::size_t kMaxHop3Probes = 200;
                std::size_t probes = 0;
                for (auto const& a : srcPeers)
                {
                    if (continueCallback && !continueCallback())
                        return;
                    auto const aLines = cache_->getRippleLines(a, LineDirection::Outgoing);
                    if (!aLines || aLines->size() > kMaxGatewayPeers)
                        continue;
                    for (auto const& aLine : *aLines)
                    {
                        if (aLine.getBalance().get<Issue>().currency != targetCcy)
                            continue;
                        if (aLine.getFreeze() || aLine.getDeepFreeze())
                            continue;
                        AccountID const& c = aLine.getAccountIDPeer();
                        if (c == srcAccount_ || c == dstAccount_)
                            continue;
                        if (dstPeers.contains(c))
                            continue;  // already covered by 1-hop
                        for (auto const& b : dstPeers)
                        {
                            if (probes++ >= kMaxHop3Probes)
                                return;
                            if (c == b || c == a)
                                continue;
                            if (ledger_->read(keylet::trustLine(c, b, targetCcy)))
                            {
                                STPath path;
                                path.emplaceBack(
                                    STPathElement::TypeAccount, a, xrpCurrency(), xrpAccount());
                                path.emplaceBack(
                                    STPathElement::TypeAccount, c, xrpCurrency(), xrpAccount());
                                path.emplaceBack(
                                    STPathElement::TypeAccount, b, xrpCurrency(), xrpAccount());
                                completePaths_.pushBack(path);
                            }
                        }
                    }
                }
            }();
        }
    }

    JLOG(j_.debug()) << "GraphPathfinder: " << completePaths_.size() << " concrete paths";
    return true;
}

//==============================================================================
// materialise — convert an abstract AssetPath into a concrete STPath.
//
// An AssetPath is a sequence of asset vertices in the exchange graph:
//   assetPath.vids = [srcAsset, bridge1, bridge2, ..., dstAsset]
//
// For each consecutive pair (A, B):
//   • If A -> B is an OrderBook edge: emit an offer node (currency+issuer only,
//     no account).  The XRPL payment engine will fill in the best offer.
//   • If A -> B is an AMM edge: same structure as an order book node;
//     the engine resolves AMM vs offer automatically.
//
// The path does NOT include the source or destination accounts; those are
// implicit in the payment.
//==============================================================================

std::optional<STPath>
GraphPathfinder::materialise(PayGraph::AssetPath const& assetPath)
{
    if (assetPath.vids.size() < 2)
        return std::nullopt;

    PayGraph::VID const firstVID = assetPath.vids.front();
    PayGraph::VID const lastVID = assetPath.vids.back();
    if (firstVID >= snap_->assets.size() || lastVID >= snap_->assets.size())
        return std::nullopt;

    STPath path;

    // Prefix: when the PayGraph path's first vertex is issued by a gateway G
    // that differs from both the sender and the caller's explicit srcIssuer,
    // add G as an account node.  This arises when srcIssuer_ refers to the
    // sender (fallback) but the actual OB offer is in G's IOU — rippleCalc
    // needs G explicit to ripple through it.
    // Do NOT add the prefix when the first vertex issuer matches srcIssuer_
    // (the caller already holds that gateway's IOU directly).
    {
        Asset const& srcAsset = snap_->assets[firstVID];
        if (srcAsset.holds<Issue>() && !isXRP(srcAsset.get<Issue>().currency))
        {
            AccountID const& g = srcAsset.get<Issue>().account;
            AccountID const expectedIssuer = srcIssuer_.value_or(srcAccount_);
            if (g != srcAccount_ && g != xrpAccount() && g != expectedIssuer)
                path.emplaceBack(STPathElement::TypeAccount, g, xrpCurrency(), xrpAccount());
        }
    }

    for (std::size_t i = 0; i + 1 < assetPath.vids.size(); ++i)
    {
        PayGraph::VID const vFrom = assetPath.vids[i];
        PayGraph::VID const vTo = assetPath.vids[i + 1];

        if (vFrom >= snap_->assets.size() || vTo >= snap_->assets.size())
            return std::nullopt;

        Asset const& toAsset = snap_->assets[vTo];

        bool const toIsXRP = isXRP(toAsset);

        // Find the edge kind between these two vertices.
        PayGraph::EdgeKind kind = PayGraph::EdgeKind::OrderBook;
        if (vFrom < snap_->adj.size())
        {
            for (auto const& e : snap_->adj[vFrom])
            {
                if (e.to == vTo)
                {
                    kind = e.kind;
                    break;
                }
            }
        }

        if (kind == PayGraph::EdgeKind::OrderBook)
        {
            // Offer-book crossing: emit a node that has only the receiving
            // asset's currency and issuer (TypeCurrency | TypeIssuer).
            // The account field is set to the xrpAccount() sentinel per XRPL
            // convention for offer nodes.
            if (toIsXRP)
            {
                // Receiving XRP from an order book: emit XRP offer node.
                path.emplaceBack(
                    STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount());
            }
            else if (toAsset.holds<MPTIssue>())
            {
                // Receiving an MPT from an order book.
                auto const& mptIssue = toAsset.get<MPTIssue>();
                path.emplaceBack(
                    STPathElement::TypeMpt | STPathElement::TypeIssuer,
                    xrpAccount(),
                    mptIssue.getMptID(),
                    mptIssue.getIssuer());
            }
            else
            {
                // Receiving an IOU from an order book.
                auto const& toIssue = toAsset.get<Issue>();
                path.emplaceBack(
                    STPathElement::TypeCurrency | STPathElement::TypeIssuer,
                    xrpAccount(),
                    toIssue.currency,
                    toIssue.account);
            }
        }
        else  // AMM pool
        {
            // AMM crossing: same structure as order book for path purposes.
            // The engine resolves AMM vs offer automatically.
            if (!toIsXRP)
            {
                if (toAsset.holds<MPTIssue>())
                {
                    auto const& mptIssue = toAsset.get<MPTIssue>();
                    path.emplaceBack(
                        STPathElement::TypeMpt | STPathElement::TypeIssuer,
                        xrpAccount(),
                        mptIssue.getMptID(),
                        mptIssue.getIssuer());
                }
                else
                {
                    auto const& toIssue = toAsset.get<Issue>();
                    path.emplaceBack(
                        STPathElement::TypeCurrency | STPathElement::TypeIssuer,
                        xrpAccount(),
                        toIssue.currency,
                        toIssue.account);
                }
            }
            else
            {
                path.emplaceBack(
                    STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount());
            }
        }
    }

    if (path.empty())
        return std::nullopt;

    JLOG(j_.info()) << "GraphPathfinder::materialise src=" << toBase58(srcAccount_)
                    << " dst=" << toBase58(dstAccount_)
                    << " srcAsset=" << snap_->assets[firstVID].getText()
                    << " dstAsset=" << snap_->assets[lastVID].getText()
                    << " path=" << json::Compact{path.getJson(JsonOptions::Values::None)};

    // Suffix: when the path ends at an OB node whose asset is issued by a
    // gateway G that is not the payment's effective destination, the engine
    // needs account nodes to ripple from G to dstAccount_.  We emit G plus
    // (if G is not directly connected to dstAccount_) the intermediate node
    // B that bridges G to dstAccount_ via a SHAMap point-lookup.
    {
        Asset const& dstAsset = snap_->assets[lastVID];
        if (dstAsset.holds<Issue>() && !isXRP(dstAsset.get<Issue>().currency))
        {
            AccountID const& g = dstAsset.get<Issue>().account;
            if (g != effectiveDst_ && g != dstAccount_)
            {
                Currency const& ccy = dstAsset.get<Issue>().currency;
                // First add G itself.
                path.emplaceBack(STPathElement::TypeAccount, g, xrpCurrency(), xrpAccount());
                // If dstAccount_ does not hold G's IOU directly, look for an
                // intermediate account B that has trust lines with both G and
                // dstAccount_.  Load dstAccount_'s trust lines (cheap: these
                // are the user's own lines, typically very few).
                if (!ledger_->read(keylet::trustLine(g, dstAccount_, ccy)))
                {
                    auto const dstLines = ledger_->read(keylet::account(dstAccount_))
                        ? cache_->getRippleLines(dstAccount_, LineDirection::Outgoing)
                        : nullptr;
                    if (dstLines)
                    {
                        for (auto const& dl : *dstLines)
                        {
                            if (dl.getBalance().get<Issue>().currency != ccy)
                                continue;
                            AccountID const& b = dl.getAccountIDPeer();
                            if (b == g || b == dstAccount_ || b == srcAccount_)
                                continue;
                            if (ledger_->read(keylet::trustLine(g, b, ccy)))
                            {
                                path.emplaceBack(
                                    STPathElement::TypeAccount, b, xrpCurrency(), xrpAccount());
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return path;
}

//==============================================================================
// getPathLiquidity — same logic as Pathfinder::getPathLiquidity
//==============================================================================

TER
GraphPathfinder::getPathLiquidity(
    STPath const& path,
    STAmount const& minDstAmount,
    STAmount& amountOut,
    uint64_t& qualityOut) const
{
    STPathSet pathSet;
    pathSet.pushBack(path);

    path::RippleCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    PaymentSandbox sandbox(&*ledger_, TapNone);

    try
    {
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

        // Broken AMMs are excluded inside BookStep::getAMMOffer after a single
        // FlowException (noteFailedAMM).  Subsequent polls skip that AMM only;
        // CLOB liquidity on the same book remains available.

        if (!isTesSuccess(rc.result()))
        {
            JLOG(j_.debug()) << "GraphPathfinder::getPathLiquidity failed: "
                             << transHuman(rc.result());
            return rc.result();
        }

        qualityOut = getRate(rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        // Second pass only when we still need more liquidity for fixed dst.
        // Skip for convert_all — one probe is enough for ranking.
        if (!convertAll_ && amountOut < minDstAmount)
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
    catch (FlowException const& e)
    {
        // Rare: exception escaped StrandFlow.  Prefer the AMM attached to the
        // exception; do not blacklist every hop on the path.
        if (e.ammBook)
            noteFailedAMM(*e.ammBook, ledger_ ? ledger_->seq() : 0);
        JLOG(j_.debug()) << "GraphPathfinder::getPathLiquidity FlowException: " << e.what();
        return tefEXCEPTION;
    }
    catch (std::exception const& e)
    {
        JLOG(j_.debug()) << "GraphPathfinder::getPathLiquidity exception: " << e.what();
        return tefEXCEPTION;
    }
}

//==============================================================================
// rankPaths
//==============================================================================

namespace {

STAmount
smallestUsefulAmount(STAmount const& amount, int maxPaths)
{
    return divide(amount, STAmount(maxPaths + 2), amount.asset());
}

}  // namespace

void
GraphPathfinder::rankPaths(
    int maxPaths,
    STPathSet const& paths,
    std::vector<PathRank>& rankedPaths,
    std::function<bool()> const& continueCallback)
{
    rankedPaths.clear();
    rankedPaths.reserve(paths.size());

    auto const saMinDstAmount = [&]() -> STAmount {
        if (!convertAll_)
            return smallestUsefulAmount(dstAmount_, maxPaths);
        return largestAmount(dstAmount_);
    }();

    // Hard cap on expensive rippleCalculate probes.  After the first
    // FlowException for an AMM, BookStep skips that AMM so later evals should
    // not re-throw; these caps still bound worst case.
    int const maxProbes = std::max(maxPaths * 2, maxPaths + 3);
    int consecutiveFailures = 0;
    int probes = 0;

    for (int i = 0; i < static_cast<int>(paths.size()); ++i)
    {
        if (continueCallback && !continueCallback())
            return;

        auto const& currentPath = paths[i];
        if (currentPath.empty())
            continue;

        // Enough ranked paths for the caller — stop probing.
        if (static_cast<int>(rankedPaths.size()) >= maxPaths)
            break;

        if (++probes > maxProbes)
            break;

        STAmount liquidity;
        uint64_t quality = 0;
        if (isTesSuccess(getPathLiquidity(currentPath, saMinDstAmount, liquidity, quality)))
        {
            consecutiveFailures = 0;
            rankedPaths.push_back({quality, currentPath.size(), liquidity, i});
        }
        else
        {
            // Bail after a short run of dry/overflow paths.  Yen already
            // ordered by graph quality, so later candidates rarely save us
            // once several consecutive rippleCalcs fail.
            if (++consecutiveFailures >= 3)
                break;
        }
    }

    std::ranges::sort(rankedPaths, [&](PathRank const& a, PathRank const& b) {
        if (!convertAll_ && a.quality != b.quality)
            return a.quality < b.quality;
        if (a.liquidity != b.liquidity)
            return a.liquidity > b.liquidity;
        if (a.length != b.length)
            return a.length < b.length;
        return a.index > b.index;
    });
}

//==============================================================================
// computePathRanks
//==============================================================================

void
GraphPathfinder::computePathRanks(int maxPaths, std::function<bool()> const& continueCallback)
{
    remainingAmount_ = convertAmount(dstAmount_, convertAll_);

    // Subtract the default-path contribution (same as Pathfinder).
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
            remainingAmount_ -= rc.actualAmountOut;
    }
    catch (std::exception const&)
    {
        JLOG(j_.debug()) << "GraphPathfinder: default path exception";
    }

    rankPaths(maxPaths, completePaths_, pathRanks_, continueCallback);
}

//==============================================================================
// getBestPaths — identical selection logic to Pathfinder::getBestPaths
//==============================================================================

STPathSet
GraphPathfinder::getBestPaths(
    int maxPaths,
    STPathSet const& extraPaths,
    AccountID const& srcIssuer,
    std::function<bool()> const& continueCallback)
{
    if (completePaths_.empty() && extraPaths.empty())
        return completePaths_;

    // GraphPathfinder materialises paths as offer/currency nodes only — it
    // never prepends the issuer's account node the way Pathfinder does.
    // The XRPL payment engine implicitly handles the sender→issuer trust-line
    // traversal, so the issuer-first filtering that Pathfinder::getBestPaths
    // uses does not apply here.  Always treat issuerIsSender = true.
    bool const issuerIsSender = true;

    // If all paths failed quality ranking (e.g. every route hits an AMM
    // overflow), return the unranked concrete paths so processResult's
    // safeCalc wrapper can attempt them with the real amounts.  Any that
    // still overflow will be caught there and silently skipped.
    if (pathRanks_.empty() && !completePaths_.empty())
    {
        STPathSet result;
        for (auto const& path : completePaths_)
        {
            if (static_cast<int>(result.size()) >= maxPaths)
                break;
            if (!path.empty())
                result.pushBack(path);
        }
        return result;
    }

    std::vector<PathRank> extraRanks;
    rankPaths(maxPaths, extraPaths, extraRanks, continueCallback);

    STPathSet bestPaths;
    STAmount remaining = remainingAmount_;

    auto itA = pathRanks_.begin();
    auto itB = extraRanks.begin();

    while (itA != pathRanks_.end() || itB != extraRanks.end())
    {
        if (continueCallback && !continueCallback())
            break;

        bool usePath = false;
        bool useExtra = false;

        if (itA == pathRanks_.end())
        {
            useExtra = true;
        }
        else if (itB == extraRanks.end())
        {
            usePath = true;
        }
        else if (itB->quality < itA->quality)
        {
            useExtra = true;
        }
        else if (itB->quality > itA->quality)
        {
            usePath = true;
        }
        else if (itB->liquidity > itA->liquidity)
        {
            useExtra = true;
        }
        else if (itB->liquidity < itA->liquidity)
        {
            usePath = true;
        }
        else
        {
            useExtra = true;
            usePath = true;
        }

        auto& rank = usePath ? *itA : *itB;
        auto const& p = usePath ? completePaths_[rank.index] : extraPaths[rank.index];

        if (useExtra)
            ++itB;
        if (usePath)
            ++itA;

        int iPathsLeft = maxPaths - static_cast<int>(bestPaths.size());
        if (iPathsLeft <= 0)
            break;

        if (p.empty())
            continue;

        // Skip paths that don't match issuer constraints.
        bool startsWithIssuer = false;
        if (!issuerIsSender && usePath)
        {
            if (p.size() == 1 || p.front().getAccountID() != srcIssuer)
                continue;
            startsWithIssuer = true;
        }

        STPath trimmed;
        if (startsWithIssuer)
        {
            for (auto it = p.begin() + 1; it != p.end(); ++it)
                trimmed.pushBack(*it);
        }
        STPath const& finalPath = startsWithIssuer ? trimmed : p;

        if (iPathsLeft > 0)
        {
            --iPathsLeft;
            remaining -= rank.liquidity;
            bestPaths.pushBack(finalPath);
        }
    }

    return bestPaths;
}

}  // namespace xrpl
