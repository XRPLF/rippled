/** @file
 *  Declares the `Pathfinder` class — the core payment-path discovery engine
 *  for the XRP Ledger RPC layer.
 *
 *  `Pathfinder` drives the three-phase pipeline used by `path_find` and
 *  `ripple_path_find`: graph enumeration (`findPaths`), liquidity ranking
 *  (`computePathRanks`), and best-path selection (`getBestPaths`).  Its
 *  output is an `STPathSet` consumed by `RippleCalc` during transaction
 *  processing.
 */
#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>

namespace xrpl {

/** Discovers viable multi-hop payment paths across the XRP Ledger.
 *
 *  Callers must invoke the three public methods in sequence:
 *  1. `findPaths(searchLevel)` — enumerate candidate paths via template
 *     expansion of a static payment-type table.
 *  2. `computePathRanks(maxPaths)` — score each candidate with a simulated
 *     `RippleCalc` payment against a read-only `PaymentSandbox`.
 *  3. `getBestPaths(maxPaths, ...)` — merge ranked discovered paths with any
 *     caller-injected `extraPaths` and return the final `STPathSet`.
 *
 *  Separating enumeration from ranking is deliberate: graph traversal is
 *  cheap, while `RippleCalc` calls are expensive.  The split lets streaming
 *  `path_find` sessions inject paths from previous invocations without
 *  re-running the full search.
 *
 *  A `LoadEvent` registered at construction time charges CPU time to the
 *  job-queue's load-balancing system, because pathfinding is among the most
 *  expensive RPC operations on a busy server.
 *
 *  @note `Pathfinder` works through a shared `AssetCache` rather than
 *      reading trust lines directly from the ledger.  The cache memoises
 *      results across multiple `addLink()` calls so each account's SLE is
 *      read at most once per pathfinding session.
 *
 *  @see PathRequest
 *  @see AssetCache
 */
class Pathfinder : public CountedObject<Pathfinder>
{
public:
    /** Construct a `Pathfinder` for a payment request.
     *
     *  Initialises all path-search state.  `effectiveDst_` is set to the
     *  gateway account when `dstAmount` carries a non-XRP issuer that
     *  differs from `dstAccount`; otherwise it equals `dstAccount`.
     *  `convert_all_` is `true` when `dstAmount` is the ledger maximum,
     *  signalling a "send as much as possible" payment.
     *
     *  @param cache        Memoised trust-line and MPT view of the current
     *      ledger; shared across multiple `Pathfinder` invocations within a
     *      single client session so warm cache entries persist.
     *  @param srcAccount   Account sending the payment.
     *  @param dstAccount   Ultimate destination account.
     *  @param uSrcPathAsset Source asset (IOU currency or MPT ID).
     *  @param uSrcIssuer   Optional explicit issuer override for the source
     *      asset; must be XRP iff `uSrcPathAsset` is XRP.
     *  @param dstAmount    Amount to deliver; determines the `PaymentType`
     *      classification and whether `convert_all_` is set.
     *  @param srcAmount    Optional explicit source amount (stored but not
     *      currently used in path ranking).
     *  @param domain       Optional domain filter restricting pathfinding to
     *      a specific permissioned domain; paths violating it are pruned at
     *      liquidity-measurement time.
     *  @param app          Application context supplying the job queue,
     *      order-book DB, and other infrastructure.
     */
    Pathfinder(
        std::shared_ptr<AssetCache> const& cache,
        AccountID const& srcAccount,
        AccountID const& dstAccount,
        PathAsset const& uSrcPathAsset,
        std::optional<AccountID> const& uSrcIssuer,
        STAmount const& dstAmount,
        std::optional<STAmount> const& srcAmount,
        std::optional<uint256> const& domain,
        Application& app);
    Pathfinder(Pathfinder const&) = delete;
    Pathfinder&
    operator=(Pathfinder const&) = delete;
    ~Pathfinder() = default;

    /** Populate the global path-shape table used by all `Pathfinder` instances.
     *
     *  Must be called once at startup before any `Pathfinder` is constructed.
     *  Clears and rebuilds `gPathTable` mapping each `PaymentType` to its
     *  ordered list of `PathType` templates and associated search-level costs.
     *  Calling it again is safe and is done in tests.
     *
     *  @note Do not add entries that reproduce the implicit default path (a
     *      direct trust-line transfer with no intermediaries); those are
     *      handled separately by `computePathRanks()`.
     */
    static void
    initPathTable();

