#pragma once

//------------------------------------------------------------------------------
/*
    GraphPathfinder — Drop-in replacement for Pathfinder that uses PayGraph.

    DESIGN
    ------
    Instead of BFS over the combined account+asset space, GraphPathfinder:

      1. Looks up the pre-built PayGraph for the current ledger.
      2. Runs Yen's K-Shortest (via PayGraph::kShortestPaths) on the tiny
         asset-exchange graph to get ≤6 abstract asset-type paths.
         This is O((V+E) log V) — microseconds.

      3. For each abstract path, "materialises" it into a concrete STPath:
         • Each hop that crosses an order book or AMM pool becomes a single
           book node (currency + issuer, no account).  The XRPL payment
           engine fills in the best offer and handles trust-line rippling
           implicitly.
         This is O(hops) per path.

      4. Passes the materialised STPathSet to rippleCalculate for final
         liquidity confirmation and quality scoring.  At most kMaxPaths (6)
         paths are evaluated.

      5. Results are returned immediately; each successive call (driven by
         PathRequestManager) can augment the previous result with paths
         discovered at higher search depth — maintaining the existing
         progressive-refinement WebSocket contract.

    WEBSOCKET CONTRACT
    ------------------
    The existing PathRequest/PathRequestManager pipeline already handles
    the "emit fast then refine" pattern:

      • doUpdate(fast=true) → emit first result ASAP
      • doUpdate(fast=false) called on each ledger → refine / confirm

    GraphPathfinder plugs in as the engine behind PathRequest::getPathFinder()
    and is otherwise transparent to the rest of the stack.

    INTERFACE COMPATIBILITY
    -----------------------
    GraphPathfinder exposes the same public interface as Pathfinder so it can
    be swapped in with minimal changes to PathRequest.cpp.

    The key difference: findPaths() is O(μs) instead of O(ms–seconds) because
    the graph traversal is pre-built and query time is trivially small.
*/
//------------------------------------------------------------------------------

#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PayGraph.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/UintTypes.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

class Application;
class ReadView;

class GraphPathfinder : public CountedObject<GraphPathfinder>
{
public:
    //--------------------------------------------------------------------------
    // Construction
    //
    // Matches the Pathfinder constructor signature so PathRequest can
    // instantiate either class with the same code path.
    //--------------------------------------------------------------------------

    GraphPathfinder(
        std::shared_ptr<PayGraph> const& graph,
        std::shared_ptr<AssetCache> const& cache,
        AccountID const& srcAccount,
        AccountID const& dstAccount,
        PathAsset const& srcPathAsset,
        std::optional<AccountID> const& srcIssuer,
        STAmount const& dstAmount,
        std::optional<STAmount> const& srcAmount,
        std::optional<uint256> const& domain,
        Application& app);

    GraphPathfinder(GraphPathfinder const&) = delete;
    GraphPathfinder&
    operator=(GraphPathfinder const&) = delete;
    ~GraphPathfinder() = default;

    //--------------------------------------------------------------------------
    // findPaths — core entry point.
    //
    // Runs Yen's K-Shortest on the asset graph, materialises the abstract
    // paths into concrete STPath objects, and stores them in completePaths_.
    //
    // continueCallback is checked between path materialisations; return false
    // to abort early (the paths found so far remain valid).
    //
    // Returns true if at least one candidate path was found.
    //--------------------------------------------------------------------------
    bool
    findPaths(std::function<bool()> const& continueCallback = {});

    //--------------------------------------------------------------------------
    // computePathRanks — rank completePaths_ by quality and liquidity.
    //
    // Calls rippleCalculate on each candidate (same as the old Pathfinder).
    // Populates pathRanks_ for use by getBestPaths().
    //--------------------------------------------------------------------------
    void
    computePathRanks(int maxPaths, std::function<bool()> const& continueCallback = {});

    //--------------------------------------------------------------------------
    // getBestPaths — select up to maxPaths from the ranked candidates.
    //
    // Interface identical to Pathfinder::getBestPaths().
    //--------------------------------------------------------------------------
    STPathSet
    getBestPaths(
        int maxPaths,
        STPathSet const& extraPaths,
        AccountID const& srcIssuer,
        std::function<bool()> const& continueCallback = {});

    //--------------------------------------------------------------------------
    // PathRank — identical layout to Pathfinder::PathRank so PathRequest
    // can use the same ranking/selection code.
    //--------------------------------------------------------------------------
    struct PathRank
    {
        std::uint64_t quality{};
        std::uint64_t length{};
        STAmount liquidity;
        int index{};
    };

private:
    //--------------------------------------------------------------------------
    // Materialise one abstract AssetPath into a concrete STPath.
    //
    // Each hop (consecutive VID pair) becomes a single book-node
    // STPathElement with only currency/issuer set (no account).  The XRPL
    // payment engine resolves offers and trust-line rippling implicitly.
    //
    // Returns an empty optional if the path is degenerate (< 2 vertices).
    //--------------------------------------------------------------------------
    std::optional<STPath>
    materialise(PayGraph::AssetPath const& assetPath);

    //--------------------------------------------------------------------------
    // Compute liquidity for a single path using rippleCalculate.
    // Returns tesSUCCESS and fills amountOut/qualityOut on success.
    //--------------------------------------------------------------------------
    TER
    getPathLiquidity(
        STPath const& path,
        STAmount const& minDstAmount,
        STAmount& amountOut,
        uint64_t& qualityOut) const;

    //--------------------------------------------------------------------------
    // Failed-AMM hop helpers (shared process-wide via PathRequestManager).
    //--------------------------------------------------------------------------
    [[nodiscard]] bool
    assetPathTouchesFailedAmm(PayGraph::AssetPath const& assetPath) const;

    [[nodiscard]] bool
    stPathTouchesFailedAmm(STPath const& path) const;

    void
    noteFailedAmmHopsFromPath(STPath const& path) const;

    //--------------------------------------------------------------------------
    // Rank paths (fills pathRanks_ from completePaths_).
    //--------------------------------------------------------------------------
    void
    rankPaths(
        int maxPaths,
        STPathSet const& paths,
        std::vector<PathRank>& rankedPaths,
        std::function<bool()> const& continueCallback);

    //--------------------------------------------------------------------------
    // Member data
    //--------------------------------------------------------------------------

    std::shared_ptr<PayGraph> graph_;
    std::shared_ptr<PayGraph::Snapshot const> snap_;  // stable view for this request

    AccountID srcAccount_;
    AccountID dstAccount_;
    AccountID effectiveDst_;
    STAmount dstAmount_;
    PathAsset srcPathAsset_;
    std::optional<AccountID> srcIssuer_;
    STAmount srcAmount_;
    bool convertAll_;
    std::optional<uint256> domain_;

    std::shared_ptr<ReadView const> ledger_;
    std::shared_ptr<AssetCache> cache_;

    STPathSet completePaths_;
    std::vector<PathRank> pathRanks_;
    STAmount remainingAmount_;

    Application& app_;
    beast::Journal j_;
};

}  // namespace xrpl
