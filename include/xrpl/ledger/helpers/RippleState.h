#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHolderBase.h>

namespace xrpl {

class RippleState : public virtual TokenHolderBase
{
public:
    RippleState(IOUToken const& token, AccountID const& holder)
        : ReadOnlySLE(
              token.readView().read(keylet::line(holder, token.getIssuer(), token.getCurrency())),
              token.readView())
        , TokenHolderBase(
              token.readView(),
              token.readView().read(keylet::line(holder, token.getIssuer(), token.getCurrency())),
              token,
              holder)
        , iouToken_(token)
    {
    }

    /** Constructor with explicit SLE (for when SLE is already available) */
    RippleState(
        ReadView const& view,
        std::shared_ptr<SLE const> sle,
        IOUToken const& token,
        AccountID const& holder)
        : ReadOnlySLE(sle, view), TokenHolderBase(view, sle, token, holder), iouToken_(token)
    {
    }

    IOUToken const&
    getIOUToken() const
    {
        return iouToken_;
    }

protected:
    IOUToken const& iouToken_;
};

class WritableRippleState : public virtual WritableTokenHolderBase, public virtual RippleState
{
public:
    WritableRippleState(ApplyView& view, WritableIOUToken& token, AccountID const& holder)
        : ReadOnlySLE(view.peek(keylet::line(holder, token.getIssuer(), token.getCurrency())), view)
        , TokenHolderBase(
              view,
              view.peek(keylet::line(holder, token.getIssuer(), token.getCurrency())),
              token,
              holder)
        , WritableSLE(view.peek(keylet::line(holder, token.getIssuer(), token.getCurrency())), view)
        , WritableTokenHolderBase(
              view,
              view.peek(keylet::line(holder, token.getIssuer(), token.getCurrency())),
              token,
              holder)
        , RippleState(token, holder)
        , writableIOUToken_(token)
    {
    }

    /** Constructor with explicit SLE (for creation or when SLE is already available) */
    WritableRippleState(
        ApplyView& view,
        std::shared_ptr<SLE> sle,
        WritableIOUToken& token,
        AccountID const& holder)
        : ReadOnlySLE(sle, view)
        , TokenHolderBase(view, sle, token, holder)
        , WritableSLE(sle, view)
        , WritableTokenHolderBase(view, sle, token, holder)
        , RippleState(view, sle, token, holder)
        , writableIOUToken_(token)
    {
    }

    // Resolve ambiguity: use writable operator-> for non-const, read-only for const
    using WritableSLE::operator->;
    using RippleState::operator->;
    using WritableSLE::operator*;
    using RippleState::operator*;

    WritableIOUToken&
    getWritableIOUToken()
    {
        return writableIOUToken_;
    }

    static Expected<WritableRippleState, TER>
    makeNew(WritableIOUToken& token, AccountID const& accountID, beast::Journal journal)
    {
        auto const ter = token.addEmptyHolding(accountID, XRPAmount{0}, journal);
        if (ter != tesSUCCESS)
            return Unexpected(ter);
        return WritableRippleState{token.applyView(), token, accountID};
    }

    //--------------------------------------------------------------------------
    //
    // Trust line operations
    //
    //--------------------------------------------------------------------------

    /** Create a trust line

        This can set an initial balance.
    */
    [[nodiscard]] static TER
    trustCreate(
        ApplyView& view,
        bool const bSrcHigh,
        AccountID const& uSrcAccountID,
        AccountID const& uDstAccountID,
        uint256 const& uIndex,             // ripple state entry
        WritableAccountRoot& wrappedAcct,  // the account being set.
        bool const bAuth,                  // authorize account.
        bool const bNoRipple,              // others cannot ripple through
        bool const bFreeze,                // funds cannot leave
        bool bDeepFreeze,                  // can neither receive nor send funds
        STAmount const& saBalance,         // balance of account being set.
                                           // Issuer should be noAccount()
        STAmount const& saLimit,           // limit for account being set.
                                           // Issuer should be the account being set.
        std::uint32_t uQualityIn,
        std::uint32_t uQualityOut,
        beast::Journal j);

    [[nodiscard]] static TER
    trustDelete(
        ApplyView& view,
        std::shared_ptr<SLE> const& sleRippleState,
        AccountID const& uLowAccountID,
        AccountID const& uHighAccountID,
        beast::Journal j);

protected:
    WritableIOUToken& writableIOUToken_;
};

}  // namespace xrpl
