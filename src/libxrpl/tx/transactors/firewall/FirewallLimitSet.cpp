#include <xrpl/tx/transactors/firewall/FirewallLimitSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
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

namespace {

// Whether the change widens what can leave the account, which needs the
// counterparty's signature.
bool
isLoosening(
    ReadView const& view,
    STTx const& tx,
    SLE const& firewall,
    SLE::const_pointer const& limit)
{
    bool const refusesUnlisted = firewall[~sfUnlistedAssets].value_or(0) == 1;

    if (!limit)
        return refusesUnlisted;

    if (tx.isFlag(tfLimitDelete))
    {
        if (!limit->isFieldPresent(sfLimitSigner))
            return !refusesUnlisted;

        // Without an account-wide limit, deleting a signer's limit leaves the
        // signer refused on the asset only while unlisted assets are refused.
        return !refusesUnlisted ||
            view.exists(keylet::firewallLimit(tx.getAccountID(sfAccount), tx[sfAsset]));
    }

    return tx[sfLimitAmount] > (*limit)[sfLimitAmount] || tx[sfPeriod] < (*limit)[sfPeriod];
}

// Writes a count on the firewall, absent when zero.
void
setCount(SLE& firewall, SF_UINT32 const& field, std::uint32_t value)
{
    if (value == 0)
    {
        firewall.makeFieldAbsent(field);
    }
    else
    {
        firewall.setFieldU32(field, value);
    }
}

}  // namespace

bool
FirewallLimitSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureFirewall);
}

std::uint32_t
FirewallLimitSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfFirewallLimitSetMask;
}

XRPAmount
FirewallLimitSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx) + calculateCounterpartyFee(view, tx);
}

NotTEC
FirewallLimitSet::preflight(PreflightContext const& ctx)
{
    // The counterparty's consent is its signature, which an inner Batch
    // transaction cannot carry.
    if (ctx.tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    bool const hasAmount = ctx.tx.isFieldPresent(sfLimitAmount);
    bool const hasPeriod = ctx.tx.isFieldPresent(sfPeriod);
    if (ctx.tx.isFlag(tfLimitDelete))
    {
        if (hasAmount || hasPeriod)
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: a delete carries no LimitAmount or Period";
            return temMALFORMED;
        }
    }
    else
    {
        if (!hasAmount || !hasPeriod)
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: LimitAmount and Period are required";
            return temMALFORMED;
        }

        STAmount const amount = ctx.tx[sfLimitAmount];
        if (amount.asset() != ctx.tx[sfAsset] || amount.negative() || !isLegalNet(amount) ||
            !isLegalMPT(amount))
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: LimitAmount is invalid";
            return temBAD_AMOUNT;
        }

        std::uint32_t const period = ctx.tx[sfPeriod];
        if (period < kFirewallLimitMinPeriod || period > kFirewallLimitMaxPeriod)
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: Period is out of range";
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFieldPresent(sfCounterpartySignature))
    {
        if (auto const ret = detail::preflightCheckSigningKey(
                ctx.tx.getFieldObject(sfCounterpartySignature), ctx.j))
            return ret;
    }

    return tesSUCCESS;
}

TER
FirewallLimitSet::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    auto const sleFirewall = ctx.view.read(keylet::firewall(account));
    if (!sleFirewall)
    {
        JLOG(ctx.j.trace()) << "FirewallLimitSet: the account has no firewall";
        return tecNO_TARGET;
    }

    auto const sleLimit =
        ctx.view.read(keylet::firewallLimit(account, ctx.tx[sfAsset], ctx.tx[~sfLimitSigner]));
    if (ctx.tx.isFlag(tfLimitDelete) && !sleLimit)
    {
        JLOG(ctx.j.trace()) << "FirewallLimitSet: the limit to delete does not exist";
        return tecNO_ENTRY;
    }

    if (!ctx.tx.isFieldPresent(sfCounterpartySignature))
    {
        if (isLoosening(ctx.view, ctx.tx, *sleFirewall, sleLimit))
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: a loosening needs the counterparty's "
                                   "signature";
            return temBAD_SIGNER;
        }
        return tesSUCCESS;
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
FirewallLimitSet::doApply()
{
    auto const sleOwner = view().peek(keylet::account(accountID_));
    auto const sleFirewall = view().peek(keylet::firewall(accountID_));
    if (!sleOwner || !sleFirewall)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const asset = ctx_.tx[sfAsset];
    auto const limitSigner = ctx_.tx[~sfLimitSigner];
    Keylet const limitKeylet = keylet::firewallLimit(accountID_, asset, limitSigner);
    auto const sleLimit = view().peek(limitKeylet);
    SLE const& firewall = *sleFirewall;
    std::uint32_t const limitCount = firewall[~sfLimitCount].value_or(0);
    std::uint32_t const signerLimitCount = firewall[~sfSignerLimitCount].value_or(0);

    if (ctx_.tx.isFlag(tfLimitDelete))
    {
        if (!sleLimit)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (!view().dirRemove(
                keylet::ownerDir(accountID_), (*sleLimit)[sfOwnerNode], sleLimit->key(), false))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "FirewallLimitSet: could not remove the limit from the owner dir";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        decreaseOwnerCountForObject(view(), sleOwner, sleLimit, 1, j_);
        view().erase(sleLimit);

        setCount(*sleFirewall, sfLimitCount, limitCount - 1);
        if (limitSigner)
            setCount(*sleFirewall, sfSignerLimitCount, signerLimitCount - 1);
        view().update(sleFirewall);
        return tesSUCCESS;
    }

    auto const now = view().parentCloseTime();

    if (sleLimit)
    {
        // What was spent in the current period is kept across a change.
        auto const period = firewallLimitPeriod(*sleLimit, now);
        sleLimit->setFieldAmount(sfSpent, period.spent);
        sleLimit->setFieldU32(sfStartDate, period.startDate);
        sleLimit->setFieldAmount(sfLimitAmount, ctx_.tx[sfLimitAmount]);
        sleLimit->setFieldU32(sfPeriod, ctx_.tx[sfPeriod]);
        view().update(sleLimit);
        return tesSUCCESS;
    }

    auto const applyViewContext = ctx_.getApplyViewContext();
    if (auto const ret =
            checkReserve(applyViewContext, sleOwner, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    auto const sleNew = std::make_shared<SLE>(limitKeylet);
    sleNew->setAccountID(sfOwner, accountID_);
    sleNew->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    if (limitSigner)
        sleNew->setAccountID(sfLimitSigner, *limitSigner);
    sleNew->setFieldAmount(sfLimitAmount, ctx_.tx[sfLimitAmount]);
    sleNew->setFieldU32(sfPeriod, ctx_.tx[sfPeriod]);
    sleNew->setFieldAmount(sfSpent, STAmount{asset});
    sleNew->setFieldU32(sfStartDate, now.time_since_epoch().count());
    view().insert(sleNew);

    if (auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), limitKeylet, describeOwnerDir(accountID_)))
    {
        sleNew->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    }

    increaseOwnerCount(applyViewContext, sleOwner, 1, j_);
    addSponsorToLedgerEntry(applyViewContext, sleNew);

    setCount(*sleFirewall, sfLimitCount, limitCount + 1);
    if (limitSigner)
        setCount(*sleFirewall, sfSignerLimitCount, signerLimitCount + 1);
    view().update(sleFirewall);
    return tesSUCCESS;
}

void
FirewallLimitSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
FirewallLimitSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
