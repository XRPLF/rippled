#include <xrpl/tx/transactors/firewall/FirewallLimitSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>

namespace xrpl {

namespace {

// Whether the change widens what can leave the account, which needs the
// counterparty's signature.
bool
isLoosening(STTx const& tx, SLE const& firewall, std::optional<STObject> const& limit)
{
    bool const refusesUnlisted = firewall[~sfUnlistedAssets].value_or(0) == 1;

    if (!limit)
        return refusesUnlisted;

    if (tx.isFlag(tfLimitDelete))
        return !refusesUnlisted;

    return tx[sfLimitAmount] > (*limit)[sfLimitAmount] || tx[sfPeriod] < (*limit)[sfPeriod];
}

// Whether the account issues the asset.
bool
isIssuedBy(Asset const& asset, AccountID const& account)
{
    if (asset.native())
        return false;
    return asset.visit(
        [&account](Issue const& issue) { return issue.account == account; },
        [&account](MPTIssue const& issue) { return issue.getIssuer() == account; });
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

    if (isIssuedBy(ctx.tx[sfAsset], ctx.tx.getAccountID(sfAccount)))
    {
        JLOG(ctx.j.trace()) << "FirewallLimitSet: an asset the account issues has no limit";
        return temBAD_ISSUER;
    }

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

    Asset const asset = ctx.tx[sfAsset];
    auto const location = firewallLimitField(account, asset);
    if (!location)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleHolding = ctx.view.read(location->entry);
    if (asset.holds<Issue>() && !asset.native())
    {
        // The owner's side of the line holds nothing without its reserve flag.
        bool const ownerIsLow = location->field == &sfLowFirewallLimit;
        if (!sleHolding || !sleHolding->isFlag(ownerIsLow ? lsfLowReserve : lsfHighReserve))
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: the account holds no line of the asset";
            return tecNO_LINE;
        }

        // A limit on an LP token's line would keep the AMM from being deleted.
        auto const sleIssuer = ctx.view.read(keylet::account(asset.get<Issue>().account));
        if (sleIssuer && sleIssuer->isFieldPresent(sfAMMID))
        {
            JLOG(ctx.j.trace()) << "FirewallLimitSet: an AMM's LP token takes no limit";
            return tecNO_PERMISSION;
        }
    }
    else if (!sleHolding)
    {
        JLOG(ctx.j.trace()) << "FirewallLimitSet: the account holds none of the asset";
        return tecOBJECT_NOT_FOUND;
    }

    std::optional<STObject> limit;
    if (sleHolding->isFieldPresent(*location->field))
        limit = sleHolding->getFieldObject(*location->field);

    if (ctx.tx.isFlag(tfLimitDelete) && !limit)
    {
        JLOG(ctx.j.trace()) << "FirewallLimitSet: the limit to delete does not exist";
        return tecNO_ENTRY;
    }

    if (!ctx.tx.isFieldPresent(sfCounterpartySignature))
    {
        if (isLoosening(ctx.tx, *sleFirewall, limit))
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
    auto const location = firewallLimitField(accountID_, ctx_.tx[sfAsset]);
    if (!location)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleHolding = view().peek(location->entry);
    auto const sleFirewall = view().peek(keylet::firewall(accountID_));
    if (!sleHolding || !sleFirewall)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    SField const& field = *location->field;
    std::uint32_t const limitCount = (*sleFirewall)[~sfLimitCount].valueOr(0);

    if (ctx_.tx.isFlag(tfLimitDelete))
    {
        if (!sleHolding->isFieldPresent(field))
            return tefINTERNAL;  // LCOV_EXCL_LINE

        sleHolding->makeFieldAbsent(field);
        view().update(sleHolding);

        // A count of zero is stored as absent.
        if (limitCount <= 1)
        {
            sleFirewall->makeFieldAbsent(sfLimitCount);
        }
        else
        {
            sleFirewall->setFieldU32(sfLimitCount, limitCount - 1);
        }
        view().update(sleFirewall);
        return tesSUCCESS;
    }

    auto const now = view().parentCloseTime();

    if (sleHolding->isFieldPresent(field))
    {
        // What was spent in the current period is kept across a change.
        STObject& limit = sleHolding->peekFieldObject(field);
        auto const period = firewallLimitPeriod(limit, now);
        limit.setFieldAmount(sfSpent, period.spent);
        limit.setFieldU32(sfStartDate, period.startDate);
        limit.setFieldAmount(sfLimitAmount, ctx_.tx[sfLimitAmount]);
        limit.setFieldU32(sfPeriod, ctx_.tx[sfPeriod]);
        view().update(sleHolding);
        return tesSUCCESS;
    }

    STObject limit = STObject::makeInnerObject(field);
    limit.setFieldAmount(sfLimitAmount, ctx_.tx[sfLimitAmount]);
    limit.setFieldU32(sfPeriod, ctx_.tx[sfPeriod]);
    limit.setFieldAmount(sfSpent, STAmount{ctx_.tx[sfAsset]});
    limit.setFieldU32(sfStartDate, now.time_since_epoch().count());
    sleHolding->setFieldObject(field, limit);
    view().update(sleHolding);

    sleFirewall->setFieldU32(sfLimitCount, limitCount + 1);
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
