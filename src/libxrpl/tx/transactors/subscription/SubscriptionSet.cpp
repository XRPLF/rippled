#include <xrpl/tx/transactors/subscription/SubscriptionSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SubscriptionHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <variant>

namespace xrpl {

template <ValidIssueType T>
static NotTEC
setPreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
setPreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::kZero)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.get<Issue>().currency)
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

template <>
NotTEC
setPreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.native() || amount.mpt() > MPTAmount{kMaxMpTokenAmount} || amount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

std::uint32_t
SubscriptionSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfSubscriptionSetMask;
}

NotTEC
SubscriptionSet::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        if (!ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Malformed transaction: SubscriptionID "
                                   "is present, but Amount is not.";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfDestination) || ctx.tx.isFieldPresent(sfStartTime))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Malformed transaction: SubscriptionID "
                                   "is  present, but immutable fields are also present.";
            return temMALFORMED;
        }

        // lsfSingleUse is fixed at creation and cannot be changed on update.
        if (ctx.tx.getFlags() & tfSingleUse)
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: tfSingleUse cannot be set on update.";
            return temINVALID_FLAG;
        }
    }
    else
    {
        // create
        if (!ctx.tx.isFieldPresent(sfDestination) || !ctx.tx.isFieldPresent(sfAmount) ||
            !ctx.tx.isFieldPresent(sfFrequency))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Malformed transaction: SubscriptionID "
                                   "is not present, and required fields are not present.";
            return temMALFORMED;
        }

        if (ctx.tx.getAccountID(sfDestination) == ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Malformed transaction: Account "
                                   "is the same as the destination.";
            return temDST_IS_SRC;
        }
    }

    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    if (amount.native())
    {
        if (!isLegalNet(amount) || amount <= beast::kZero)
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Malformed transaction: bad amount: "
                                << amount.getFullText();
            return temBAD_AMOUNT;
        }
    }
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) { return setPreflightHelper<T>(ctx); },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    return tesSUCCESS;
}

TER
SubscriptionSet::preclaim(PreclaimContext const& ctx)
{
    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID dest = ctx.tx.getAccountID(sfDestination);
    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        auto sle = ctx.view.read(keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
        if (!sle)
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Subscription does not exist.";
            return tecNO_ENTRY;
        }

        if (sle->getAccountID(sfAccount) != ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Account is not the "
                                   "owner of the subscription.";
            return tecNO_PERMISSION;
        }

        if (amount.asset() != sle->getFieldAmount(sfAmount).asset())
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Amount asset does not "
                                   "match the subscription asset.";
            return tecWRONG_ASSET;
        }

        dest = sle->getAccountID(sfDestination);
    }
    else
    {
        // create
        auto const sleDest = ctx.view.read(keylet::account(ctx.tx.getAccountID(sfDestination)));
        if (!sleDest)
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Destination account does not exist.";
            return tecNO_DST;
        }

        auto const flags = sleDest->getFlags();
        if ((flags & lsfRequireDestTag) && !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Frequency == 0 denotes an unmetered subscription: no period
        // accounting, each claim capped at Amount.
    }

    if (!isXRP(amount))
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return canTransferTokenHelper<T>(ctx.view, account, dest, amount, ctx.j);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

TER
SubscriptionSet::doApply()
{
    Sandbox sb(&ctx_.view());

    AccountID const account = ctx_.tx.getAccountID(sfAccount);
    auto const sleAccount = sb.peek(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(ctx_.journal.trace()) << "SubscriptionSet: Account does not exist.";
        return tecINTERNAL;
    }

    if (ctx_.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        auto const currentTime = sb.header().parentCloseTime.time_since_epoch().count();
        auto sle = sb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
        sle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));

        // Changing Frequency starts a clean period: reset the anchor to now and
        // restore the full balance. This covers metered<->unmetered and
        // metered->metered transitions uniformly.
        if (ctx_.tx.isFieldPresent(sfFrequency))
        {
            sle->setFieldU32(sfFrequency, ctx_.tx.getFieldU32(sfFrequency));
            sle->setFieldU32(sfNextClaimTime, currentTime);
            sle->setFieldAmount(sfBalance, ctx_.tx.getFieldAmount(sfAmount));
        }

        if (ctx_.tx.isFieldPresent(sfExpiration))
        {
            auto const expiration = ctx_.tx.getFieldU32(sfExpiration);

            // Expiration == 0 removes any existing expiration.
            if (expiration == 0)
            {
                if (sle->isFieldPresent(sfExpiration))
                    sle->makeFieldAbsent(sfExpiration);
            }
            else if (expiration < currentTime)
            {
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The expiration time is in the past.";
                return tecEXPIRED;
            }
            else
            {
                sle->setFieldU32(sfExpiration, expiration);
            }
        }

        sb.update(sle);
    }
    else
    {
        auto const currentTime = sb.header().parentCloseTime.time_since_epoch().count();
        auto startTime = currentTime;
        auto nextClaimTime = currentTime;

        // create
        {
            auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
            auto const reserve =
                accountReserve(sb, sleAccount, ctx_.journal, {.ownerCountDelta = 1});
            if (balance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        AccountID const dest = ctx_.tx.getAccountID(sfDestination);
        Keylet const subKeylet = keylet::subscription(account, dest, ctx_.tx.getSeqProxy().value());
        auto sle = std::make_shared<SLE>(subKeylet);
        sle->setAccountID(sfAccount, account);
        sle->setAccountID(sfDestination, dest);
        sle->setFieldU32(sfSequence, ctx_.tx.getSeqProxy().value());
        if (ctx_.tx.getFlags() & tfSingleUse)
            sle->setFlag(lsfSingleUse);
        if (ctx_.tx.isFieldPresent(sfDestinationTag))
            sle->setFieldU32(sfDestinationTag, ctx_.tx.getFieldU32(sfDestinationTag));
        sle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));
        sle->setFieldAmount(sfBalance, ctx_.tx.getFieldAmount(sfAmount));
        sle->setFieldU32(sfFrequency, ctx_.tx.getFieldU32(sfFrequency));
        if (ctx_.tx.isFieldPresent(sfStartTime))
        {
            startTime = ctx_.tx.getFieldU32(sfStartTime);
            nextClaimTime = startTime;
            if (startTime < currentTime)
            {
                JLOG(ctx_.journal.trace()) << "SubscriptionSet: The start time is in the past.";
                return tecNO_PERMISSION;
            }
        }

        sle->setFieldU32(sfNextClaimTime, nextClaimTime);
        if (ctx_.tx.isFieldPresent(sfExpiration))
        {
            auto const expiration = ctx_.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The expiration time is in the past.";
                return tecEXPIRED;
            }

            if (expiration < nextClaimTime)
            {
                JLOG(ctx_.journal.trace()) << "SubscriptionSet: The expiration time is "
                                              "less than the next claim time.";
                return tecEXPIRED;
            }
            sle->setFieldU32(sfExpiration, expiration);
        }

        {
            auto page =
                sb.dirInsert(keylet::ownerDir(account), subKeylet, describeOwnerDir(account));
            if (!page)
                return tecDIR_FULL;
            (*sle)[sfOwnerNode] = *page;
        }

        {
            auto page = sb.dirInsert(keylet::ownerDir(dest), subKeylet, describeOwnerDir(dest));
            if (!page)
                return tecDIR_FULL;
            (*sle)[sfDestinationNode] = *page;
        }

        increaseOwnerCount(sb, sleAccount, SLE::pointer(), 1, ctx_.journal);
        sb.insert(sle);
    }
    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
SubscriptionSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
SubscriptionSet::finalizeInvariants(
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
