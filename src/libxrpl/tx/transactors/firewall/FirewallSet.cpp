#include <xrpl/tx/transactors/firewall/FirewallSet.h>

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
#include <xrpl/protocol/STAmount.h>
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
FirewallSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

XRPAmount
FirewallSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    // Each signature in the counterparty's signature, single or multi, adds one
    // base fee. getFieldObject returns an empty object when the field is
    // absent, which is the create case.
    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    std::size_t const signerCount = [&counterSig]() -> std::size_t {
        if (counterSig.isFieldPresent(sfSigners))
            return counterSig.getFieldArray(sfSigners).size();
        return counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0;
    }();

    return normalCost + (view.fees().base * signerCount);
}

NotTEC
FirewallSet::preflight(PreflightContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    bool const isCreate = !ctx.tx.isFieldPresent(sfFirewallID);

    if (isCreate)
    {
        if (!ctx.tx.isFieldPresent(sfCounterparty))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterparty is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfCounterparty))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterparty must not be the account";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfBackup is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfBackup))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfBackup must not be the account";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfCounterpartySignature))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfCounterpartySignature is not allowed for creation";
            return temMALFORMED;
        }
    }
    else
    {
        if (ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfBackup is not allowed for an update";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfCounterparty) && account == ctx.tx.getAccountID(sfCounterparty))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterparty must not be the account";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfCounterpartySignature))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterpartySignature is required for an update";
            return temBAD_SIGNER;
        }

        auto const counterSig = ctx.tx.getFieldObject(sfCounterpartySignature);
        if (auto const ret = detail::preflightCheckSigningKey(counterSig, ctx.j))
            return ret;
    }

    if (ctx.tx.isFieldPresent(sfMaxFee))
    {
        auto const maxFee = ctx.tx.getFieldAmount(sfMaxFee);
        if (!maxFee.native() || maxFee.negative() || !isLegalNet(maxFee))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfMaxFee is invalid";
            return temBAD_AMOUNT;
        }
    }

    return tesSUCCESS;
}

TER
FirewallSet::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);

    if (!ctx.tx.isFieldPresent(sfFirewallID))
    {
        if (ctx.view.exists(keylet::firewall(account)))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: a firewall already exists for the account";
            return tecDUPLICATE;
        }

        if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfCounterparty))))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: the counterparty account does not exist";
            return tecNO_DST;
        }

        if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfBackup))))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: the backup account does not exist";
            return tecNO_DST;
        }

        return tesSUCCESS;
    }

    auto const sleFirewall = ctx.view.read(keylet::firewall(ctx.tx.getFieldH256(sfFirewallID)));
    if (!sleFirewall)
    {
        JLOG(ctx.j.trace()) << "FirewallSet: the firewall was not found";
        return tecNO_TARGET;
    }

    if (sleFirewall->getAccountID(sfOwner) != account)
    {
        JLOG(ctx.j.trace()) << "FirewallSet: the account does not own the firewall";
        return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfCounterparty))
    {
        AccountID const newCounterparty = ctx.tx.getAccountID(sfCounterparty);
        if (sleFirewall->getAccountID(sfCounterparty) == newCounterparty)
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterparty matches the recorded counterparty";
            return tecDUPLICATE;
        }

        if (!ctx.view.exists(keylet::account(newCounterparty)))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: the new counterparty account does not exist";
            return tecNO_DST;
        }
    }

    // The counterparty recorded on the firewall authorizes the update, not
    // whoever the transaction names.
    return Transactor::checkSign(
        ctx.view,
        ctx.flags,
        ctx.parentBatchId,
        sleFirewall->getAccountID(sfCounterparty),
        ctx.tx.getFieldObject(sfCounterpartySignature),
        ctx.j);
}

TER
FirewallSet::createFirewall(std::shared_ptr<SLE> const& sleOwner)
{
    auto applyViewContext = ctx_.getApplyViewContext();

    // A create inserts the firewall and the backup preauthorization.
    if (auto const ret =
            checkReserve(applyViewContext, sleOwner, preFeeBalance_, {.ownerCountDelta = 2}, j_);
        !isTesSuccess(ret))
        return ret;

    auto const sleFirewall = std::make_shared<SLE>(keylet::firewall(accountID_));
    sleFirewall->setAccountID(sfOwner, accountID_);
    sleFirewall->setAccountID(sfCounterparty, ctx_.tx.getAccountID(sfCounterparty));
    if (ctx_.tx.isFieldPresent(sfMaxFee))
        sleFirewall->setFieldAmount(sfMaxFee, ctx_.tx.getFieldAmount(sfMaxFee));
    view().insert(sleFirewall);

    if (auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), sleFirewall->key(), describeOwnerDir(accountID_)))
    {
        sleFirewall->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    }

    increaseOwnerCount(applyViewContext, sleOwner, 1, j_);
    addSponsorToLedgerEntry(applyViewContext, sleFirewall);

    // The backup destination is preauthorized as the firewall is created, so
    // the account always has one destination it can still reach.
    AccountID const backup = ctx_.tx.getAccountID(sfBackup);
    std::uint32_t const dtag = ctx_.tx[~sfDestinationTag].value_or(0);
    Keylet const preauthKeylet = keylet::withdrawPreauth(accountID_, backup, dtag);
    auto const slePreauth = std::make_shared<SLE>(preauthKeylet);
    slePreauth->setAccountID(sfAccount, accountID_);
    slePreauth->setAccountID(sfAuthorize, backup);
    slePreauth->setFieldU32(sfDestinationTag, dtag);
    view().insert(slePreauth);

    if (auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), preauthKeylet, describeOwnerDir(accountID_)))
    {
        slePreauth->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    }

    increaseOwnerCount(applyViewContext, sleOwner, 1, j_);
    addSponsorToLedgerEntry(applyViewContext, slePreauth);

    return tesSUCCESS;
}

TER
FirewallSet::updateFirewall()
{
    auto const sleFirewall = view().peek(keylet::firewall(ctx_.tx.getFieldH256(sfFirewallID)));
    if (!sleFirewall)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.tx.isFieldPresent(sfCounterparty))
        sleFirewall->setAccountID(sfCounterparty, ctx_.tx.getAccountID(sfCounterparty));

    // A zero MaxFee removes the cap.
    if (ctx_.tx.isFieldPresent(sfMaxFee))
    {
        if (ctx_.tx.getFieldAmount(sfMaxFee) == beast::kZero)
            sleFirewall->makeFieldAbsent(sfMaxFee);
        else
            sleFirewall->setFieldAmount(sfMaxFee, ctx_.tx.getFieldAmount(sfMaxFee));
    }

    view().update(sleFirewall);
    return tesSUCCESS;
}

TER
FirewallSet::doApply()
{
    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!ctx_.tx.isFieldPresent(sfFirewallID))
        return createFirewall(sleOwner);

    return updateFirewall();
}

void
FirewallSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
FirewallSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
