#include <xrpl/tx/transactors/sponsor/SponsorshipTransfer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
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
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/oracle/OracleSet.h>

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

static std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account)
{
    switch (sle.getType())
    {
        case ltNFTOKEN_OFFER:
        case ltORACLE:
        case ltPERMISSIONED_DOMAIN:
        case ltVAULT:
        case ltLOAN_BROKER:
            return sle.getAccountID(sfOwner);
        case ltCHECK:
        case ltDID:
        case ltTICKET:
        case ltOFFER:
        case ltXCHAIN_OWNED_CLAIM_ID:
        case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltBRIDGE:
        case ltDEPOSIT_PREAUTH:
            return sle.getAccountID(sfAccount);
        case ltMPTOKEN_ISSUANCE:
            return sle.getAccountID(sfIssuer);
        case ltLOAN:
            return sle.getAccountID(sfBorrower);
        case ltSIGNER_LIST: {
            if (sle.isFieldPresent(sfOwner))
                return sle.getAccountID(sfOwner);  // added by fixIncludeKeyletFields

            auto const signerList = view.read(keylet::signers(account));
            if (!signerList)
                return std::nullopt;
            if (signerList->key() == sle.key())
                return account;
            return std::nullopt;
        }
        case ltCREDENTIAL: {
            if (sle.isFlag(lsfAccepted))
                return sle.getAccountID(sfSubject);
            return sle.getAccountID(sfIssuer);
        }
        case ltNFTOKEN_PAGE: {
            // the upper 20 bytes of the index of ltNFTokenPage are the Owner's
            // AccountID
            uint256 const& key = sle.key();
            return AccountID::fromVoid(key.data());
        }
        case ltRIPPLE_STATE: {
            if (sle.isFlag(lsfHighReserve))
            {
                auto const highAccount = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == account)
                    return highAccount;
            }
            if (sle.isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == account)
                    return lowAccount;
            }
            return std::nullopt;
        }
        case ltACCOUNT_ROOT: {
            // AccountRoot is not supported for object sponsorship
            return std::nullopt;
        }
        case ltNEGATIVE_UNL:
        case ltDIR_NODE:
        case ltAMENDMENTS:
        case ltLEDGER_HASHES:
        case ltFEE_SETTINGS:
        case ltAMM:
            return std::nullopt;
        default:
            return std::nullopt;
    };
}

static SF_ACCOUNT const&
getLedgerEntrySponsorField(SLE const& sle, AccountID const& owner)
{
    switch (sle.getType())
    {
        case ltRIPPLE_STATE: {
            if (sle.isFlag(lsfHighReserve))
            {
                auto const highAccount = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == owner)
                    return sfHighSponsor;
            }

            XRPL_ASSERT(sle.isFlag(lsfLowReserve), "getLedgerEntrySponsorField lsfLowReserve flag");
            [[maybe_unused]] auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
            XRPL_ASSERT(lowAccount == owner, "getLedgerEntrySponsorField lowAccount == owner");
            return sfLowSponsor;
        }
        default:
            return sfSponsor;
    }
}

