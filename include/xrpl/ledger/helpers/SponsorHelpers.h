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

inline std::shared_ptr<SLE>
getTxReserveSponsor(ApplyView& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
        return view.peek(keylet::account(*sponsorID));
    return {};
}

inline std::shared_ptr<SLE const>
getTxReserveSponsor(ReadView const& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx);
    if (sponsorID)
        return view.read(keylet::account(*sponsorID));
    return {};
}

inline std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(
    std::shared_ptr<SLE const> const& sle,
    SF_ACCOUNT const& field = sfSponsor)
{
    if (sle->isFieldPresent(field))
        return sle->getAccountID(field);
    return {};
}

inline std::shared_ptr<SLE>
getLedgerEntryReserveSponsor(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    SF_ACCOUNT const& field = sfSponsor)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.peek(keylet::account(*sponsorID));
    return {};
}

inline std::shared_ptr<SLE const>
getLedgerEntryReserveSponsor(
    ReadView const& view,
    std::shared_ptr<SLE const> const& sle,
    SF_ACCOUNT const& field = sfSponsor)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.read(keylet::account(*sponsorID));
    return {};
}

inline void
addSponsorToLedgerEntry(
    std::shared_ptr<SLE> const& sle,
    std::shared_ptr<SLE> const& sponsorSle,
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
removeSponsorFromLedgerEntry(std::shared_ptr<SLE> const& sle, SF_ACCOUNT const& field = sfSponsor)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "removeSponsorFromLedgerEntry : Invalid field to the LedgerEntry");
    if (sle->isFieldPresent(field))
        sle->makeFieldAbsent(field);
}

}  // namespace xrpl
