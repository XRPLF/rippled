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

/** Calculates payment paths.

    The @ref RippleCalc determines the quality of the found paths.

    @see RippleCalc
*/
class Pathfinder : public CountedObject<Pathfinder>
{
public:
    /** Construct a pathfinder without an issuer.*/
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

    static void
    initPathTable();

    bool
    findPaths(int searchLevel, std::function<bool(void)> const& continueCallback = {});

    /** Compute the rankings of the paths. */
    void
    computePathRanks(int maxPaths, std::function<bool(void)> const& continueCallback = {});

    /* Get the best paths, up to maxPaths in number, from completePaths_.

       On return, if fullLiquidityPath is not empty, then it contains the best
       additional single path which can consume all the liquidity.
    */
    STPathSet
    getBestPaths(
        int maxPaths,
        STPath& fullLiquidityPath,
        STPathSet const& extraPaths,
        AccountID const& srcIssuer,
        std::function<bool(void)> const& continueCallback = {});

    enum class NodeType {
        Source,      // The source account: with an issuer account, if needed.
        Accounts,    // Accounts that connect from this source/currency.
        Books,       // Order books that connect to this currency.
        XrpBook,     // The order book from this currency to XRP.
        DestBook,    // The order book to the destination currency/issuer.
        Destination  // The destination account only.
    };

    // The PathType is a list of the NodeTypes for a path.
    using PathType = std::vector<NodeType>;

    // PaymentType represents the types of the source and destination currencies
    // in a path request.
    enum class PaymentType {
        XrpToXrp,
        XrpToNonXrp,
        NonXrpToXrp,
        NonXrpToSame,   // Destination currency is the same as source.
        NonXrpToNonXrp  // Destination currency is NOT the same as source.
    };

    struct PathRank
    {
        std::uint64_t quality{};
        std::uint64_t length{};
        STAmount liquidity;
        int index{};
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
    static std::uint32_t const kAfAddAccounts = 0x001;

    // Add order books
    static std::uint32_t const kAfAddBooks = 0x002;

    // Add order book to XRP only
    static std::uint32_t const kAfObXrp = 0x010;

    // Must link to destination currency
    static std::uint32_t const kAfObLast = 0x040;

    // Destination account only
    static std::uint32_t const kAfAcLast = 0x080;
};

}  // namespace xrpl