static inline std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle)
{
    switch (sle.getType())
    {
        case ltORACLE: {
            return OracleSet::calculateOracleReserve(sle.getFieldArray(sfPriceDataSeries).size());
        }
        // Vaults require 2 owner counts (the vault and a pseudo-account)
        case ltVAULT:
            return 2;
        default:
            return 1;
    }
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

    auto const sponsorshipKeylet = keylet::sponsorship(sponsor, account);
    auto const sponsorshipSle = view.peek(sponsorshipKeylet);
    if (!sponsorshipSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const afterReserveCount =
        applyCountDelta(sponsorshipSle->getFieldU32(sfRemainingOwnerCount), delta);
    if (!afterReserveCount)
    {
        // already checked in preclaim()
        UNREACHABLE("xrpl::reduceReserveCount : invalid reserve count");  // LCOV_EXCL_LINE
        return tefINTERNAL;                                               // LCOV_EXCL_LINE
    }

    sponsorshipSle->at(sfRemainingOwnerCount) = *afterReserveCount;
    view.update(sponsorshipSle);
    return tesSUCCESS;
}

TER
setSponsorFieldU32(SLE& sle, SF_UINT32 const& field, std::int64_t delta)
{
    auto const newValue = applyCountDelta(sle[field], delta);
    if (!newValue)
    {
        UNREACHABLE(
            "xrpl::SponsorshipTransfer::doApply : Invalid sponsor field value");  // LCOV_EXCL_LINE
        return tecINTERNAL;                                                       // LCOV_EXCL_LINE
    }

    sle[field] = *newValue;
    return tesSUCCESS;
}

std::uint32_t
SponsorshipTransfer::getFlagsMask(PreflightContext const&)
{
    return tfSponsorshipTransferMask;
}

NotTEC
SponsorshipTransfer::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    auto const flagsSet = flags & ~(tfSponsorshipTransferMask | tfUniversal);
    if (std::popcount(flagsSet) != 1)
    {
        JLOG(ctx.j.debug()) << "preflight: Only one SponsorshipTransfer flag can be set per tx.";
        return temINVALID_FLAG;
    }

    if (ctx.tx.isFlag(tfSponsorshipCreate))
    {
        if (!isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve should be set when creating sponsorship";
            return temINVALID_FLAG;
        }
        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsee should be available only when ending sponsorship";
            return temMALFORMED;
        }
    }
    if (ctx.tx.isFlag(tfSponsorshipReassign))
    {
        if (!isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve should be set when reassigning sponsorship";
            return temINVALID_FLAG;
        }
        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsee should not be set when reassigning sponsorship";
            return temMALFORMED;
        }
    }
    if (ctx.tx.isFlag(tfSponsorshipEnd))
    {
        if (isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve should not be set when ending sponsorship";
            return temINVALID_FLAG;
        }

        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            if (ctx.tx.getAccountID(sfSponsee) == ctx.tx.getAccountID(sfAccount))
            {
                JLOG(ctx.j.debug()) << "preflight: sfSponsee should not be the same as the account";
                return temMALFORMED;
            }
        }
    }

    // When an account sponsoring, sfSponsorSignature must be provided
    auto const newSponsor = getTxReserveSponsorAccountID(ctx.tx);
    bool const isObjectSponsor = ctx.tx.isFieldPresent(sfObjectID);

    // both sfSponsor and sfObjectID are provided
    bool const isNewAccountSponsor = newSponsor && !isObjectSponsor;

    if (isNewAccountSponsor && !ctx.tx.isFieldPresent(sfSponsorSignature))
    {
        JLOG(ctx.j.debug()) << "preflight: sponsoring an account needs co-signing sponsor";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
SponsorshipTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const index = ctx.tx[~sfObjectID];
    bool const isObjectSponsor = index != std::nullopt;
    auto const newSponsorSle = getTxReserveSponsor(ctx.view, ctx.tx);
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

        auto const ownerCountDelta = getLedgerEntryOwnerCount(*sle);
        auto const owner = getLedgerEntryOwner(ctx.view, *sle, sponseeID);
        if (!owner || owner != sponseeID)
            return tecNO_PERMISSION;

        auto const& sponsorField = getLedgerEntrySponsorField(*sle, *owner);

        if (ctx.tx.isFlag(tfSponsorshipCreate))
        {
            if (!newSponsorSle)
                return tecNO_PERMISSION;

            // check object is not sponsored yet
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
        // (AccountReserve = 0). However, by setting reserveCountAdj = 1 here, we are able to
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

    if (isObjectSponsor)
    {
        auto const hasSignature = tx.isFieldPresent(sfSponsorSignature);

        // transfer object sponsor
        auto const objSle = view().peek(keylet::unchecked(*index));
        if (!objSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerAccountID = getLedgerEntryOwner(view(), *objSle, sponseeID);
        if (!ownerAccountID)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(*ownerAccountID));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        std::int64_t const ownerCountDelta = getLedgerEntryOwnerCount(*objSle);

        auto const& sponsorField = getLedgerEntrySponsorField(*objSle, *ownerAccountID);

        if (ctx_.tx.isFlag(tfSponsorshipCreate))
        {
            auto const newSponsorID = tx.getAccountID(sfSponsor);
            XRPL_ASSERT(!!newSponsorID, "New sponsor is required when creating sponsorship");

            // update owner's sponsored count
            if (auto const ter =
                    setSponsorFieldU32(*ownerSle, sfSponsoredOwnerCount, ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(ownerSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter =
                    setSponsorFieldU32(*newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
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
                    setSponsorFieldU32(*oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(oldSponsorSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter =
                    setSponsorFieldU32(*newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
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
                    setSponsorFieldU32(*sponseeSle, sfSponsoredOwnerCount, -ownerCountDelta);
                !isTesSuccess(ter))
                return ter;
            view().update(sponseeSle);

            // decrement old sponsoring count
            if (auto const ter =
                    setSponsorFieldU32(*oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
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
            if (auto const ter = setSponsorFieldU32(*newSponsorSle, sfSponsoringAccountCount, 1);
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
            if (auto const ter = setSponsorFieldU32(*newSponsorSle, sfSponsoringAccountCount, 1);
                !isTesSuccess(ter))
                return ter;
            view().update(newSponsorSle);

            // decrement old sponsoring count
            auto const oldSponsorID = sponseeSle->getAccountID(sfSponsor);
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            if (auto const ter = setSponsorFieldU32(*oldSponsorSle, sfSponsoringAccountCount, -1);
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
            if (auto const ter = setSponsorFieldU32(*oldSponsorSle, sfSponsoringAccountCount, -1);
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
