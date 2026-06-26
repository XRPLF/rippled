#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>

#include <expected>

namespace xrpl {

inline bool
isFeeSponsored(STTx const& tx)
{
    return (tx.getFieldU32(sfSponsorFlags) & spfSponsorFee) != 0u;
}

inline bool
isReserveSponsored(STTx const& tx)
{
    return (tx.getFieldU32(sfSponsorFlags) & spfSponsorReserve) != 0u;
}

inline bool
isSponsorReserveCoSigning(STTx const& tx)
{
    if (!tx.isFieldPresent(sfSponsorSignature))
        return false;
    return isReserveSponsored(tx);
}

inline std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx)
{
    if (tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx))
    {
        return tx.getAccountID(sfSponsor);
    }
    return {};
}

inline std::expected<SLE::pointer, TER>
getTxReserveSponsor(ApplyView& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
    {
        auto sle = view.peek(keylet::account(*sponsorID));

        // already checked in Transactor::checkSponsor
        if (!sle)
            return std::unexpected(tecINTERNAL);
        return sle;
    }
    return SLE::pointer();
}

inline std::expected<SLE::const_pointer, TER>
getTxReserveSponsor(ReadView const& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
    {
        auto sle = view.read(keylet::account(*sponsorID));

        // already checked in Transactor::checkSponsor
        if (!sle)
            return std::unexpected(tecINTERNAL);
        return sle;
    }
    return SLE::pointer();
}

inline std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor)
{
    if (sle->isFieldPresent(field))
        return sle->getAccountID(field);
    return {};
}

inline SLE::pointer
getLedgerEntryReserveSponsor(
    ApplyView& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.peek(keylet::account(*sponsorID));
    return {};
}

inline SLE::const_pointer
getLedgerEntryReserveSponsor(
    ReadView const& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.read(keylet::account(*sponsorID));
    return {};
}

inline void
addSponsorToLedgerEntry(
    SLE::ref sle,
    SLE::const_ref sponsorSle,
    SF_ACCOUNT const& field = sfSponsor)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "addSponsorToLedgerEntry : Invalid field to the LedgerEntry");
    if (sponsorSle)
        sle->setAccountID(field, sponsorSle->getAccountID(sfAccount));
}

inline void
removeSponsorFromLedgerEntry(SLE::ref sle, SF_ACCOUNT const& field = sfSponsor)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "removeSponsorFromLedgerEntry : Invalid field to the LedgerEntry");
    if (sle->isFieldPresent(field))
        sle->makeFieldAbsent(field);
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
        case ltLOAN_BROKER:
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
        case ltLOAN:
            return sle->getAccountID(sfBorrower);
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
            return calculateOracleReserve(sle->getFieldArray(sfPriceDataSeries).size());
        }
        // Vaults require 2 owner counts (the vault and a pseudo-account)
        case ltVAULT:
            return 2;
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
            UNREACHABLE("Should not happen. Owner should be checked before calling this function.");
            return sfSponsor;
            // LCOV_EXCL_STOP
        }
        default:
            return sfSponsor;
    }
};

}  // namespace xrpl
