#include <xrpl/tx/transactors/sponsor/SponsorshipTransfer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/LeaseHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/account/AccountDelete.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace xrpl {

// Increment an uint32 sponsor count field and update the SLE.
static TER
incrementSponsorCount(
    ApplyView& view,
    SLE::Ref sle,
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
    SLE::Ref sle,
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
decrementPrefundedReserveCount(ApplyView& view, SLE::Ref sponsorshipSle, std::uint32_t const delta)
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

namespace {

// Whether `accountID` owns a Sponsorship (as its sfOwner) with prefunded XRP.
[[nodiscard]] bool
ownsFundedSponsorship(ReadView const& view, AccountID const& accountID)
{
    bool found = false;
    forEachItem(view, accountID, [&](SLE::const_pointer const& sle) {
        if (sle && sle->getType() == ltSPONSORSHIP && sle->at(sfOwner) == accountID &&
            sle->isFieldPresent(sfFeeAmount) && sle->getFieldAmount(sfFeeAmount) > beast::kZero)
            found = true;
    });
    return found;
}

}  // namespace

std::uint32_t
SponsorshipTransfer::getFlagsMask(PreflightContext const& ctx)
{
    // tfSponsorshipReap exists only with AccountLeasing.
    if (!ctx.rules.enabled(featureAccountLeasing))
        return tfSponsorshipTransferMask | tfSponsorshipReap;
    return tfSponsorshipTransferMask;
}

XRPAmount
SponsorshipTransfer::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Reaping deletes an account, so it costs one owner reserve like
    // AccountDelete to keep account churn uneconomical. Other transfer flags
    // use the normal base fee.
    if (tx.isFlag(tfSponsorshipReap))
        return calculateOwnerReserveFee(view, tx);
    return Transactor::calculateBaseFee(view, tx);
}

