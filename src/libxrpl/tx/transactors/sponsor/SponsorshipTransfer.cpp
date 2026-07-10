#include <xrpl/tx/transactors/sponsor/SponsorshipTransfer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl {

// Increment an uint32 sponsor count field and update the SLE.
static TER
incrementSponsorCount(
    ApplyView& view,
    SLE::ref sle,
    SF_UINT32 const& field,
    std::uint32_t const delta)
{
    auto const currentValue = sle->getFieldU32(field);
    if (std::numeric_limits<std::uint32_t>::max() - currentValue < delta)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::incrementSponsorCount : sponsor field overflow");
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    sle->at(field) = currentValue + delta;
    view.update(sle);
    return tesSUCCESS;
}

// Decrement an uint32 sponsor count field and update the SLE.
static TER
decrementSponsorCount(
    ApplyView& view,
    SLE::ref sle,
    SF_UINT32 const& field,
    std::uint32_t const delta)
{
    auto const currentValue = sle->getFieldU32(field);
    if (currentValue < delta)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::decrementSponsorCount : sponsor field underflow");
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    sle->at(field) = currentValue - delta;
    view.update(sle);
    return tesSUCCESS;
}

// Consume the sponsor's pre-funded reserve budget and lowers the Sponsorship
// object's RemainingOwnerCount.
static TER
decrementPrefundedReserveCount(ApplyView& view, SLE::ref sponsorshipSle, std::uint32_t const delta)
{
    if (delta == 0)
        return tesSUCCESS;  // LCOV_EXCL_LINE

    auto const currentReserveCount = sponsorshipSle->getFieldU32(sfRemainingOwnerCount);
    if (currentReserveCount < delta)
    {
        // LCOV_EXCL_START
        // Already verified by checkReserve (sufficient RemainingOwnerCount)
        UNREACHABLE("xrpl::decrementPrefundedReserveCount : invalid reserve count");
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    sponsorshipSle->at(sfRemainingOwnerCount) = currentReserveCount - delta;
    view.update(sponsorshipSle);
    return tesSUCCESS;
}

std::uint32_t
SponsorshipTransfer::getFlagsMask(PreflightContext const& ctx)
{
    return tfSponsorshipTransferMask;
}

NotTEC
SponsorshipTransfer::preflight(PreflightContext const& ctx)
{
    static constexpr auto transferFlags =
        tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd;
    if (std::popcount(ctx.tx.getFlags() & transferFlags) != 1)
    {
        JLOG(ctx.j.debug()) << "preflight: Only one SponsorshipTransfer flag can be set per tx.";
        return temINVALID_FLAG;
    }

    if (ctx.tx.isFlag(tfSponsorshipCreate))
    {
        // Creating sponsorship transfers an unsponsored target from the sponsee
        // to a reserve sponsor identified by sfSponsor + spfSponsorReserve.
        if (!ctx.tx.isFieldPresent(sfSponsor))
        {
            JLOG(ctx.j.debug()) << "preflight: sfSponsor must be present when creating sponsorship";
            return temMALFORMED;
        }

        if (!isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve must be set when creating sponsorship";
            return temINVALID_FLAG;
        }

        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsee must not be present when creating sponsorship";
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFlag(tfSponsorshipReassign))
    {
        // Reassigning sponsorship transfers an already sponsored target from its
        // current reserve sponsor to the new sponsor identified by sfSponsor +
        // spfSponsorReserve.
        if (!ctx.tx.isFieldPresent(sfSponsor))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsor must be present when reassigning sponsorship";
            return temMALFORMED;
        }

        if (!isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve must be set when reassigning sponsorship";
            return temINVALID_FLAG;
        }
        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsee must not be present when reassigning sponsorship";
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFlag(tfSponsorshipEnd))
    {
        // Ending sponsorship removes reserve sponsorship from a sponsored target.
        // The target is sfSponsee when provided; otherwise it is sfAccount.
        if (ctx.tx.isFieldPresent(sfSponsor))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsor must not be present when ending sponsorship";
            return temMALFORMED;
        }

        // sfSponsorFlags should not be present if it is ending sponsorship.
        if (ctx.tx.isFieldPresent(sfSponsorFlags))
        {
            // Unreachable: reaching here means sfSponsor is absent, which is already checked above,
            // and preflight1Sponsor already rejects sfSponsorFlags present without sfSponsor with
            // temINVALID_FLAG. Keep this as a defensive check.
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::SponsorshipTransfer::preflight : sfSponsorFlags present without sfSponsor "
                "when ending sponsorship");
            return temINVALID_FLAG;
            // LCOV_EXCL_STOP
        }

        if (ctx.tx.isFieldPresent(sfSponsee) &&
            ctx.tx.getAccountID(sfSponsee) == ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.debug()) << "preflight: sfSponsee should not be the same as the account";
            return temMALFORMED;
        }
    }

    // Account-level reserve sponsorship changes the reserve responsibility for
    // the account itself, so the new sponsor must explicitly co-sign. Object-level
    // sponsorship may use pre-funded reserve sponsorship instead.
    bool const isCreateOrReassign =
        ctx.tx.isFlag(tfSponsorshipCreate) || ctx.tx.isFlag(tfSponsorshipReassign);
    auto const reserveSponsor = getTxReserveSponsorID(ctx.tx);
    bool const isAccountReserveSponsorship =
        isCreateOrReassign && reserveSponsor && !ctx.tx.isFieldPresent(sfObjectID);

    if (isAccountReserveSponsorship && !ctx.tx.isFieldPresent(sfSponsorSignature))
    {
        JLOG(ctx.j.debug()) << "preflight: account sponsorship requires sfSponsorSignature";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
SponsorshipTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const objectID = ctx.tx[~sfObjectID];
    auto const newSponsorSleExpected = getTxReserveSponsor(ctx.view, ctx.tx);
    if (!newSponsorSleExpected)
        return newSponsorSleExpected.error();  // LCOV_EXCL_LINE
    auto const newSponsorSle = *newSponsorSleExpected;

    auto const account = ctx.tx[sfAccount];
    auto const sponseeID = ctx.tx[~sfSponsee].value_or(account);
    auto const sponseeSle = ctx.view.read(keylet::account(sponseeID));
    if (!sponseeSle)
    {
        // If it is ending sponsorship, sfSponsee is user input, return terNO_ACCOUNT if it does not
        // exist.
        if (ctx.tx.isFieldPresent(sfSponsee))
            return terNO_ACCOUNT;

        // If it is creating or reassigning sponsorship, sfSponsee is the account itself, which is
        // always present by the time preclaim runs. Return tecINTERNAL if it does not exist.
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Default setup with an account sponsorship transfer. If it is an object transfer, they will be
    // overridden to the object SLE and its type-specific sponsor field:
    // sfHighSponsor/sfLowSponsor for a RippleState, sfSponsor for other object types.
    SLE::const_pointer targetSle = sponseeSle;
    auto const* sponsorField = &sfSponsor;

    if (objectID.has_value())
    {
        auto const objectSle = ctx.view.read(keylet::unchecked(*objectID));
        if (!objectSle)
            return tecNO_ENTRY;

        if (!isLedgerEntrySupportedBySponsorship(*objectSle))
            return tecNO_PERMISSION;

        if (!isLedgerEntryOwner(ctx.view, *objectSle, sponseeID))
            return tecNO_PERMISSION;

        // Object transfer: the target is the object, and its sponsor field
        // depends on the object type, a RippleState stores the sponsor in
        // sfHighSponsor/sfLowSponsor, while other object type uses sfSponsor.
        targetSle = objectSle;
        sponsorField = &getLedgerEntrySponsorField(*objectSle, sponseeID);
    }

    bool const isSponsored = targetSle->isFieldPresent(*sponsorField);

    if (ctx.tx.isFlag(tfSponsorshipCreate))
    {
        // Creating a new sponsorship: needs a new reserve sponsor, and the
        // target must not already be sponsored
        if (!newSponsorSle || isSponsored)
            return tecNO_PERMISSION;
    }
    else if (ctx.tx.isFlag(tfSponsorshipReassign))
    {
        // Reassigning sponsorship: needs a new reserve sponsor, and the target must already
        // be sponsored
        if (!newSponsorSle || !isSponsored)
            return tecNO_PERMISSION;

        // Reassigning to the current sponsor would change no state, but would
        // still draw down the sponsor's pre-funded reserve budget (and its
        // reserve headroom would be double-counted in checkReserve).
        if (targetSle->getAccountID(*sponsorField) == ctx.tx.getAccountID(sfSponsor))
            return tecNO_PERMISSION;
    }
    else if (ctx.tx.isFlag(tfSponsorshipEnd))
    {
        // Ending sponsorship: no new reserve sponsor, the target must be sponsored.
        if (newSponsorSle || !isSponsored)
            return tecNO_PERMISSION;

        // Only the sponsor or sponsee can end sponsorship.
        auto const sponsor = targetSle->getAccountID(*sponsorField);
        if (account != sponsor && account != sponseeID)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
SponsorshipTransfer::doApply()
{
    auto const objectID = ctx_.tx[~sfObjectID];

    auto const sponseeID = ctx_.tx[~sfSponsee].value_or(accountID_);
    auto const sponseeSle = view().peek(keylet::account(sponseeID));
    if (!sponseeSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const balanceBeforeFee = [&](SLE::const_ref sle) -> XRPAmount {
        if (sle->getAccountID(sfAccount) == accountID_)
            return preFeeBalance_;
        return sle->getFieldAmount(sfBalance).xrp();
    };

    bool const isCreate = ctx_.tx.isFlag(tfSponsorshipCreate);
    bool const isReassign = ctx_.tx.isFlag(tfSponsorshipReassign);

    if (objectID.has_value())
    {
        // Transfer object sponsor
        auto const objectSle = view().peek(keylet::unchecked(*objectID));
        if (!objectSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        // preclaim established that the sponsee owns the object.
        if (!isLedgerEntryOwner(view(), *objectSle, sponseeID))
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(sponseeID));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerCountDelta =
            static_cast<std::int32_t>(getLedgerEntryOwnerCount(*objectSle));
        auto const& sponsorField = getLedgerEntrySponsorField(*objectSle, sponseeID);

        if (isCreate || isReassign)
        {
            auto const newSponsor = ctx_.tx[~sfSponsor];
            XRPL_ASSERT(
                newSponsor.has_value(),
                "xrpl::SponsorshipTransfer::doApply : sfSponsor present for object sponsor "
                "create/reassign");
            if (!newSponsor)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            auto const newSponsorID = *newSponsor;
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // Check if new sponsor has sufficient balance
            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            if (auto const ter = checkReserve(
                    ctx_.getApplyViewContext(),
                    sponseeSle,
                    sponseeSle->getFieldAmount(sfBalance).xrp(),
                    newSponsorSle,
                    {.ownerCountDelta = ownerCountDelta},
                    ctx_.journal);
                !isTesSuccess(ter))
                return ter;

            if (isCreate)
            {
                // Update owner's sponsored count
                if (auto const ter = incrementSponsorCount(
                        view(), ownerSle, sfSponsoredOwnerCount, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;  // LCOV_EXCL_LINE
            }
            else if (isReassign)
            {
                auto const oldSponsorID = objectSle->getAccountID(sponsorField);
                if (!oldSponsorID)
                    return tefINTERNAL;  // LCOV_EXCL_LINE
                auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
                if (!oldSponsorSle)
                    return tefINTERNAL;  // LCOV_EXCL_LINE

                // Decrement old sponsor's sponsoring count
                if (auto const ter = decrementSponsorCount(
                        view(), oldSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;  // LCOV_EXCL_LINE
            }

            // Increment new sponsor's sponsoring count
            if (auto const ter = incrementSponsorCount(
                    view(), newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE

            // Object is now sponsored by new sponsor
            objectSle->setAccountID(sponsorField, newSponsorID);
            view().update(objectSle);

            auto const sponsorshipSle = view().peek(keylet::sponsorship(newSponsorID, sponseeID));
            if (sponsorshipSle)
            {
                // Update ReserveCount for sponsorship object if it exists
                if (auto const ter =
                        decrementPrefundedReserveCount(view(), sponsorshipSle, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;  // LCOV_EXCL_LINE
            }
        }
        else if (ctx_.tx.isFlag(tfSponsorshipEnd))
        {
            // End object sponsor
            auto const oldSponsorID = objectSle->getAccountID(sponsorField);
            if (!oldSponsorID)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // The owner reclaims the reserve burden when the object is no longer sponsored.
            // We do not check the sponsee's reserve here (via `checkReserve`) so that a sponsor can
            // always end a sponsorship, even if the sponsee lacks sufficient reserve.

            // Decrement sponsored count
            if (auto const ter = decrementSponsorCount(
                    view(), sponseeSle, sfSponsoredOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE

            // Decrement old sponsoring count
            if (auto const ter = decrementSponsorCount(
                    view(), oldSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE

            // Remove sponsor from object
            objectSle->makeFieldAbsent(sponsorField);
            view().update(objectSle);
        }
    }
    else
    {
        // Account-level sponsorship is always co-signed (preflight requires
        // sfSponsorSignature), so there is no pre-funded budget to draw down here.
        if (isCreate || isReassign)
        {
            auto const newSponsor = ctx_.tx[~sfSponsor];
            XRPL_ASSERT(
                newSponsor.has_value(),
                "xrpl::SponsorshipTransfer::doApply : sfSponsor present for account sponsor "
                "create/reassign");
            if (!newSponsor)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            auto const newSponsorID = *newSponsor;
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            if (auto const ter = checkReserve(
                    ctx_.getApplyViewContext(),
                    sponseeSle,
                    sponseeSle->getFieldAmount(sfBalance).xrp(),
                    newSponsorSle,
                    {.accountCountDelta = 1},
                    ctx_.journal);
                !isTesSuccess(ter))
                return ter;

            if (isReassign)
            {
                auto const oldSponsorID = sponseeSle->getAccountID(sfSponsor);
                if (!oldSponsorID)
                    return tefINTERNAL;  // LCOV_EXCL_LINE
                auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
                if (!oldSponsorSle)
                    return tefINTERNAL;  // LCOV_EXCL_LINE

                // Decrement old sponsoring count
                if (auto const ter =
                        decrementSponsorCount(view(), oldSponsorSle, sfSponsoringAccountCount, 1);
                    !isTesSuccess(ter))
                    return ter;  // LCOV_EXCL_LINE
            }

            // Increment new sponsoring count
            if (auto const ter =
                    incrementSponsorCount(view(), newSponsorSle, sfSponsoringAccountCount, 1);
                !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE

            // Account is now sponsored by new sponsor
            sponseeSle->setAccountID(sfSponsor, newSponsorID);
            view().update(sponseeSle);
        }
        else if (ctx_.tx.isFlag(tfSponsorshipEnd))
        {
            // End account sponsor
            auto const oldSponsorID = sponseeSle->getAccountID(sfSponsor);
            if (!oldSponsorID)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // The sponsee must be able to hold its own account reserve after
            // the sponsorship is removed.
            if (auto const ter = checkReserve(
                    ctx_.getApplyViewContext(),
                    sponseeSle,
                    balanceBeforeFee(sponseeSle),
                    SLE::pointer(),
                    {.accountCountDelta = 1},
                    ctx_.journal);
                !isTesSuccess(ter))
                return ter;

            sponseeSle->makeFieldAbsent(sfSponsor);
            view().update(sponseeSle);

            // Decrement account sponsoring count
            if (auto const ter =
                    decrementSponsorCount(view(), oldSponsorSle, sfSponsoringAccountCount, 1);
                !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE
        }
    }

    return tesSUCCESS;
}

void
SponsorshipTransfer::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
SponsorshipTransfer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
