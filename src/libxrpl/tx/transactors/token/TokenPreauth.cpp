#include <xrpl/tx/transactors/token/TokenPreauth.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

std::uint32_t
TokenPreauth::getFlagsMask(PreflightContext const&)
{
    return tfTokenPreauthMask;
}

NotTEC
TokenPreauth::preflight(PreflightContext const& ctx)
{
    auto const holder = ctx.tx[sfHolder];
    if (!holder)
        return temINVALID_ACCOUNT_ID;

    if (holder == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
TokenPreauth::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const holder = ctx.tx[sfHolder];
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];

    // Only the issuer manages these entries. The issuer is read from the
    // issuance ID rather than the issuance, so an issuer can still clean up
    // (and recover the reserve for) entries of a destroyed issuance.
    if (MPTIssue{issuanceID}.getIssuer() != account)
        return tecNO_PERMISSION;

    bool const preauthorized = ctx.view.exists(keylet::tokenPreauth(holder, issuanceID));

    if (ctx.tx.isFlag(tfUnauthorize))
        return preauthorized ? TER{tesSUCCESS} : TER{tecNO_ENTRY};

    if (!ctx.view.exists(keylet::mptokenIssuance(issuanceID)))
        return tecOBJECT_NOT_FOUND;

    // Pseudo-accounts (AMM, Vault, LoanBroker) hold tokens on behalf of their
    // participants and are always implicitly authorized.
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (sleHolder && isPseudoAccount(sleHolder))
        return tecPSEUDO_ACCOUNT;

    if (!sleHolder)
        return tecNO_DST;

    // A block is only ever removed by TokenBlock with tfUnblock.
    if (ctx.view.exists(keylet::tokenBlock(holder, issuanceID)))
        return tecNO_PERMISSION;

    if (preauthorized)
        return tecDUPLICATE;

    return tesSUCCESS;
}

TER
TokenPreauth::doApply()
{
    auto const holder = ctx_.tx[sfHolder];
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const preauthKeylet = keylet::tokenPreauth(holder, issuanceID);

    // Unauthorizing does not revoke an existing MPToken's authorization; that
    // stays a separate, explicit issuer transaction.
    if (ctx_.tx.isFlag(tfUnauthorize))
        return removeFromLedger(view(), view().peek(preauthKeylet), j_);

    return createEntry(ctx_, preFeeBalance_, preauthKeylet, j_);
}

TER
TokenPreauth::createEntry(
    ApplyContext& ctx,
    XRPAmount preFeeBalance,
    Keylet const& keylet,
    beast::Journal j)
{
    auto& view = ctx.view();
    AccountID const account = ctx.tx[sfAccount];
    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const applyViewContext = ctx.getApplyViewContext();

    // The entry counts against the issuer's reserve, but we check the
    // starting balance so the issuer may dip into its reserve for fees.
    if (auto const ret =
            checkReserve(applyViewContext, sleOwner, preFeeBalance, {.ownerCountDelta = 1}, j);
        !isTesSuccess(ret))
        return ret;

    auto const sle = std::make_shared<SLE>(keylet);
    sle->setAccountID(sfAccount, account);
    sle->setAccountID(sfHolder, ctx.tx[sfHolder]);
    sle->setFieldH192(sfMPTokenIssuanceID, ctx.tx[sfMPTokenIssuanceID]);

    auto const page = view.dirInsert(keylet::ownerDir(account), keylet, describeOwnerDir(account));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    sle->setFieldU64(sfOwnerNode, *page);
    view.insert(sle);

    increaseOwnerCount(applyViewContext, sleOwner, 1, j);
    return tesSUCCESS;
}

TER
TokenPreauth::removeFromLedger(ApplyView& view, SLE::Ref sle, beast::Journal j)
{
    // Existence already checked in preclaim and AccountDelete
    if (!sle)
        return tecNO_ENTRY;  // LCOV_EXCL_LINE

    AccountID const owner = (*sle)[sfAccount];
    if (!view.dirRemove(keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete TokenPreauth/TokenBlock from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view, sleOwner, sle, 1, j);
    view.erase(sle);
    return tesSUCCESS;
}

void
TokenPreauth::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // Checked for every transaction by the ValidTokenPreauth invariant.
}

bool
TokenPreauth::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
