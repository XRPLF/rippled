#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <optional>

namespace xrpl {

/** Describes how an account was found in a path, and how to find the next set
of paths. "Outgoing" is defined as the source account, or an account found via a
trustline that has rippling enabled on the account's side.
"Incoming" is defined as an account found via a trustline that has rippling
disabled on the account's side. Any trust lines for an incoming account that
have rippling disabled are unusable in paths.
*/
enum class LineDirection : bool { incoming = false, outgoing = true };

/** Wraps a trust line SLE for convenience.
    The complication of trust lines is that there is a
    "low" account and a "high" account. This wraps the
    SLE and expresses its data from the perspective of
    a chosen account on the line.

    This wrapper is primarily used in the path finder and there can easily be
    tens of millions of instances of this class. When modifying this class think
    carefully about the memory implications.
*/
class TrustLineBase
{
public:
    TrustLineBase&
    operator=(TrustLineBase const&) = delete;

protected:
    // This class should not be instantiated directly. Use one of the derived
    // classes.
    TrustLineBase(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount);

    ~TrustLineBase() = default;
    TrustLineBase(TrustLineBase const&) = default;
    TrustLineBase(TrustLineBase&&) = default;

public:
    /** Returns the state map key for the ledger entry. */
    [[nodiscard]] uint256 const&
    key() const
    {
        return key_;
    }

    // VFALCO Take off the "get" from each function name

    [[nodiscard]] AccountID const&
    getAccountID() const
    {
        return mViewLowest ? mLowLimit.getIssuer() : mHighLimit.getIssuer();
    }

    [[nodiscard]] AccountID const&
    getAccountIDPeer() const
    {
        return !mViewLowest ? mLowLimit.getIssuer() : mHighLimit.getIssuer();
    }

    // True, Provided auth to peer.
    [[nodiscard]] bool
    getAuth() const
    {
        return (mFlags & (mViewLowest ? lsfLowAuth : lsfHighAuth)) != 0u;
    }

    [[nodiscard]] bool
    getAuthPeer() const
    {
        return (mFlags & (!mViewLowest ? lsfLowAuth : lsfHighAuth)) != 0u;
    }

    [[nodiscard]] bool
    getNoRipple() const
    {
        return (mFlags & (mViewLowest ? lsfLowNoRipple : lsfHighNoRipple)) != 0u;
    }

    [[nodiscard]] bool
    getNoRipplePeer() const
    {
        return (mFlags & (!mViewLowest ? lsfLowNoRipple : lsfHighNoRipple)) != 0u;
    }

    [[nodiscard]] LineDirection
    getDirection() const
    {
        return getNoRipple() ? LineDirection::incoming : LineDirection::outgoing;
    }

    [[nodiscard]] LineDirection
    getDirectionPeer() const
    {
        return getNoRipplePeer() ? LineDirection::incoming : LineDirection::outgoing;
    }

    /** Have we set the freeze flag on our peer */
    [[nodiscard]] bool
    getFreeze() const
    {
        return (mFlags & (mViewLowest ? lsfLowFreeze : lsfHighFreeze)) != 0u;
    }

    /** Have we set the deep freeze flag on our peer */
    [[nodiscard]] bool
    getDeepFreeze() const
    {
        return (mFlags & (mViewLowest ? lsfLowDeepFreeze : lsfHighDeepFreeze)) != 0u;
    }

    /** Has the peer set the freeze flag on us */
    [[nodiscard]] bool
    getFreezePeer() const
    {
        return (mFlags & (!mViewLowest ? lsfLowFreeze : lsfHighFreeze)) != 0u;
    }

    /** Has the peer set the deep freeze flag on us */
    [[nodiscard]] bool
    getDeepFreezePeer() const
    {
        return (mFlags & (!mViewLowest ? lsfLowDeepFreeze : lsfHighDeepFreeze)) != 0u;
    }

    [[nodiscard]] STAmount const&
    getBalance() const
    {
        return mBalance;
    }

    [[nodiscard]] STAmount const&
    getLimit() const
    {
        return mViewLowest ? mLowLimit : mHighLimit;
    }

    [[nodiscard]] STAmount const&
    getLimitPeer() const
    {
        return !mViewLowest ? mLowLimit : mHighLimit;
    }

    Json::Value
    getJson(int);

protected:
    uint256 key_;

    STAmount const mLowLimit;
    STAmount const mHighLimit;

    STAmount mBalance;

    std::uint32_t mFlags;

    bool mViewLowest;
};

// This wrapper is used for the path finder
class PathFindTrustLine final : public TrustLineBase, public CountedObject<PathFindTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    PathFindTrustLine() = delete;

    static std::optional<PathFindTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    static std::vector<PathFindTrustLine>
    getItems(AccountID const& accountID, ReadView const& view, LineDirection direction);
};

// This wrapper is used for the `AccountLines` command and includes the quality
// in and quality out values.
class RPCTrustLine final : public TrustLineBase, public CountedObject<RPCTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    RPCTrustLine() = delete;

    RPCTrustLine(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount);

    [[nodiscard]] Rate const&
    getQualityIn() const
    {
        return mViewLowest ? lowQualityIn_ : highQualityIn_;
    }

    [[nodiscard]] Rate const&
    getQualityOut() const
    {
        return mViewLowest ? lowQualityOut_ : highQualityOut_;
    }

    static std::optional<RPCTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    static std::vector<RPCTrustLine>
    getItems(AccountID const& accountID, ReadView const& view);

private:
    Rate lowQualityIn_;
    Rate lowQualityOut_;
    Rate highQualityIn_;
    Rate highQualityOut_;
};

}  // namespace xrpl
