#include <xrpl/tx/transactors/sponsor/SponsorshipSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

static bool
hasSponsorshipBudget(
    SLE::const_ref sponsorshipSle,
    std::optional<STAmount> const& feeAmountDelta,
    std::optional<std::uint32_t> const& remainingOwnerCount)
{
    // A field the transaction omits keeps whatever the existing object holds,
    // so fall back to the current SLE value when the tx does not set it.
    STAmount const currentFee =
        sponsorshipSle ? (*sponsorshipSle)[~sfFeeAmount].value_or(STAmount{0}) : STAmount{0};
    STAmount const newFee = currentFee + feeAmountDelta.value_or(STAmount{0});
    bool const hasFeeAmount = newFee > beast::kZero;

    bool const hasRemainingOwnerCount = remainingOwnerCount
        ? *remainingOwnerCount > 0
        : sponsorshipSle && (*sponsorshipSle)[~sfRemainingOwnerCount].value_or(0) > 0;

    return hasFeeAmount || hasRemainingOwnerCount;
}

TxConsequences
SponsorshipSet::makeTxConsequences(PreflightContext const& ctx)
{
    auto const feeAmount = ctx.tx[~sfFeeAmountDelta];
    return TxConsequences{ctx.tx, feeAmount.has_value() ? feeAmount->xrp() : beast::kZero};
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
            ctx.tx.isFieldPresent(sfRemainingOwnerCount) || ctx.tx.isFieldPresent(sfMaxFee))
            return temMALFORMED;
    }
    else
    {
        // Both sponsor and sponsee can delete a Sponsorship object, but only
        // the sponsor can create or update one.
        if (account != sponsorID)
            return temMALFORMED;

        // FeeAmountDelta must be an XRP amount when present.
        if (auto const feeAmt = ctx.tx[~sfFeeAmountDelta]; feeAmt && !isXRP(*feeAmt))
            return temBAD_AMOUNT;

        // MaxFee must be a non-negative XRP amount when present.
        if (auto const maxFee = ctx.tx[~sfMaxFee];
            maxFee && (!isXRP(*maxFee) || *maxFee < beast::kZero))
            return temBAD_AMOUNT;

        if (!ctx.tx.isFieldPresent(sfRemainingOwnerCount) &&
            ctx.tx[~sfFeeAmountDelta].value_or(XRPAmount{0}) == beast::kZero &&
            !ctx.tx.isFieldPresent(sfMaxFee) && ((ctx.tx[sfFlags] & tfUniversalMask) == 0))
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

    auto const sponsorshipSle = ctx.view.read(keylet::sponsorship(sponsorID, sponseeID));

    // Deleting a Sponsorship object requires the object to already exist.
    if (ctx.tx.isFlag(tfDeleteObject) && !sponsorshipSle)
        return tecNO_ENTRY;

    if (!ctx.tx.isFlag(tfDeleteObject))
    {
        if (sponsorshipSle)
        {
            // Check for redundancy on updates
            if (!ctx.tx.isFieldPresent(sfRemainingOwnerCount) &&
                ctx.tx[~sfFeeAmountDelta].value_or(XRPAmount{0}) == beast::kZero &&
                !ctx.tx.isFieldPresent(sfMaxFee) && ((ctx.tx[sfFlags] & tfUniversalMask) == 0))
                return temREDUNDANT;
        }

        // Reject creating or updating a Sponsorship that would be left with no
        // budget (neither a positive FeeAmount nor a positive RemainingOwnerCount).
        // Such an object is unusable yet still consumes the sponsor's reserve.
        if (!hasSponsorshipBudget(
                sponsorshipSle, ctx.tx[~sfFeeAmountDelta], ctx.tx[~sfRemainingOwnerCount]))
            return tecNO_PERMISSION;

        if (!sponsorshipSle && ctx.tx[~sfFeeAmountDelta].value_or(XRPAmount{0}) < beast::kZero)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

static TER
deleteSponsorship(ApplyView& view, SLE::ref sle, beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorID = (*sle)[sfOwner];
    auto const sponseeID = (*sle)[sfSponsee];

    // The sponsor owns the Sponsorship object, so deletion releases the
    // sponsor's owner reserve.
    auto sponsorAccSle = view.peek(keylet::account(sponsorID));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(sponsorID), (*sle)[sfOwnerNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsor.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    if (!view.dirRemove(keylet::ownerDir(sponseeID), (*sle)[sfSponseeNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsee.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(view, sponsorAccSle, sle, 1, j);

    // Return any prefunded fee amount to the sponsor before erasing the object.
    if (sle->isFieldPresent(sfFeeAmount))
    {
        (*sponsorAccSle)[sfBalance] += sle->getFieldAmount(sfFeeAmount);
        view.update(sponsorAccSle);
    }

    view.erase(sle);

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

    auto const sponsorKeylet = keylet::sponsorship(sponsorID, sponseeID);
    auto const sponsorshipSle = ctx_.view().peek(sponsorKeylet);

    if (ctx_.tx.isFlag(tfDeleteObject))
    {
        if (!sponsorshipSle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return deleteSponsorship(ctx_.view(), sponsorshipSle, ctx_.journal);
    }

    auto const feeAmountDeltaOpt = ctx_.tx[~sfFeeAmountDelta];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const remainingOwnerCount = ctx_.tx[~sfRemainingOwnerCount];

    bool const hasPositiveFeeAmount =
        feeAmountDeltaOpt.has_value() && *feeAmountDeltaOpt > beast::kZero;

    auto reserveSponsorAccSle = getTxReserveSponsor(ctx_.getApplyViewContext());
    if (!reserveSponsorAccSle)
        return reserveSponsorAccSle.error();  // LCOV_EXCL_LINE

    if (!sponsorshipSle)
    {
        // Create a new Sponsorship object between the sponsor and sponsee.
        auto newSle = std::make_shared<SLE>(sponsorKeylet);

        (*newSle)[sfOwner] = sponsorID;
        (*newSle)[sfSponsee] = sponseeID;
        if (feeAmountDeltaOpt && (*feeAmountDeltaOpt).xrp() > (*sponsorAccSle)[sfBalance])
            return tecUNFUNDED;

        STAmount sponsorBalanceAfterFee = (*sponsorAccSle)[sfBalance];
        if (hasPositiveFeeAmount)
            sponsorBalanceAfterFee -= *feeAmountDeltaOpt;

        if (auto const ret = checkReserve(
                ctx_.getApplyViewContext(),
                sponsorAccSle,
                sponsorBalanceAfterFee.xrp(),
                *reserveSponsorAccSle,
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
            (*newSle)[sfFeeAmount] = *feeAmountDeltaOpt;
            (*sponsorAccSle)[sfBalance] -= *feeAmountDeltaOpt;
        }

        if (maxFee && *maxFee > beast::kZero)
            (*newSle)[sfMaxFee] = *maxFee;
        if (remainingOwnerCount && *remainingOwnerCount > 0)
            (*newSle)[sfRemainingOwnerCount] = *remainingOwnerCount;

        std::uint32_t flags = 0;
        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
            flags |= lsfSponsorshipRequireSignForFee;

        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
            flags |= lsfSponsorshipRequireSignForReserve;

        (*newSle)[sfFlags] = flags;

        auto const sponsorPage = view().dirInsert(
            keylet::ownerDir(sponsorID), sponsorKeylet, describeOwnerDir(sponsorID));
        if (!sponsorPage)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*newSle)[sfOwnerNode] = *sponsorPage;

        auto const sponseePage = view().dirInsert(
            keylet::ownerDir(sponseeID), sponsorKeylet, describeOwnerDir(sponseeID));
        if (!sponseePage)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*newSle)[sfSponseeNode] = *sponseePage;

        // NOLINTNEXTLINE(readability-suspicious-call-argument)
        increaseOwnerCount(view(), sponsorAccSle, *reserveSponsorAccSle, 1, ctx_.journal);
        addSponsorToLedgerEntry(newSle, *reserveSponsorAccSle);

        ctx_.view().insert(newSle);
        return tesSUCCESS;
    }

    // Update the existing Sponsorship object.
    if (feeAmountDeltaOpt)
    {
        auto feeAmountDelta = *feeAmountDeltaOpt;
        if (feeAmountDelta < beast::kZero &&
            -feeAmountDelta > (*sponsorshipSle)[~sfFeeAmount].valueOr(XRPAmount{0}))
            feeAmountDelta = -(*sponsorshipSle)[~sfFeeAmount].valueOr(XRPAmount{0});
        if (feeAmountDelta > beast::kZero && feeAmountDelta > (*sponsorAccSle)[sfBalance])
            return tecUNFUNDED;

        // Move the FeeAmount delta between the sponsor balance and Sponsorship
        // object.
        STAmount sponsorBalanceAfterFee = (*sponsorAccSle)[sfBalance];
        sponsorBalanceAfterFee -= feeAmountDelta;

        if (auto const ret = checkReserve(
                ctx_.getApplyViewContext(),
                sponsorAccSle,
                sponsorBalanceAfterFee.xrp(),
                *reserveSponsorAccSle,
                {},
                ctx_.journal,
                tecUNFUNDED);
            !isTesSuccess(ret))
        {
            return ret;
        }

        (*sponsorAccSle)[sfBalance] -= feeAmountDelta;
        STAmount const currentFee = (*sponsorshipSle)[~sfFeeAmount].valueOr(XRPAmount{0});
        STAmount const newFee = currentFee + feeAmountDelta;
        if (newFee == beast::kZero)
            sponsorshipSle->makeFieldAbsent(sfFeeAmount);
        else
            (*sponsorshipSle)[sfFeeAmount] = newFee;
        ctx_.view().update(sponsorAccSle);
    }

    if (maxFee)
    {
        if (*maxFee == beast::kZero)
        {
            (*sponsorshipSle).makeFieldAbsent(sfMaxFee);
        }
        else
        {
            (*sponsorshipSle)[sfMaxFee] = *maxFee;
        }
    }

    if (remainingOwnerCount)
    {
        if (*remainingOwnerCount == 0)
        {
            sponsorshipSle->makeFieldAbsent(sfRemainingOwnerCount);
        }
        else
        {
            sponsorshipSle->at(sfRemainingOwnerCount) = *remainingOwnerCount;
        }
    }

    // Apply requested flag changes.
    auto flags = sponsorshipSle->getFieldU32(sfFlags);
    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
        flags |= lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        flags &= ~lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
        flags |= lsfSponsorshipRequireSignForReserve;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
        flags &= ~lsfSponsorshipRequireSignForReserve;

    if (flags != (*sponsorshipSle)[sfFlags])
        (*sponsorshipSle)[sfFlags] = flags;

    view().update(sponsorshipSle);

    return tesSUCCESS;
}

void
SponsorshipSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