NotTEC
SponsorshipTransfer::preflight(PreflightContext const& ctx)
{
    static constexpr auto transferFlags =
        tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd | tfSponsorshipReap;
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

    // With AccountLeasing, the borrower of a paid-off lease loan takes over a
    // leased account by Reassign with sfSponsee and no sfSponsor: the signer
    // becomes the sponsor. preclaim checks that the account is leased.
    bool const isLeaseTakeover = ctx.rules.enabled(featureAccountLeasing) &&
        ctx.tx.isFlag(tfSponsorshipReassign) && !ctx.tx.isFieldPresent(sfSponsor) &&
        ctx.tx.isFieldPresent(sfSponsee) && !ctx.tx.isFieldPresent(sfObjectID);
    if (isLeaseTakeover && ctx.tx.getAccountID(sfSponsee) == ctx.tx.getAccountID(sfAccount))
    {
        JLOG(ctx.j.debug()) << "preflight: sfSponsee should not be the same as the account";
        return temMALFORMED;
    }

    if (ctx.tx.isFlag(tfSponsorshipReassign) && !isLeaseTakeover)
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

    if (ctx.tx.isFlag(tfSponsorshipReap))
    {
        // Reaping deletes an abandoned, under-reserved sponsored account. The
        // sponsee is the target and the reaping sponsor is the tx sender, so no
        // sfSponsor is supplied, the target must be a named account other than
        // the sender, and no object may be specified (account-level only).
        if (ctx.tx.isFieldPresent(sfSponsor))
        {
            JLOG(ctx.j.debug()) << "preflight: sfSponsor must not be present when reaping";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfObjectID))
        {
            JLOG(ctx.j.debug()) << "preflight: sfObjectID must not be present when reaping";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug()) << "preflight: sfSponsee must be present when reaping";
            return temMALFORMED;
        }

        if (ctx.tx.getAccountID(sfSponsee) == ctx.tx.getAccountID(sfAccount))
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

    // A proposed SponsorshipTransfer is stored unsigned; its SponsorSignature
    // is collected on-ledger afterward, so its absence here is expected, not
    // an error (On-Chain Cosigner spec §5.3.1.2).
    if (isAccountReserveSponsorship && !ctx.tx.isFieldPresent(sfSponsorSignature) &&
        (ctx.flags & TapProposal) == 0)
    {
        JLOG(ctx.j.debug()) << "preflight: account sponsorship requires sfSponsorSignature";
        return temMALFORMED;
    }

    if (auto const objectID = ctx.tx[~sfObjectID];
        ctx.rules.enabled(fixCleanup3_5_0) && objectID && *objectID == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "preflight: sfObjectID must not be zero";
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
    else if (ctx.tx.isFlag(tfSponsorshipReassign) && !ctx.tx.isFieldPresent(sfSponsor))
    {
        // A lease takeover (preflight): the target is a leased account and the
        // signer is the borrower of its paid-off lease loan.
        if (objectID.has_value() || !isSponsored)
            return tecNO_PERMISSION;
        auto const loan = leaseLoanOfSponsor(ctx.view, targetSle->getAccountID(*sponsorField));
        if (!loan || loan->at(sfBorrower) != account || !isLoanPaidOff(*loan))
            return tecNO_PERMISSION;
    }
    else if (ctx.tx.isFlag(tfSponsorshipReassign))
    {
        // Reassigning sponsorship: needs a new reserve sponsor, and the target must already
        // be sponsored
        if (!newSponsorSle || !isSponsored)
            return tecNO_PERMISSION;

        // A leased account leaves its lease only by the takeover above, End,
        // AccountDelete or Reap; another sponsor cannot take it.
        if (!objectID.has_value() &&
            leaseLoanOfSponsor(ctx.view, targetSle->getAccountID(*sponsorField)))
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

        // Only the sponsor or sponsee can end sponsorship. A leased account's
        // sponsor is a pseudo-account, so the account acting for it may end it.
        auto const sponsor = targetSle->getAccountID(*sponsorField);
        auto const acting =
            objectID.has_value() ? std::nullopt : leaseActingSponsor(ctx.view, sponsor);
        if (account != sponsor && account != sponseeID && account != acting)
            return tecNO_PERMISSION;
    }
    else if (ctx.tx.isFlag(tfSponsorshipReap))
    {
        // Reaping introduces no new sponsor and is account-level only.
        if (newSponsorSle || objectID.has_value())
            return tecNO_PERMISSION;

        // The target must be an account-level sponsored account.
        if (!isSponsored)
            return tecNO_PERMISSION;

        // Only the account's current sponsor may reap it; a leased account is
        // reaped by the account acting for its lease account.
        auto const sponsor = targetSle->getAccountID(*sponsorField);
        auto const reaper = leaseActingSponsor(ctx.view, sponsor).value_or(sponsor);
        if (account != reaper)
            return tecNO_SPONSOR_PERMISSION;

        // Reaping is limited to a sponsee that cannot stand on its own reserve.
        // A solvent sponsee is never reapable — the sponsor should release the
        // sponsorship with tfSponsorshipEnd instead. Keeping its own reserve is
        // the sponsee's defense against being reaped.
        XRPAmount const balance = sponseeSle->getFieldAmount(sfBalance).xrp();
        XRPAmount const ownReserve =
            accountReserve(ctx.view, sponseeSle, ctx.j, {.accountCountDelta = 1});
        if (balance >= ownReserve)
            return tecNO_PERMISSION;

        // The sponsee account must itself satisfy the deletion preconditions
        // (no obligations, outside the sequence-replay window).
        if (auto const ter = checkAccountDeletable(ctx.view, sponseeSle, reaper, ctx.j);
            !isTesSuccess(ter))
            return ter;

        // A Sponsorship the sponsee owns holds prefunded XRP (FeeAmount) that
        // deleting the account would pay to the reaper. That XRP is the
        // sponsee's, so such an account is not reapable. checkAccountDeletable
        // has bounded the owner directory this reads.
        if (ownsFundedSponsorship(ctx.view, sponseeID))
            return tecHAS_OBLIGATIONS;
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

    auto const balanceBeforeFee = [&](SLE::ConstRef sle) -> XRPAmount {
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

            // The owner reclaims the reserve burden when the object is no longer
            // sponsored, so it must be able to hold that reserve on its own once the
            // sponsorship is removed. This mirrors the account-level End check below,
            // keeping the behavior consistent across accounts and objects: a
            // sponsorship can only be ended if the sponsee self-funds, another sponsor
            // steps in (Reassign), or the object/account is deleted.
            if (view().rules().enabled(fixCleanup3_4_0))
            {
                if (auto const ter = checkReserve(
                        ctx_.getApplyViewContext(),
                        sponseeSle,
                        balanceBeforeFee(sponseeSle),
                        SLE::pointer(),
                        {.ownerCountDelta = ownerCountDelta},
                        ctx_.journal);
                    !isTesSuccess(ter))
                    return ter;
            }

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
            // A lease takeover (no sfSponsor, preflight) makes the signer the
            // sponsor of the leased account.
            bool const isLeaseTakeover = isReassign && !ctx_.tx.isFieldPresent(sfSponsor);
            auto const newSponsor =
                isLeaseTakeover ? std::optional<AccountID>{accountID_} : ctx_.tx[~sfSponsor];
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

            // The borrower takes the leased account with the XRP it frees: the
            // lease account's Free once it sponsors one account less (the base
            // reserve, more after a fall of the reserve, less after a rise). The
            // borrower's reserve check below then counts the account.
            if (isLeaseTakeover)
            {
                auto const leaseID = sponseeSle->getAccountID(sfSponsor);
                auto const leaseSle = view().read(keylet::account(leaseID));
                if (!leaseSle)
                    return tefINTERNAL;  // LCOV_EXCL_LINE
                XRPAmount const freed = leaseFree(view(), *leaseSle, -1);
                if (freed > beast::kZero)
                {
                    if (auto const ter = accountSend(
                            view(),
                            leaseID,
                            newSponsorID,
                            STAmount{freed},
                            j_,
                            {},
                            WaiveTransferFee::Yes);
                        !isTesSuccess(ter))
                        return ter;
                }
            }

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
        else if (ctx_.tx.isFlag(tfSponsorshipReap))
        {
            // Reap the abandoned sponsee: delete its account, returning its
            // residual XRP (and any prefunded sponsorship) to the sponsor. The
            // account deletion unwinds the sfSponsor reserve sponsorship, so
            // the sponsor's SponsoringAccountCount is released here.
            // The reaper is the sponsor, or the account acting for a lease
            // account (preclaim); it receives the residual XRP.
            auto reaperSle = view().peek(keylet::account(accountID_));
            if (!reaperSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            if (auto const ter = applyAccountDelete(ctx_, sponseeSle, reaperSle, ctx_.journal);
                !isTesSuccess(ter))
                return ter;
        }
    }

    return tesSUCCESS;
}

void
SponsorshipTransfer::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