    /** Enumerate candidate complete payment paths up to the given search depth.
     *
     *  Validates preconditions (non-zero destination amount, accounts exist,
     *  new-account funding rules), classifies the payment into one of the five
     *  `PaymentType` categories, then expands all `gPathTable` entries whose
     *  `searchLevel` does not exceed `searchLevel`.  Complete paths accumulate
     *  in `completePaths_`, capped at 1000 to prevent combinatorial explosion.
     *
     *  Returns `true` when the caller should proceed to `computePathRanks()`.
     *  Returns `false` when the request is definitively unsatisfiable (zero
     *  destination amount, same source and destination asset on the same
     *  account, missing ledger, non-existent accounts, or insufficient funding
     *  for a new account) or when `continueCallback` signals cancellation.
     *
     *  @param searchLevel      Maximum cost tier to explore; 0 skips all
     *      graph traversal.  Streaming `path_find` sessions start at 1 for a
     *      fast first reply and increment toward 10 for subsequent ones.
     *  @param continueCallback Optional cooperative-cancellation predicate;
     *      returning `false` aborts the search and causes this function to
     *      return `false`.
     *  @return `true` if ranking should proceed; `false` if the request is
     *      unsatisfiable or was cancelled.
     */
    bool
    findPaths(int searchLevel, std::function<bool(void)> const& continueCallback = {});

    /** Score all discovered paths by liquidity and exchange rate.
     *
     *  Before ranking, probes the implicit default path (an empty
     *  `STPathSet`) via `RippleCalc` and subtracts any amount it can deliver
     *  from `remainingAmount_`.  Explicit paths discovered by `findPaths` are
     *  then only required to cover the residual.  Delegates to `rankPaths()`
     *  which calls `getPathLiquidity()` per candidate and sorts survivors by
     *  quality, then liquidity, then length.
     *
     *  Must be called after `findPaths()` returns `true` and before
     *  `getBestPaths()`.
     *
     *  @param maxPaths          Maximum number of paths that will ultimately
     *      be selected; used to compute the per-path minimum useful amount.
     *  @param continueCallback  Optional cooperative-cancellation predicate.
     */
    void
    computePathRanks(int maxPaths, std::function<bool(void)> const& continueCallback = {});

    /** Select up to `maxPaths` best paths and an optional full-liquidity fallback.
     *
     *  Merges `pathRanks_` (ranked discovered paths) with re-ranked
     *  `extraPaths` in a single linear pass.  Selection rules:
     *  - Up to `maxPaths - 1` slots are filled in quality order.
     *  - The last slot is only filled if the path's liquidity covers the
     *    remaining amount, ensuring the selected set can collectively complete
     *    the payment (assuming no liquidity overlap).
     *  - After the slots are full, the scan continues to find a
     *    `fullLiquidityPath`: a single path whose liquidity alone covers
     *    `dstAmount_`, returned as a covering fallback.
     *
     *  When `srcIssuer` is not the sender, only paths beginning with the
     *  issuer node are eligible; the issuer node is stripped before the path
     *  is added to the result.
     *
     *  @param maxPaths           Maximum number of paths to return.
     *  @param fullLiquidityPath  Output: set to a single path that alone can
     *      cover the full `dstAmount_`; left empty if none is found.
     *      Caller must pass an empty `STPath`.
     *  @param extraPaths         Previously found paths to merge with newly
     *      discovered ones; typically preserved across `path_find` subscription
     *      updates to avoid discarding useful routes on ledger advance.
     *  @param srcIssuer          Issuer of the source asset; filters and strips
     *      the leading issuer node when the issuer is not the sender.
     *  @param continueCallback   Optional cooperative-cancellation predicate.
     *  @return                   The selected best paths (may be empty if no
     *      discovered path meets the liquidity threshold).
     */
    STPathSet
    getBestPaths(
        int maxPaths,
        STPath& fullLiquidityPath,
        STPathSet const& extraPaths,
        AccountID const& srcIssuer,
        std::function<bool(void)> const& continueCallback = {});

    /** Identifies the role of a node within a payment-path template.
     *
     *  Each `NodeType` token corresponds to a single graph-expansion step in
     *  `addPathsForType()`.  A `PathType` is a sequence of these tokens
     *  encoding a complete route shape, e.g. `{Source, Books, Accounts,
     *  Destination}` encodes `"sbad"`.
     */
    enum class NodeType {
        Source,      ///< The source account (with explicit issuer when needed).
        Accounts,    ///< Accounts reachable from the current path endpoint via trust lines or MPT.
        Books,       ///< Order books accepting the current endpoint's asset.
        XrpBook,     ///< The order book from the current asset to XRP specifically.
        DestBook,    ///< The order book whose output matches the destination asset.
        Destination  ///< The destination account; terminates the path.
    };

    /** Sequence of `NodeType` tokens encoding a single route shape. */
    using PathType = std::vector<NodeType>;

