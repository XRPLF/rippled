#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

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

inline SLE::pointer
getTxReserveSponsor(ApplyView& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
        return view.peek(keylet::account(*sponsorID));
    return {};
}

inline SLE::const_pointer
getTxReserveSponsor(ReadView const& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
        return view.read(keylet::account(*sponsorID));
    return {};
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

}  // namespace xrpl
