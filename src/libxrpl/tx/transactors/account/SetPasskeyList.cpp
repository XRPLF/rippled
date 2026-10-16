#include <xrpl/tx/transactors/account/SetPasskeyList.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <set>

namespace xrpl {

NotTEC
SetPasskeyList::preflight(PreflightContext const& ctx)
{
    auto const& passkeys = ctx.tx.getFieldArray(sfPasskeys);

    if (passkeys.empty())
    {
        JLOG(ctx.j.debug()) << "SetPasskeyList: empty passkeys array.";
        return temMALFORMED;
    }

    // Validate each passkey entry and check for duplicates
    std::set<Blob> seenPasskeyIDs;
    std::set<Blob> seenPublicKeys;
    for (auto const& passkey : passkeys)
    {
        if (!passkey.isFieldPresent(sfPasskeyID) || !passkey.isFieldPresent(sfPublicKey))
        {
            JLOG(ctx.j.debug()) << "SetPasskeyList: missing required fields.";
            return temMALFORMED;
        }

        // Check for duplicate PasskeyIDs
        auto const passkeyID = passkey.getFieldVL(sfPasskeyID);
        if (!seenPasskeyIDs.insert(passkeyID).second)
        {
            JLOG(ctx.j.debug()) << "SetPasskeyList: duplicate PasskeyID.";
            return temMALFORMED;
        }

        // Check for duplicate PublicKeys
        auto const pk = passkey.getFieldVL(sfPublicKey);
        if (!seenPublicKeys.insert(pk).second)
        {
            JLOG(ctx.j.debug()) << "SetPasskeyList: duplicate PublicKey.";
            return temMALFORMED;
        }

        // Validate public key is a valid P256 key
        auto const keyType = publicKeyType(makeSlice(pk));
        if (!keyType || *keyType != KeyType::P256)
        {
            JLOG(ctx.j.debug()) << "SetPasskeyList: invalid P256 public key.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
SetPasskeyList::doApply()
{
    auto viewJ = ctx_.registry.get().getJournal("View");
    auto const sleAccount = ctx_.view().peek(keylet::account(accountID_));
    if (!sleAccount)
        return tecINTERNAL;

    auto const passkeyKeylet = keylet::passkeyList(accountID_);
    auto sle = std::make_shared<SLE>(passkeyKeylet);
    sle->setAccountID(sfOwner, ctx_.tx.getAccountID(sfAccount));
    auto const& passkeys = ctx_.tx.getFieldArray(sfPasskeys);
    sle->setFieldArray(sfPasskeys, passkeys);

    auto page = ctx_.view().dirInsert(
        keylet::ownerDir(accountID_), sle->key(), describeOwnerDir(accountID_));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    (*sle)[sfOwnerNode] = *page;

    increaseOwnerCount(ctx_.view(), sleAccount, {}, 1, viewJ);

    ctx_.view().insert(sle);
    return tesSUCCESS;
}

void
SetPasskeyList::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
SetPasskeyList::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
