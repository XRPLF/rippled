#include <xrpl/tx/transactors/sponsor/SponsorshipTransfer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace xrpl {

static std::optional<std::uint32_t>
applyCountDelta(std::uint32_t current, std::int64_t delta)
{
    std::int64_t const next = static_cast<std::int64_t>(current) + delta;
    if (next < 0 || next > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    return static_cast<std::uint32_t>(next);
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

        // sfSponsorFlags should not be present if it is ending sponsorship
        if (ctx.tx.isFieldPresent(sfSponsorFlags))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsorFlags should not be present when ending sponsorship";
            return temINVALID_FLAG;
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
    auto const reserveSponsor = getTxReserveSponsorAccountID(ctx.tx);
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
    auto const index = ctx.tx[~sfObjectID];
    auto const newSponsorSleExpected = getTxReserveSponsor(ctx.view, ctx.tx);
    if (!newSponsorSleExpected)
        return newSponsorSleExpected.error();  // LCOV_EXCL_LINE
    auto const newSponsorSle = *newSponsorSleExpected;

    bool const isObjectSponsor = !!index;

    auto const account = ctx.tx[sfAccount];
    auto const sponseeID = ctx.tx[~sfSponsee].value_or(account);
    auto const sponseeSle = ctx.view.read(keylet::account(sponseeID));
    if (!sponseeSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (isObjectSponsor)
    {
        auto const sle = ctx.view.read(keylet::unchecked(*index));
        if (!sle)
            return tecNO_ENTRY;

        // v1 scope: an object is only sponsorable via SponsorshipTransfer if
        // its creating transaction type is itself permitted to set
        // spfSponsorReserve (the allow-list in preflight1Sponsor). Otherwise
        // an Oracle / Ticket / DID / etc. could be retroactively sponsored
        // even though its creating tx cannot be, leaving downstream
        // transactors with no path to maintain the sponsorship invariants.
        switch (sle->getType())
        {
            case ltDELEGATE:
            case ltDEPOSIT_PREAUTH:
            case ltMPTOKEN:
            case ltMPTOKEN_ISSUANCE:
            case ltCREDENTIAL:
            case ltRIPPLE_STATE:
            case ltSIGNER_LIST:
            case ltCHECK:
            case ltESCROW:
            case ltPAYCHAN:
                break;
            default:
                return tecNO_PERMISSION;
        }

        std::uint32_t const ownerCountDelta = 1;

        auto const owner = getLedgerEntryOwner(ctx.view, sle, sponseeID);
        if (!owner.has_value() || owner.value() != sponseeID)
            return tecNO_PERMISSION;

        auto const& sponsorField = getLedgerEntrySponsorField(sle, owner.value());

        if (ctx.tx.isFlag(tfSponsorshipCreate))
        {
            if (!newSponsorSle)
                return tecNO_PERMISSION;

            // check that the object is not sponsored yet
            if (sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }
        else if (ctx.tx.isFlag(tfSponsorshipReassign))
        {
            if (!newSponsorSle)
                return tecNO_PERMISSION;

            // check object is already ctx.sponsored
            if (!sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }
        else if (ctx.tx.isFlag(tfSponsorshipEnd))
        {
            if (newSponsorSle)
                return tecNO_PERMISSION;

            // check object is sponsored
            if (!sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;

            // only the sponsor or sponsee can end sponsorship
            auto const sponsor = sle->getAccountID(sponsorField);
            if (account != sponsor && account != sponseeID)
                return tecNO_PERMISSION;
        }

        // check new sponsor have sufficient balance
        // NOLINTNEXTLINE(readability-suspicious-call-argument)
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                ctx.tx,
                sponseeSle,
                sponseeSle->getFieldAmount(sfBalance),
                newSponsorSle,
                ownerCountDelta,
                0,
                ctx.j);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        if (ctx.tx.isFlag(tfSponsorshipCreate))
        {
            if (!newSponsorSle)
                return tecNO_PERMISSION;

            // check account is not sponsored yet
            if (sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }
        else if (ctx.tx.isFlag(tfSponsorshipReassign))
        {
            if (!newSponsorSle)
                return tecNO_PERMISSION;

            // check account is already sponsored
            if (!sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }
        else if (ctx.tx.isFlag(tfSponsorshipEnd))
        {
            if (newSponsorSle)
                return tecNO_PERMISSION;

            // check account is sponsored
            if (!sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;

            // only the sponsor or sponsee can end sponsorship
            auto const sponsor = sponseeSle->getAccountID(sfSponsor);
            if (account != sponsor && account != sponseeID)
                return tecNO_PERMISSION;
        }

        // check account have sufficient balance
        // In the case of removing an account sponsor, accSle should have no sfSponsor set
        // (AccountReserve = 0). However, by setting accountCountDelta = 1 here, we are able to
        // calculate the actual required Account Reserve.
        // NOLINTNEXTLINE(readability-suspicious-call-argument)
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                ctx.tx,
                sponseeSle,
                sponseeSle->getFieldAmount(sfBalance),
                newSponsorSle,
                0,
                1,
                ctx.j);
            !isTesSuccess(ter))
            return ter;
    }

    return tesSUCCESS;
}

static TER
reduceReserveCount(
    ApplyView& view,
    AccountID const& account,
    AccountID const& sponsor,
    int64_t delta)
{
    if (delta == 0)
        return tesSUCCESS;
    if (delta > 0)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorKeylet = keylet::sponsorship(sponsor, account);
    auto const sponsorSle = view.peek(sponsorKeylet);
    if (!sponsorSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const afterReserveCount =
        applyCountDelta(sponsorSle->getFieldU32(sfRemainingOwnerCount), delta);
    if (!afterReserveCount)
    {
        // already checked in preclaim()
        UNREACHABLE("xrpl::reduceReserveCount : invalid reserve count");
        return tefINTERNAL;  // LCOV_EXCL_LINE
    }

    sponsorSle->at(sfRemainingOwnerCount) = *afterReserveCount;
    view.update(sponsorSle);
    return tesSUCCESS;
}

TER
SponsorshipTransfer::doApply()
{
    auto const& tx = ctx_.tx;

    auto const index = tx[~sfObjectID];
    bool const isObjectSponsor = index != std::nullopt;

    auto const sponseeID = tx[~sfSponsee].value_or(accountID_);
    auto const sponseeSle = view().peek(keylet::account(sponseeID));
    if (!sponseeSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const setSponsorFieldU32 =
        [] [[nodiscard]] (auto const& sle, auto const& field, auto const& delta) -> TER {
        auto const newValue = applyCountDelta(sle->getFieldU32(field), delta);
        if (!newValue)
        {
            UNREACHABLE("xrpl::SponsorshipTransfer::doApply : Invalid sponsor field value");
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        sle->at(field) = *newValue;
        return tesSUCCESS;
    };

    if (isObjectSponsor)
    {
        auto const hasSignature = tx.isFieldPresent(sfSponsorSignature);

        // transfer object sponsor
        auto const objSle = view().peek(keylet::unchecked(*index));
        if (!objSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerID = getLedgerEntryOwner(view(), objSle, sponseeID);
        if (!ownerID)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(*ownerID));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        std::int64_t const ownerCountDelta = 1;

        auto const& sponsorField = getLedgerEntrySponsorField(objSle, *ownerID);

        if (ctx_.tx.isFlag(tfSponsorshipCreate))
        {
            auto const newSponsorID = tx.getAccountID(sfSponsor);
            XRPL_ASSERT(!!newSponsorID, "New sponsor is required when creating sponsorship");

            // update owner's sponsored count
            if (auto const ter =
                    setSponsorFieldU32(ownerSle, sfSponsoredOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(ownerSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter =
                    setSponsorFieldU32(newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(newSponsorSle);

            // set new sponsor to object
            objSle->setAccountID(sponsorField, newSponsorID);
            view().update(objSle);

            if (!hasSignature)
            {
                // use ReserveCount for pre-funded sponsoring
                if (auto const ter =
                        reduceReserveCount(view(), sponseeID, newSponsorID, -ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
            }
        }
        else if (ctx_.tx.isFlag(tfSponsorshipReassign))
        {
            auto const newSponsorID = tx.getAccountID(sfSponsor);
            XRPL_ASSERT(!!newSponsorID, "New sponsor is required when reassigning sponsorship");

            auto const oldSponsorID = objSle->getAccountID(sponsorField);
            XRPL_ASSERT(!!oldSponsorID, "Old sponsor is required when reassigning sponsorship");

            // decrement old sponsor's sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter =
                    setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(oldSponsorSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter =
                    setSponsorFieldU32(newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(newSponsorSle);

            // set new sponsor to object
            objSle->setAccountID(sponsorField, newSponsorID);
            view().update(objSle);

            if (!hasSignature)
            {
                // use ReserveCount for pre-funded sponsoring
                if (auto const ter =
                        reduceReserveCount(view(), sponseeID, newSponsorID, -ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
            }
        }
        else if (ctx_.tx.isFlag(tfSponsorshipEnd))
        {
            auto const oldSponsorID = objSle->getAccountID(sponsorField);
            XRPL_ASSERT(!!oldSponsorID, "Old sponsor is required when ending sponsorship");

            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // decrement sponsored count
            if (auto const ter =
                    setSponsorFieldU32(sponseeSle, sfSponsoredOwnerCount, -ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(sponseeSle);

            // decrement old sponsoring count
            if (auto const ter =
                    setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(oldSponsorSle);

            // remove sponsor from object
            objSle->makeFieldAbsent(sponsorField);
            view().update(objSle);
        }
    }
    else
    {
        if (ctx_.tx.isFlag(tfSponsorshipCreate))
        {
            // create account sponsor
            // increment new sponsoring count
            auto const newSponsorID = tx.getAccountID(sfSponsor);
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter = setSponsorFieldU32(newSponsorSle, sfSponsoringAccountCount, 1);
                !isTesSuccess(ter))
                return ter;
            view().update(newSponsorSle);

            // set new sponsor to account
            sponseeSle->setAccountID(sfSponsor, newSponsorID);
            view().update(sponseeSle);
        }
        else if (ctx_.tx.isFlag(tfSponsorshipReassign))
        {
            // reassign account sponsor
            // increment new sponsoring count
            auto const newSponsorID = tx.getAccountID(sfSponsor);
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter = setSponsorFieldU32(newSponsorSle, sfSponsoringAccountCount, 1);
                !isTesSuccess(ter))
                return ter;
            view().update(newSponsorSle);

            // decrement old sponsoring count
            auto const oldSponsorID = sponseeSle->getAccountID(sfSponsor);
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter = setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
                !isTesSuccess(ter))
                return ter;
            view().update(oldSponsorSle);

            // set new sponsor to account
            sponseeSle->setAccountID(sfSponsor, newSponsorID);
            view().update(sponseeSle);
        }
        else if (ctx_.tx.isFlag(tfSponsorshipEnd))
        {
            // dissolve account sponsor
            auto const oldSponsorID = sponseeSle->getAccountID(sfSponsor);
            sponseeSle->makeFieldAbsent(sfSponsor);
            view().update(sponseeSle);

            // decrement account sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter = setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
                !isTesSuccess(ter))
                return ter;
            view().update(oldSponsorSle);
        }
    }

    return tesSUCCESS;
}

void
SponsorshipTransfer::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
