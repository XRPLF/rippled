#include <xrpl/tx/transactors/firewall/WithdrawPreauth.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

std::uint32_t
WithdrawPreauth::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

XRPAmount
WithdrawPreauth::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    std::size_t const signerCount = [&counterSig]() -> std::size_t {
        if (counterSig.isFieldPresent(sfSigners))
            return counterSig.getFieldArray(sfSigners).size();
        return counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0;
    }();

    return normalCost + (view.fees().base * signerCount);
}

NotTEC
WithdrawPreauth::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const& j = ctx.j;

    auto const optAuth = tx[~sfAuthorize];
    auto const optUnauth = tx[~sfUnauthorize];
    if (static_cast<bool>(optAuth) == static_cast<bool>(optUnauth))
    {
        JLOG(j.trace()) << "WithdrawPreauth: exactly one of sfAuthorize and "
                           "sfUnauthorize is required";
        return temMALFORMED;
    }

    AccountID const target{optAuth ? *optAuth : *optUnauth};
    if (target == beast::kZero)
    {
        JLOG(j.trace()) << "WithdrawPreauth: the authorized account is zeroed";
        return temINVALID_ACCOUNT_ID;
    }

    if (optAuth && (target == tx[sfAccount]))
    {
        JLOG(j.trace()) << "WithdrawPreauth: an account may not preauthorize itself";
        return temCANNOT_PREAUTH_SELF;
    }

    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    if (auto const ret = detail::preflightCheckSigningKey(counterSig, j))
        return ret;

    return tesSUCCESS;
}

TER
WithdrawPreauth::preclaim(PreclaimContext const& ctx)
{
    AccountID const accountID = ctx.tx[sfAccount];
    std::uint32_t const dtag = ctx.tx[~sfDestinationTag].value_or(0);

    if (ctx.tx.isFieldPresent(sfAuthorize))
    {
        AccountID const auth{ctx.tx[sfAuthorize]};
        if (!ctx.view.exists(keylet::account(auth)))
            return tecNO_TARGET;

        if (ctx.view.exists(keylet::withdrawPreauth(accountID, auth, dtag)))
            return tecDUPLICATE;
    }
    else
    {
        AccountID const unauth{ctx.tx[sfUnauthorize]};
        if (!ctx.view.exists(keylet::withdrawPreauth(accountID, unauth, dtag)))
            return tecNO_ENTRY;
    }

    auto const sleFirewall = ctx.view.read(keylet::firewall(accountID));
    if (!sleFirewall)
    {
        JLOG(ctx.j.trace()) << "WithdrawPreauth: the account has no firewall";
        return tecNO_TARGET;
    }

    if (sleFirewall->key() != ctx.tx.getFieldH256(sfFirewallID))
    {
        JLOG(ctx.j.trace()) << "WithdrawPreauth: sfFirewallID is not the account's firewall";
        return tecNO_PERMISSION;
    }

    return Transactor::checkSign(
        ctx.view,
        ctx.flags,
        ctx.parentBatchId,
        sleFirewall->getAccountID(sfCounterparty),
        ctx.tx.getFieldObject(sfCounterpartySignature),
        ctx.j);
}

TER
WithdrawPreauth::doApply()
{
    std::uint32_t const dtag = ctx_.tx[~sfDestinationTag].value_or(0);

    if (!ctx_.tx.isFieldPresent(sfAuthorize))
    {
        auto const preauth = keylet::withdrawPreauth(accountID_, ctx_.tx[sfUnauthorize], dtag);
        return WithdrawPreauth::removeFromLedger(view(), preauth.key, j_);
    }

    auto applyViewContext = ctx_.getApplyViewContext();

    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // The preauthorization counts against the owner's reserve, checked against
    // the starting balance so the fee may still dip into it.
    if (auto const ret =
            checkReserve(applyViewContext, sleOwner, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    AccountID const auth{ctx_.tx[sfAuthorize]};
    Keylet const preauthKeylet = keylet::withdrawPreauth(accountID_, auth, dtag);
    auto const slePreauth = std::make_shared<SLE>(preauthKeylet);

    slePreauth->setAccountID(sfAccount, accountID_);
    slePreauth->setAccountID(sfAuthorize, auth);
    slePreauth->setFieldU32(sfDestinationTag, dtag);
    view().insert(slePreauth);

    auto const page =
        view().dirInsert(keylet::ownerDir(accountID_), preauthKeylet, describeOwnerDir(accountID_));

    JLOG(j_.trace()) << "WithdrawPreauth: adding " << to_string(preauthKeylet.key)
                     << " to the owner directory: " << (page ? "success" : "failure");

    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    slePreauth->setFieldU64(sfOwnerNode, *page);
    increaseOwnerCount(applyViewContext, sleOwner, 1, j_);
    addSponsorToLedgerEntry(applyViewContext, slePreauth);

    return tesSUCCESS;
}

TER
WithdrawPreauth::removeFromLedger(ApplyView& view, uint256 const& preauthIndex, beast::Journal j)
{
    auto const slePreauth = view.peek(keylet::withdrawPreauth(preauthIndex));
    if (!slePreauth)
    {
        JLOG(j.warn()) << "WithdrawPreauth: the preauthorization does not exist";
        return tecNO_ENTRY;
    }

    AccountID const account{(*slePreauth)[sfAccount]};
    std::uint64_t const page{(*slePreauth)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), page, preauthIndex, false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "WithdrawPreauth: could not remove it from the owner directory";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view, sleOwner, slePreauth, 1, j);
    view.erase(slePreauth);

    return tesSUCCESS;
}

void
WithdrawPreauth::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
WithdrawPreauth::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