    /** Classifies a payment by its source and destination asset types.
     *
     *  Used to index `gPathTable` so that only domain-relevant path shapes
     *  are explored.  The five categories cover every combination of XRP and
     *  non-XRP assets on each side of the payment.
     */
    enum class PaymentType {
        XrpToXrp,       ///< Source and destination are both XRP; no cross-currency routing needed.
        XrpToNonXrp,    ///< XRP in, non-XRP out; path must include at least one order book.
        NonXrpToXrp,    ///< Non-XRP in, XRP out; path typically passes through an XRP order book.
        NonXrpToSame,   ///< Source and destination currencies are the same non-XRP asset.
        NonXrpToNonXrp  ///< Source and destination are different non-XRP assets; most complex category.
    };

    /** Scored record for a single candidate path, produced by `rankPaths()`.
     *
     *  The three-key sort order used in `rankPaths()` is: quality ascending
     *  (lower cost first), liquidity descending (more is better), length
     *  ascending (shorter is safer).
     */
    struct PathRank
    {
        std::uint64_t quality{};  ///< Exchange rate expressed as a 64-bit fixed-point ratio (input/output); lower is better.
        std::uint64_t length{};   ///< Number of hops; shorter paths carry less counterparty risk.
        STAmount liquidity;       ///< Maximum amount this path can deliver as measured by `getPathLiquidity()`.
        int index{};              ///< Index of the path in the source `STPathSet`; used for final tie-breaking.
    };

private:
    /*
      Call graph of Pathfinder methods.

      findPaths:
          addPathsForType:
              addLinks:
                  addLink:
                      getPathsOut
                      issueMatchesOrigin
                      isNoRippleOut:
                          isNoRipple

      computePathRanks:
          rippleCalculate
          getPathLiquidity:
              rippleCalculate

      getBestPaths
     */

    // Add all paths of one type to completePaths_.
    STPathSet&
    addPathsForType(PathType const& type, std::function<bool(void)> const& continueCallback);

    bool
    issueMatchesOrigin(Asset const&);

    int
    getPathsOut(
        PathAsset const& pathAsset,
        AccountID const& account,
        LineDirection direction,
        bool isDestPathAsset,
        AccountID const& dest,
        std::function<bool(void)> const& continueCallback);

    void
    addLink(
        STPath const& currentPath,
        STPathSet& incompletePaths,
        int addFlags,
        std::function<bool(void)> const& continueCallback);

    // Call addLink() for each path in currentPaths.
    void
    addLinks(
        STPathSet const& currentPaths,
        STPathSet& incompletePaths,
        int addFlags,
        std::function<bool(void)> const& continueCallback);

    // Compute the liquidity for a path.  Return tesSUCCESS if it has enough
    // liquidity to be worth keeping, otherwise an error.
    TER
    getPathLiquidity(
        STPath const& path,            // IN:  The path to check.
        STAmount const& minDstAmount,  // IN:  The minimum output this path must
                                       //      deliver to be worth keeping.
        STAmount& amountOut,           // OUT: The actual liquidity on the path.
        uint64_t& qualityOut) const;   // OUT: The returned initial quality

    // Does this path end on an account-to-account link whose last account has
    // set the "no ripple" flag on the link?
    bool
    isNoRippleOut(STPath const& currentPath);

    // Is the "no ripple" flag set from one account to another?
    bool
    isNoRipple(AccountID const& fromAccount, AccountID const& toAccount, Currency const& currency);

    void
    rankPaths(
        int maxPaths,
        STPathSet const& paths,
        std::vector<PathRank>& rankedPaths,
        std::function<bool(void)> const& continueCallback);

    AccountID srcAccount_;
    AccountID dstAccount_;
    AccountID effectiveDst_;  // The account the paths need to end at
    STAmount dstAmount_;
    PathAsset srcPathAsset_;
    std::optional<AccountID> srcIssuer_;
    STAmount srcAmount_;
    /** The amount remaining from srcAccount_ after the default liquidity has
        been removed. */
    STAmount remainingAmount_;
    bool convert_all_;
    std::optional<uint256> domain_;

    std::shared_ptr<ReadView const> ledger_;
    std::unique_ptr<LoadEvent> loadEvent_;
    std::shared_ptr<AssetCache> rLCache_;

    STPathElement source_;
    STPathSet completePaths_;
    std::vector<PathRank> pathRanks_;
    std::map<PathType, STPathSet> paths_;

    hash_map<Asset, int> pathsOutCountMap_;

    Application& app_;
    beast::Journal const j_;

    // Add ripple paths
    static std::uint32_t const kAF_ADD_ACCOUNTS = 0x001;

    // Add order books
    static std::uint32_t const kAF_ADD_BOOKS = 0x002;

    // Add order book to XRP only
    static std::uint32_t const kAF_OB_XRP = 0x010;

    // Must link to destination currency
    static std::uint32_t const kAF_OB_LAST = 0x040;

    // Destination account only
    static std::uint32_t const kAF_AC_LAST = 0x080;
};

}  // namespace xrpl
