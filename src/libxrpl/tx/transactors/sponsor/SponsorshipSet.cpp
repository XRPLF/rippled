#include <xrpl/tx/transactors/sponsor/SponsorshipSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SponsorshipEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace xrpl {

TxConsequences
SponsorshipSet::makeTxConsequences(PreflightContext const& ctx)
{
    auto const feeAmount = ctx.tx[~sfFeeAmountDelta];
    auto const feeAmountDelta = std::max(STAmount{0}, feeAmount.value_or(STAmount{0}));
    return TxConsequences{ctx.tx, feeAmountDelta.xrp()};
}

std::uint32_t
SponsorshipSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfSponsorshipSetMask;
}

NotTEC
SponsorshipSet::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForFee) &&
        ctx.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        return temINVALID_FLAG;
    if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForReserve) &&
        ctx.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
        return temINVALID_FLAG;

    auto const account = ctx.tx.getAccountID(sfAccount);
    bool const hasSponsor = ctx.tx.isFieldPresent(sfCounterpartySponsor);
    bool const hasSponsee = ctx.tx.isFieldPresent(sfSponsee);

    // The transaction must specify either Sponsor or Sponsee, but not both.
    if (hasSponsor == hasSponsee)
        return temMALFORMED;

    auto const sponsorID = ctx.tx[~sfCounterpartySponsor].value_or(account);
    auto const sponseeID = ctx.tx[~sfSponsee].value_or(account);

    if (sponsorID == sponseeID)
        return temMALFORMED;

    if (ctx.tx.isFlag(tfDeleteObject))
    {
        // Transactions deleting `Sponsorship` cannot set modification flags.
        constexpr std::uint32_t kModifyFlags = tfSponsorshipSetRequireSignForFee |
            tfSponsorshipSetRequireSignForReserve | tfSponsorshipClearRequireSignForFee |
            tfSponsorshipClearRequireSignForReserve;

        if ((ctx.tx.getFlags() & kModifyFlags) != 0u)
            return temINVALID_FLAG;

        // Transactions deleting `Sponsorship` cannot include modification fields.
        if (ctx.tx.isFieldPresent(sfFeeAmountDelta) ||
            ctx.tx.isFieldPresent(sfRemainingOwnerCountDelta) || ctx.tx.isFieldPresent(sfMaxFee))
            return temMALFORMED;
    }
    else
    {
        // Both sponsor and sponsee can delete a Sponsorship object, but only
        // the sponsor can create or update one.
        if (account != sponsorID)
            return temMALFORMED;

        // FeeAmountDelta must be a non-zero XRP amount when present.
        if (auto const feeAmt = ctx.tx[~sfFeeAmountDelta];
            feeAmt && (!isXRP(*feeAmt) || *feeAmt == beast::kZero))
            return temBAD_AMOUNT;

        // MaxFee must be a non-negative XRP amount when present.
        if (auto const maxFee = ctx.tx[~sfMaxFee];
            maxFee && (!isXRP(*maxFee) || *maxFee < beast::kZero))
            return temBAD_AMOUNT;

        // RemainingOwnerCountDelta must be a non-zero integer when present.
        if (auto const remainingOwnerCountDelta = ctx.tx[~sfRemainingOwnerCountDelta];
            remainingOwnerCountDelta && *remainingOwnerCountDelta == 0)
            return temINVALID;

        // nothing specified in the tx
        if (!ctx.tx.isFieldPresent(sfRemainingOwnerCountDelta) &&
            !ctx.tx.isFieldPresent(sfFeeAmountDelta) && !ctx.tx.isFieldPresent(sfMaxFee) &&
            ((ctx.tx.getFlags() & tfUniversalMask) == 0))
            return temREDUNDANT;
    }

    return tesSUCCESS;
}

