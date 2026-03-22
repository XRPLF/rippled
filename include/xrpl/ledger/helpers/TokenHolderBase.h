#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/WrappedSLEBase.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

class TokenHolderBase : public virtual ReadOnlySLE
{
public:
    TokenHolderBase(
        ReadView const& view,
        std::shared_ptr<SLE const> sle,
        TokenBase const& token,
        AccountID const& holder)
        : ReadOnlySLE(sle, view), token_(token), holder_(holder), holderAccount_(holder, view)
    {
    }

    /** Constructor with explicit keylet (for when SLE lookup is needed) */
    TokenHolderBase(
        ReadView const& view,
        Keylet const& key,
        TokenBase const& token,
        AccountID const& holder)
        : ReadOnlySLE(key, view), token_(token), holder_(holder), holderAccount_(holder, view)
    {
    }

    TokenHolderBase() = delete;

    AccountID const&
    getHolder() const
    {
        return holder_;
    }

    TokenBase const&
    getToken() const
    {
        return token_;
    }

    [[nodiscard]] bool
    isFrozen(int depth = 0) const
    {
        return token_.isFrozen(holder_, depth);
    }

    [[nodiscard]] bool
    isDeepFrozen(int depth = 0) const
    {
        return token_.isDeepFrozen(holder_, depth);
    }

    [[nodiscard]] TER
    checkFrozen() const
    {
        return token_.checkFrozen(holder_);
    }

    [[nodiscard]] TER
    checkDeepFrozen() const
    {
        return token_.checkDeepFrozen(holder_);
    }

    STAmount
    accountHolds(
        FreezeHandling zeroIfFrozen,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const
    {
        return token_.accountHolds(holder_, zeroIfFrozen, j, includeFullBalance);
    }

    [[nodiscard]] STAmount
    accountHolds(
        FreezeHandling zeroIfFrozen,
        AuthHandling zeroIfUnauthorized,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const
    {
        return token_.accountHolds(
            holder_, zeroIfFrozen, zeroIfUnauthorized, j, includeFullBalance);
    }

    [[nodiscard]] TER
    requireAuth(AuthType authType = AuthType::Legacy, int depth = 0) const
    {
        return token_.requireAuth(holder_, authType, depth);
    }

    [[nodiscard]] TER
    canTransfer(AccountID const& to) const
    {
        return token_.canTransfer(holder_, to);
    }

    [[nodiscard]] TER
    canTransfer(TokenHolderBase const& to) const
    {
        return token_.canTransfer(holder_, to.getHolder());
    }

protected:
    TokenBase const& token_;
    AccountID const holder_;
    AccountRoot holderAccount_;
};

class WritableTokenHolderBase : public virtual TokenHolderBase, public virtual WritableSLE
{
public:
    WritableTokenHolderBase(
        ApplyView& view,
        std::shared_ptr<SLE> sle,
        WritableTokenBase& token,
        AccountID const& holder)
        : ReadOnlySLE(sle, view)
        , TokenHolderBase(view, sle, token, holder)
        , WritableSLE(sle, view)
        , writableToken_(token)
        , writableHolderAccount_(holder, view)
    {
    }

    /** Constructor with explicit keylet (for creation or lookup by key) */
    WritableTokenHolderBase(
        ApplyView& view,
        Keylet const& key,
        WritableTokenBase& token,
        AccountID const& holder)
        : ReadOnlySLE(key, view)
        , TokenHolderBase(view, key, token, holder)
        , WritableSLE(key, view)
        , writableToken_(token)
        , writableHolderAccount_(holder, view)
    {
    }

    WritableTokenBase&
    getWritableToken()
    {
        return writableToken_;
    }

protected:
    WritableTokenBase& writableToken_;
    WritableAccountRoot writableHolderAccount_;
};

}  // namespace xrpl
