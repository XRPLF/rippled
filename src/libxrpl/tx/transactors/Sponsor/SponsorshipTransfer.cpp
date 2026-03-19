#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/oracle/OracleSet.h>
#include <xrpl/tx/transactors/sponsor/SponsorshipTransfer.h>

namespace xrpl {

std::uint32_t
SponsorshipTransfer::getFlagsMask(PreflightContext const& ctx)
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

    if (flags & tfSponsorshipCreate)
    {
        if (!isReserveSponsored(ctx.tx))
        {
            JLOG(ctx.j.debug())
                << "preflight: spfSponsorReserve should not be set when creating sponsorship";
            return temINVALID_FLAG;
        }
        if (ctx.tx.isFieldPresent(sfSponsee))
        {
            JLOG(ctx.j.debug())
                << "preflight: sfSponsee should be available only when ending sponsorship";
            return temMALFORMED;
        }
    }
    if (flags & tfSponsorshipReassign)
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
    if (flags & tfSponsorshipEnd)
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

template <typename T>
inline std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, T const& sle, AccountID const& account)
{
    switch (sle->getType())
    {
        case ltNFTOKEN_OFFER:
        case ltORACLE:
        case ltPERMISSIONED_DOMAIN:
        case ltVAULT:
            return sle->getAccountID(sfOwner);
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
            return sle->getAccountID(sfAccount);
        case ltMPTOKEN_ISSUANCE:
            return sle->getAccountID(sfIssuer);
        case ltSIGNER_LIST: {
            auto const signerList = view.read(keylet::signers(account));
            if (!signerList)
                return std::nullopt;
            if (signerList->key() == sle->key())
                return account;
            return std::nullopt;
        }
        case ltCREDENTIAL: {
            if (sle->isFlag(lsfAccepted))
                return sle->getAccountID(sfSubject);
            return sle->getAccountID(sfIssuer);
        }
        case ltNFTOKEN_PAGE: {
            // the upper 20 bytes of the index of ltNFTokenPage are the Owner's
            // AccountID
            uint256 const& key = sle->key();
            return AccountID::fromVoid(key.data());
        }
        case ltRIPPLE_STATE: {
            if (sle->isFlag(lsfHighReserve))
            {
                auto const highAccount = sle->getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == account)
                    return highAccount;
            }
            if (sle->isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle->getFieldAmount(sfLowLimit).getIssuer();
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

template <typename T>
inline std::uint32_t
getLedgerEntryOwnerCount(T const& sle)
{
    switch (sle->getType())
    {
        case ltORACLE: {
            return OracleSet::calculateOracleReserve(sle->getFieldArray(sfPriceDataSeries).size());
        }
        default:
            return 1;
    }
};

template <typename T>
inline SF_ACCOUNT const&
getLedgerEntrySponsorField(T const& sle, AccountID const& owner)
{
    switch (sle->getType())
    {
        case ltRIPPLE_STATE: {
            if (sle->isFlag(lsfHighReserve))
            {
                auto const highAccount = sle->getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == owner)
                    return sfHighSponsor;
            }
            if (sle->isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle->getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == owner)
                    return sfLowSponsor;
            }
            // LCOV_EXCL_START
            XRPL_ASSERT(
                false,
                "Should not happen. Owner should be checked before calling "
                "this function.");
            // LCOV_EXCL_STOP
        }
        default:
            return sfSponsor;
    }
};

TER
SponsorshipTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const index = ctx.tx[~sfObjectID];
    auto const flags = ctx.tx.getFlags();
    auto const newSponsor = getTxReserveSponsor(ctx.view, ctx.tx);

    bool const isObjectSponsor = index != std::nullopt;

    auto const account = ctx.tx[sfAccount];

    auto const sponseeAccountID = ctx.tx[~sfSponsee].value_or(account);
    auto const sponseeSle = ctx.view.read(keylet::account(sponseeAccountID));
    if (!sponseeSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (isObjectSponsor)
    {
        auto const sle = ctx.view.read(keylet::unchecked(*index));
        if (!sle)
            return tecNO_ENTRY;

        auto const ownerCountDelta = getLedgerEntryOwnerCount(sle);

        auto const owner = getLedgerEntryOwner(ctx.view, sle, sponseeAccountID);
        if (!owner || owner != sponseeAccountID)
            return tecNO_PERMISSION;

        auto const& sponsorField = getLedgerEntrySponsorField(sle, *owner);

        if (flags & tfSponsorshipCreate)
        {
            if (!newSponsor)
                return tecNO_PERMISSION;

            // check object is not sponsored yet
            if (sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }
        else if (flags & tfSponsorshipReassign)
        {
            if (!newSponsor)
                return tecNO_PERMISSION;

            // check object is already sponsored
            if (!sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }
        else if (flags & tfSponsorshipEnd)
        {
            if (newSponsor)
                return tecNO_PERMISSION;

            // check object is sponsored
            if (!sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }

        // check new sponsor have sufficient balance
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                ctx.tx,
                sponseeSle,
                sponseeSle->getFieldAmount(sfBalance),
                newSponsor,
                ownerCountDelta);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        if (flags & tfSponsorshipCreate)
        {
            if (!newSponsor)
                return tecNO_PERMISSION;

            // check account is not sponsored yet
            if (sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }
        else if (flags & tfSponsorshipReassign)
        {
            if (!newSponsor)
                return tecNO_PERMISSION;

            // check account is already sponsored
            if (!sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }
        else if (flags & tfSponsorshipEnd)
        {
            if (newSponsor)
                return tecNO_PERMISSION;

            // check account is sponsored
            if (!sponseeSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }

        // check account have sufficient balance
        // In the case of removing an account sponsor, accSle should have no sfSponsor set
        // (AccountReserve = 0). However, by setting accountCountDelta = 1 here, we are able to
        // calculate the actual required Account Reserve.
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                ctx.tx,
                sponseeSle,
                sponseeSle->getFieldAmount(sfBalance),
                newSponsor,
                0,
                1);
            !isTesSuccess(ter))
            return ter;
    }

    return tesSUCCESS;
}

TER
adjustReserveCount(
    ApplyView& view,
    AccountID const& account,
    AccountID const& sponsor,
    int32_t delta)
{
    if (delta == 0)
        return tesSUCCESS;
    auto const sponsorKeylet = keylet::sponsor(sponsor, account);
    auto const sponsorSle = view.peek(sponsorKeylet);
    if (!sponsorSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const reserveCount = sponsorSle->getFieldU32(sfReserveCount);
    int32_t const afterReserveCount = reserveCount + delta;

    if (afterReserveCount < 0)
        // already checked in preclaim()
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (afterReserveCount == 0)
        sponsorSle->makeFieldAbsent(sfReserveCount);
    else
        sponsorSle->setFieldU32(sfReserveCount, afterReserveCount);
    view.update(sponsorSle);
    return tesSUCCESS;
}

TER
SponsorshipTransfer::doApply()
{
    auto const& tx = ctx_.tx;

    auto const index = tx[~sfObjectID];
    auto const flags = tx.getFlags();
    bool const isObjectSponsor = index != std::nullopt;

    auto const sponseeAccountID = tx[~sfSponsee].value_or(account_);
    auto const sponseeSle = view().peek(keylet::account(sponseeAccountID));
    if (!sponseeSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const setSponsorFieldU32 = [](auto const& sle, auto const& field, auto const& delta) {
        auto const newValue = sle->getFieldU32(field) + delta;
        if (newValue == 0)
            sle->makeFieldAbsent(field);
        else
            sle->setFieldU32(field, newValue);
    };

    if (isObjectSponsor)
    {
        auto const hasSignature = tx.isFieldPresent(sfSponsorSignature);

        // transfer object sponsor
        auto const objSle = view().peek(keylet::unchecked(*index));
        if (!objSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerAccountID = getLedgerEntryOwner(view(), objSle, account_);
        if (!ownerAccountID)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(*ownerAccountID));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerCountDelta = getLedgerEntryOwnerCount(objSle);

        auto const& sponsorField = getLedgerEntrySponsorField(objSle, *ownerAccountID);

        if (flags & tfSponsorshipCreate)
        {
            auto const newSponsorAccountID = tx.getAccountID(sfSponsor);
            XRPL_ASSERT(!!newSponsorAccountID, "New sponsor is required when creating sponsorship");

            // update owner's sponsored count
            setSponsorFieldU32(ownerSle, sfSponsoredOwnerCount, ownerCountDelta);
            view().update(ownerSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorAccountID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
            view().update(newSponsorSle);

            // set new sponsor to object
            objSle->setAccountID(sponsorField, newSponsorAccountID);
            view().update(objSle);

            if (!hasSignature)
            {
                // use ReserveCount for pre-funded sponsoring
                if (auto const ter =
                        adjustReserveCount(view(), account_, newSponsorAccountID, -ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
            }
        }
        else if (flags & tfSponsorshipReassign)
        {
            auto const newSponsorAccountID = tx.getAccountID(sfSponsor);
            XRPL_ASSERT(
                !!newSponsorAccountID, "New sponsor is required when reassigning sponsorship");

            auto const oldSponsorAccountID = objSle->getAccountID(sponsorField);
            XRPL_ASSERT(
                !!oldSponsorAccountID, "Old sponsor is required when reassigning sponsorship");

            // decrement old sponsor's sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorAccountID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
            view().update(oldSponsorSle);

            // increment new sponsor's sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsorAccountID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
            view().update(newSponsorSle);

            // set new sponsor to object
            objSle->setAccountID(sponsorField, newSponsorAccountID);
            view().update(objSle);

            if (!hasSignature)
            {
                // use ReserveCount for pre-funded sponsoring
                if (auto const ter =
                        adjustReserveCount(view(), account_, newSponsorAccountID, -ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
            }

            // payback the reserve count if ltSponsorship exists
            if (auto const sponsorSle =
                    view().exists(keylet::sponsor(oldSponsorAccountID, account_));
                sponsorSle)
                if (auto const ter =
                        adjustReserveCount(view(), account_, oldSponsorAccountID, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
        }
        else if (flags & tfSponsorshipEnd)
        {
            auto const oldSponsorAccountID = objSle->getAccountID(sponsorField);
            XRPL_ASSERT(!!oldSponsorAccountID, "Old sponsor is required when ending sponsorship");

            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorAccountID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // decrement sponsored count
            setSponsorFieldU32(sponseeSle, sfSponsoredOwnerCount, -ownerCountDelta);
            view().update(sponseeSle);

            // decrement old sponsoring count
            setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
            view().update(oldSponsorSle);

            // payback the reserve count if ltSponsorship exists
            if (auto const sponsorSle =
                    view().exists(keylet::sponsor(oldSponsorAccountID, account_));
                sponsorSle)
                if (auto const ter =
                        adjustReserveCount(view(), account_, oldSponsorAccountID, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;

            // remove sponsor from object
            objSle->makeFieldAbsent(sponsorField);
            view().update(objSle);
        }
    }
    else
    {
        if (flags & tfSponsorshipCreate)
        {
            // create account sponsor
            // increment new sponsoring count
            auto const newSponsorAccountID = tx.getAccountID(sfSponsor);
            auto const newSponsorSle = view().peek(keylet::account(newSponsorAccountID));
            if (!newSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(newSponsorSle, sfSponsoringAccountCount, 1);
            view().update(newSponsorSle);

            // set new sponsor to account
            sponseeSle->setAccountID(sfSponsor, newSponsorAccountID);
            view().update(sponseeSle);
        }
        else if (flags & tfSponsorshipReassign)
        {
            // reassign account sponsor
            // increment new sponsoring count
            auto const newSponsorAccountID = tx.getAccountID(sfSponsor);
            auto const newSponsorSle = view().peek(keylet::account(newSponsorAccountID));
            setSponsorFieldU32(newSponsorSle, sfSponsoringAccountCount, 1);
            view().update(newSponsorSle);

            // decrement old sponsoring count
            auto const oldSponsor = sponseeSle->getAccountID(sfSponsor);
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsor));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
            view().update(oldSponsorSle);

            // set new sponsor to account
            sponseeSle->setAccountID(sfSponsor, newSponsorAccountID);
            view().update(sponseeSle);
        }
        else if (flags & tfSponsorshipEnd)
        {
            // dissolve account sponsor
            auto const oldSponsorAccountID = sponseeSle->getAccountID(sfSponsor);
            sponseeSle->makeFieldAbsent(sfSponsor);
            view().update(sponseeSle);

            // decrement account sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsorAccountID));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE
            setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
            view().update(oldSponsorSle);
        }
    }

    return tesSUCCESS;
}

}  // namespace xrpl