TER
SponsorshipSet::preclaim(PreclaimContext const& ctx)
{
    auto const sponsorID = ctx.tx[~sfCounterpartySponsor].value_or(ctx.tx[sfAccount]);
    auto const sponseeID = ctx.tx[~sfSponsee].value_or(ctx.tx[sfAccount]);

    if (sponseeID == sponsorID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorAccSle = ctx.view.read(keylet::account(sponsorID));
    if (!sponsorAccSle)
        return tecNO_DST;

    auto const sponseeSle = ctx.view.read(keylet::account(sponseeID));
    if (!sponseeSle)
        return tecNO_DST;

    // Pseudo-accounts cannot participate in sponsorship.
    if (isPseudoAccount(sponsorAccSle) || isPseudoAccount(sponseeSle))
        return tecPSEUDO_ACCOUNT;

    SponsorshipEntryR const sponsorship(sponsorID, sponseeID, ctx.view, ctx.j);

    // Deleting a Sponsorship object requires the object to already exist.
    if (ctx.tx.isFlag(tfDeleteObject) && !sponsorship)
        return tecNO_ENTRY;

    if (!ctx.tx.isFlag(tfDeleteObject))
    {
        // Reject if applying the delta would overflow uint32_t. A negative delta
        // that underflows is clamped to zero (field absent) rather than erroring.
        if (sponsorship.totalRemainingOwnerCount(ctx.tx[~sfRemainingOwnerCountDelta]) >
            static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
            return tecLIMIT_EXCEEDED;

        // Reject creating or updating a Sponsorship that would be left with no
        // budget (neither a positive FeeAmount nor a positive RemainingOwnerCount).
        // Such an object is unusable yet still consumes the sponsor's reserve.
        if (!sponsorship.hasBudget(ctx.tx[~sfFeeAmountDelta], ctx.tx[~sfRemainingOwnerCountDelta]))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
SponsorshipSet::createSponsorship(
    SponsorshipEntryW& sponsorship,
    AccountID const& sponsorID,
    AccountID const& sponseeID,
    SLE::Ref sponsorAccSle,
    SLE::Ref reserveSponsorAccSle)
{
    auto const feeAmountDelta = ctx_.tx[~sfFeeAmountDelta];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const remainingOwnerCountDelta = ctx_.tx[~sfRemainingOwnerCountDelta];

    bool const hasPositiveFeeAmount = feeAmountDelta.has_value() && *feeAmountDelta > beast::kZero;

    // Create a new Sponsorship object between the sponsor and sponsee.
    sponsorship.newSLE();
    STAmount sponsorBalanceAfterFee = (*sponsorAccSle)[sfBalance];
    // sfFeeAmountDelta must be positive if the sponsorship object doesn't exist. This is
    // checked in preclaim.
    XRPL_ASSERT(
        !feeAmountDelta.has_value() || *feeAmountDelta > beast::kZero,
        "xrpl::SponsorshipSet::doApply : new sponsorship has positive fee amount");

    (*sponsorship)[sfOwner] = sponsorID;
    (*sponsorship)[sfSponsee] = sponseeID;
    if (feeAmountDelta && feeAmountDelta->xrp() > sponsorBalanceAfterFee.xrp())
        return tecUNFUNDED;

    if (hasPositiveFeeAmount)
        sponsorBalanceAfterFee -= *feeAmountDelta;

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(),
            sponsorAccSle,
            sponsorBalanceAfterFee.xrp(),
            reserveSponsorAccSle,
            {.ownerCountDelta = 1},
            ctx_.journal,
            tecUNFUNDED);
        !isTesSuccess(ret))
    {
        return ret;
    }

    if (hasPositiveFeeAmount)
    {
        // New object: FeeAmount starts absent, so deduct and record the full amount
        (*sponsorship)[sfFeeAmount] = *feeAmountDelta;
        (*sponsorAccSle)[sfBalance] -= *feeAmountDelta;
    }

    if (maxFee && *maxFee > beast::kZero)
        (*sponsorship)[sfMaxFee] = *maxFee;
    if (remainingOwnerCountDelta && *remainingOwnerCountDelta > 0)
        (*sponsorship)[sfRemainingOwnerCount] = *remainingOwnerCountDelta;

    std::uint32_t flags = 0;
    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
        flags |= lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
        flags |= lsfSponsorshipRequireSignForReserve;

    (*sponsorship)[sfFlags] = flags;

    auto const sponsorPage = view().dirInsert(
        keylet::ownerDir(sponsorID), sponsorship.keylet(), describeOwnerDir(sponsorID));
    if (!sponsorPage)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sponsorship)[sfOwnerNode] = *sponsorPage;

    auto const sponseePage = view().dirInsert(
        keylet::ownerDir(sponseeID), sponsorship.keylet(), describeOwnerDir(sponseeID));
    if (!sponseePage)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sponsorship)[sfSponseeNode] = *sponseePage;

    // NOLINTNEXTLINE(readability-suspicious-call-argument)
    increaseOwnerCount(view(), sponsorAccSle, reserveSponsorAccSle, 1, ctx_.journal);
    addSponsorToLedgerEntry(sponsorship.mutableRawSle(), reserveSponsorAccSle);

    sponsorship.insert();
    return tesSUCCESS;
}

