#pragma once

#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHolderBase.h>

namespace xrpl {

class MPToken : public virtual TokenHolderBase
{
public:
    MPToken(MPTokenIssuance const& issuance, AccountID const& holder)
        : ReadOnlySLE(
              issuance.readView().read(keylet::mptoken(issuance.getMptID(), holder)),
              issuance.readView())
        , TokenHolderBase(
              issuance.readView(),
              issuance.readView().read(keylet::mptoken(issuance.getMptID(), holder)),
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
    WritableMPToken(WritableMPTokenIssuance& issuance, AccountID const& holder)
        : ReadOnlySLE(
              issuance.applyView().peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance.applyView())
        , TokenHolderBase(
              issuance.applyView(),
              issuance.applyView().peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance,
              holder)
        , WritableSLE(
              issuance.applyView().peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance.applyView())
        , WritableTokenHolderBase(
              issuance.applyView(),
              issuance.applyView().peek(keylet::mptoken(issuance.getMptID(), holder)),
              issuance,
              holder)
        , MPToken(issuance, holder)
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
        WritableMPTokenIssuance& issuance,
        AccountID const& account,
        std::uint32_t const flags)
    {
        WritableMPToken mptoken(issuance, account);

        auto const ownerNode = mptoken.applyView().dirInsert(
            keylet::ownerDir(account), mptoken.key(), describeOwnerDir(account));

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
