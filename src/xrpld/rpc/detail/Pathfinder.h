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

    /* Get the best paths, up to maxPaths in number, from mCompletePaths.

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
        NtSource,      // The source account: with an issuer account, if needed.
        NtAccounts,    // Accounts that connect from this source/currency.
        NtBooks,       // Order books that connect to this currency.
        NtXrpBook,     // The order book from this currency to XRP.
        NtDestBook,    // The order book to the destination currency/issuer.
        NtDestination  // The destination account only.
    };

    // The PathType is a list of the NodeTypes for a path.
    using PathType = std::vector<NodeType>;

    // PaymentType represents the types of the source and destination currencies
    // in a path request.
    enum class PaymentType {
        PtXrpToXrp,
        PtXrpToNonXrp,
        PtNonXrpToXrp,
        PtNonXrpToSame,   // Destination currency is the same as source.
        PtNonXrpToNonXrp  // Destination currency is NOT the same as source.
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

    // Add all paths of one type to mCompletePaths.
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

    AccountID mSrcAccount_;
    AccountID mDstAccount_;
    AccountID mEffectiveDst_;  // The account the paths need to end at
    STAmount mDstAmount_;
    PathAsset mSrcPathAsset_;
    std::optional<AccountID> mSrcIssuer_;
    STAmount mSrcAmount_;
    /** The amount remaining from mSrcAccount after the default liquidity has
        been removed. */
    STAmount mRemainingAmount_;
    bool convert_all_;
    std::optional<uint256> mDomain_;

    std::shared_ptr<ReadView const> mLedger_;
    std::unique_ptr<LoadEvent> m_loadEvent_;
    std::shared_ptr<AssetCache> mAssetCache_;

    STPathElement mSource_;
    STPathSet mCompletePaths_;
    std::vector<PathRank> mPathRanks_;
    std::map<PathType, STPathSet> mPaths_;

    hash_map<Asset, int> mPathsOutCountMap_;

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
