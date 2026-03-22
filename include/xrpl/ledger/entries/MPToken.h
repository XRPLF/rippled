#pragma once

#include <xrpl/ledger/entries/MPTokenHelpers.h>
#include <xrpl/ledger/entries/TokenHolderBase.h>

namespace xrpl {

class MPToken : public virtual TokenHolderBase
{
public:
    MPToken(ReadView const& view, MPTokenIssuance const& issuance, AccountID const& holder)
        : ReadOnlySLE(view.read(keylet::mptoken(issuance.getMptID(), holder)), view)
        , TokenHolderBase(
              view,
              view.read(keylet::mptoken(issuance.getMptID(), holder)),
              issuance,
              holder)
        , issuance_(issuance)
    {
    }

    MPTokenIssuance const&
    getIssuance() const
    {
        return issuance_;
    }

protected:
    MPTokenIssuance const& issuance_;
};

class WritableMPToken : public virtual WritableTokenHolderBase, public virtual MPToken
{
public:
    WritableMPToken(ApplyView& view, WritableMPTokenIssuance& issuance, AccountID const& holder)
        : ReadOnlySLE(view.peek(keylet::mptoken(issuance.getMptID(), holder)), view)
        , TokenHolderBase(
              view,
              view.peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance,
              holder)
        , WritableSLE(view.peek(keylet::mptoken(issuance.getMptID(), holder)), view)
        , WritableTokenHolderBase(
              view,
              view.peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance,
              holder)
        , MPToken(view, issuance, holder)
        , writableIssuance_(issuance)
    {
    }

    // Resolve ambiguity: use writable operator-> for non-const, read-only for const
    using WritableSLE::operator->;
    using MPToken::operator->;
    using WritableSLE::operator*;
    using MPToken::operator*;

    WritableMPTokenIssuance&
    getWritableIssuance()
    {
        return writableIssuance_;
    }

    static TER
    createMPToken(
        ApplyView& view,
        WritableMPTokenIssuance& issuance,
        AccountID const& account,
        std::uint32_t const flags)
    {
        WritableMPToken mptoken(view, issuance, account);

        auto const ownerNode =
            view.dirInsert(keylet::ownerDir(account), mptoken.key(), describeOwnerDir(account));

        if (!ownerNode)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        mptoken.newSLE();

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = issuance.getMptID();
        (*mptoken)[sfFlags] = flags;
        (*mptoken)[sfOwnerNode] = *ownerNode;

        mptoken.insert();

        return tesSUCCESS;
    }

protected:
    WritableMPTokenIssuance& writableIssuance_;
};

}  // namespace xrpl
