#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/SetOracle.h>
#include <xrpl/tx/transactors/Sponsor/SponsorshipTransfer.h>

namespace xrpl {

NotTEC
SponsorshipTransfer::preflight(PreflightContext const& ctx)
{
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
            return SetOracle::calculateOracleReserve(sle->getFieldArray(sfPriceDataSeries).size());
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
    auto const newSponsor = getTxReserveSponsor(ctx.view, ctx.tx);

    bool const isObjectSponsor = index != std::nullopt;

    auto const account = ctx.tx[sfAccount];

    auto const accSle = ctx.view.read(keylet::account(account));
    if (!accSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (isObjectSponsor)
    {
        auto const sle = ctx.view.read(keylet::unchecked(*index));
        if (!sle)
            return tecNO_ENTRY;

        auto const ownerCountDelta = getLedgerEntryOwnerCount(sle);

        auto const owner = getLedgerEntryOwner(ctx.view, sle, account);
        if (!owner || owner != account)
            return tecNO_PERMISSION;

        auto const& sponsorField = getLedgerEntrySponsorField(sle, *owner);

        if (newSponsor)
        {
            if (sle->isFieldPresent(sponsorField))
            {
                // transfer sponsor
                // check if the object owner isn't the same as the new sponsor
                if (newSponsor->getAccountID(sfAccount) == owner)
                    // checked in above
                    return tecINTERNAL;  // LCOV_EXCL_LINE
            }
        }
        else
        {
            // dissolve sponsor
            // check object is sponsored
            if (!sle->isFieldPresent(sponsorField))
                return tecNO_PERMISSION;
        }

        // check new sponsor have sufficient balance
        if (auto const ter = checkInsufficientReserve(
                ctx.view, ctx.tx, accSle, accSle->getFieldAmount(sfBalance), newSponsor, ownerCountDelta);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        if (newSponsor)
        {
            if (accSle->isFieldPresent(sfSponsor))
            {
                // check not same account
                if (newSponsor->getAccountID(sfAccount) == accSle->getAccountID(sfAccount))
                    // already checked in Transactor::preflight1()
                    return tecINTERNAL;  // LCOV_EXCL_LINE
            }
        }
        else
        {
            // dissolve sponsor
            // check account is sponsored
            if (!accSle->isFieldPresent(sfSponsor))
                return tecNO_PERMISSION;
        }

        // check account have sufficient balance
        // In the case of removing an account sponsor, accSle should have no sfSponsor set (AccountReserve = 0).
        // However, by setting accountCountDelta = 1 here, we are able to calculate the actual required Account Reserve.
        if (auto const ter =
                checkInsufficientReserve(ctx.view, ctx.tx, accSle, accSle->getFieldAmount(sfBalance), newSponsor, 0, 1);
            !isTesSuccess(ter))
            return ter;
    }

    return tesSUCCESS;
}

TER
adjustReserveCount(ApplyView& view, AccountID const& account, AccountID const& sponsor, int32_t delta)
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
    bool const isObjectSponsor = index != std::nullopt;

    auto const accSle = view().peek(keylet::account(account_));
    if (!accSle)
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

        auto const owner = getLedgerEntryOwner(view(), objSle, account_);
        if (!owner)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(*owner));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerCountDelta = getLedgerEntryOwnerCount(objSle);

        auto const& sponsorField = getLedgerEntrySponsorField(objSle, *owner);

        if (tx.isFieldPresent(sfSponsor))
        {
            auto const oldSponsor = objSle->getAccountID(sponsorField);
            auto const newSponsor = tx.getAccountID(sfSponsor);
            // decrement old sponsoring count if exists
            if (auto const oldSponsorSle = view().peek(keylet::account(oldSponsor)))
            {
                setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
                view().update(oldSponsorSle);
            }
            else
            {
                // update owner's sponsored count
                setSponsorFieldU32(ownerSle, sfSponsoredOwnerCount, ownerCountDelta);
                view().update(ownerSle);
            }

            // increment new sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsor));

            setSponsorFieldU32(newSponsorSle, sfSponsoringOwnerCount, ownerCountDelta);
            view().update(newSponsorSle);

            objSle->setAccountID(sponsorField, newSponsor);
            view().update(objSle);

            if (!hasSignature)
            {
                // pre-funded sponsor
                if (auto const ter = adjustReserveCount(view(), account_, newSponsor, -ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
            }

            // payback the reserve count if ltSponsorship exists
            if (auto const sponsorSle = view().exists(keylet::sponsor(oldSponsor, account_)); sponsorSle)
                if (auto const ter = adjustReserveCount(view(), account_, oldSponsor, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;
        }
        else
        {
            // dissolve object sponsor
            auto const oldSponsor = objSle->getAccountID(sponsorField);
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsor));
            if (!oldSponsorSle)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            // decrement sponsored count
            setSponsorFieldU32(accSle, sfSponsoredOwnerCount, -ownerCountDelta);

            view().update(accSle);

            // decrement old sponsoring count
            setSponsorFieldU32(oldSponsorSle, sfSponsoringOwnerCount, -ownerCountDelta);
            view().update(oldSponsorSle);

            // payback the reserve count if ltSponsorship exists
            if (auto const sponsorSle = view().exists(keylet::sponsor(oldSponsor, account_)); sponsorSle)
                if (auto const ter = adjustReserveCount(view(), account_, oldSponsor, ownerCountDelta);
                    !isTesSuccess(ter))
                    return ter;

            // remove sponsor from object
            objSle->makeFieldAbsent(sponsorField);
            view().update(objSle);
        }
    }
    else
    {
        // Transfer Account sponsor
        if (tx.isFieldPresent(sfSponsor))
        {
            // transfer account sponsor
            // increment new sponsoring count
            auto const newSponsor = tx.getAccountID(sfSponsor);
            auto const newSponsorSle = view().peek(keylet::account(newSponsor));
            setSponsorFieldU32(newSponsorSle, sfSponsoringAccountCount, 1);

            view().update(newSponsorSle);
            // decrement old sponsoring count
            if (accSle->isFieldPresent(sfSponsor))
            {
                auto const oldSponsor = accSle->getAccountID(sfSponsor);
                auto const oldSponsorSle = view().peek(keylet::account(oldSponsor));
                setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
                view().update(oldSponsorSle);
            }
            accSle->setAccountID(sfSponsor, newSponsor);
            view().update(accSle);
        }
        else
        {
            // dissolve account sponsor
            auto const oldSponsor = accSle->getAccountID(sfSponsor);
            accSle->makeFieldAbsent(sfSponsor);
            // decrement account sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsor));
            setSponsorFieldU32(oldSponsorSle, sfSponsoringAccountCount, -1);
            view().update(oldSponsorSle);
        }
    }

    return tesSUCCESS;
}

}  // namespace xrpl
