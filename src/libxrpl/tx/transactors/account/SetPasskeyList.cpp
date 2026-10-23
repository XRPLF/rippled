#include <xrpl/tx/transactors/account/SetPasskeyList.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

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
    auto const& passkeys = ctx_.tx.getFieldArray(sfPasskeys);
    auto const passkeyKeylet = keylet::passkeyList(accountID_);

    // An existing list is replaced in place; its directory entry and reserve stay as they are.
    if (auto const sleList = ctx_.view().peek(passkeyKeylet))
    {
        sleList->setFieldArray(sfPasskeys, passkeys);
        ctx_.view().update(sleList);
        return tesSUCCESS;
    }

    auto const sleAccount = ctx_.view().peek(keylet::account(accountID_));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // A new list is one more owned object, so the account must cover one more reserve.
    auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
    if (balance < accountReserve(ctx_.view(), sleAccount, j_, {.ownerCountDelta = 1}))
        return tecINSUFFICIENT_RESERVE;

    auto const sle = std::make_shared<SLE>(passkeyKeylet);
    sle->setAccountID(sfOwner, accountID_);
    sle->setFieldArray(sfPasskeys, passkeys);

    auto page = ctx_.view().dirInsert(
        keylet::ownerDir(accountID_), sle->key(), describeOwnerDir(accountID_));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    (*sle)[sfOwnerNode] = *page;

    increaseOwnerCount(ctx_.view(), sleAccount, {}, 1, j_);

    ctx_.view().insert(sle);
    return tesSUCCESS;
}

void
SetPasskeyList::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