TER
SponsorshipSet::doApply()
{
    auto const sponsorID = ctx_.tx[~sfCounterpartySponsor].value_or(accountID_);
    auto const sponseeID = ctx_.tx[~sfSponsee].value_or(accountID_);

    if (sponseeID == sponsorID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorAccSle = ctx_.view().peek(keylet::account(sponsorID));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!ctx_.view().exists(keylet::account(sponseeID)))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    SponsorshipEntryW sponsorship(sponsorID, sponseeID, ctx_.view(), ctx_.journal);

    if (ctx_.tx.isFlag(tfDeleteObject))
    {
        if (!sponsorship)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return sponsorship.removeFromLedger();
    }

    auto const feeAmountDelta = ctx_.tx[~sfFeeAmountDelta];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const remainingOwnerCountDelta = ctx_.tx[~sfRemainingOwnerCountDelta];

    auto reserveSponsorAccSle = getTxReserveSponsor(ctx_.getApplyViewContext());
    if (!reserveSponsorAccSle)
        return reserveSponsorAccSle.error();  // LCOV_EXCL_LINE

    if (!sponsorship)
    {
        return createSponsorship(
            sponsorship, sponsorID, sponseeID, sponsorAccSle, *reserveSponsorAccSle);
    }

    // Update the existing Sponsorship object.
    if (feeAmountDelta)
    {
        auto actualDelta = feeAmountDelta.value();
        auto const currentFee = (*sponsorship)[~sfFeeAmount].valueOr(XRPAmount{0});

        // Clamp negative delta to avoid underflow.
        if (actualDelta < beast::kZero && -actualDelta > currentFee)
            actualDelta = -currentFee;
        // Reject if the sponsor cannot afford the (positive) delta.
        if (actualDelta > beast::kZero && actualDelta > (*sponsorAccSle)[sfBalance])
            return tecUNFUNDED;

        // Move the FeeAmount delta between the sponsor balance and Sponsorship
        // object.
        (*sponsorAccSle)[sfBalance] -= actualDelta;

        if (auto const ret = checkReserve(
                ctx_.getApplyViewContext(),
                sponsorAccSle,
                (*sponsorAccSle)[sfBalance]->xrp(),
                *reserveSponsorAccSle,
                {},
                ctx_.journal,
                tecUNFUNDED);
            !isTesSuccess(ret))
        {
            return ret;
        }

        STAmount const newFee = currentFee + actualDelta;
        // checked in preclaim
        XRPL_ASSERT(
            newFee >= beast::kZero, "xrpl::SponsorshipSet::doApply : new fee is non-negative");
        if (newFee == beast::kZero)
        {
            sponsorship->makeFieldAbsent(sfFeeAmount);
        }
        else
        {
            (*sponsorship)[sfFeeAmount] = newFee;
        }
        ctx_.view().update(sponsorAccSle);
    }

    if (maxFee)
    {
        if (*maxFee == beast::kZero)
        {
            (*sponsorship).makeFieldAbsent(sfMaxFee);
        }
        else
        {
            (*sponsorship)[sfMaxFee] = *maxFee;
        }
    }

    if (remainingOwnerCountDelta)
    {
        std::int64_t const newCount =
            sponsorship.totalRemainingOwnerCount(remainingOwnerCountDelta);
        // Overflow is rejected in preclaim; underflow clamps to zero (field absent).
        XRPL_ASSERT(
            newCount <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()),
            "xrpl::SponsorshipSet::doApply : RemainingOwnerCount does not overflow");
        if (newCount <= 0)
        {
            sponsorship->makeFieldAbsent(sfRemainingOwnerCount);
        }
        else
        {
            sponsorship->at(sfRemainingOwnerCount) = static_cast<std::uint32_t>(newCount);
        }
    }

    // Apply requested flag changes.
    auto flags = sponsorship->getFieldU32(sfFlags);
    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
        flags |= lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        flags &= ~lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
        flags |= lsfSponsorshipRequireSignForReserve;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
        flags &= ~lsfSponsorshipRequireSignForReserve;

    if (flags != (*sponsorship)[sfFlags])
        (*sponsorship)[sfFlags] = flags;

    sponsorship.update();

    return tesSUCCESS;
}

void
SponsorshipSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
SponsorshipSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
