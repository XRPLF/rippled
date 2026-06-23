#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>

#include <expected>
#include <type_traits>
#include <utility>

namespace xrpl {

SLE::pointer
getTxReserveSponsor(ApplyView& view, STTx const& tx, std::optional<AccountID> const& acc = {});

SLE::const_pointer
getTxReserveSponsor(ReadView const& view, STTx const& tx, std::optional<AccountID> const& acc = {});

SLE::pointer
getLedgerEntryReserveSponsor(
    ApplyView& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor);

SLE::const_pointer
getLedgerEntryReserveSponsor(
    ReadView const& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor);

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
    return isReserveSponsored(tx) && tx.isFieldPresent(sfSponsorSignature);
}

/** Optional account is an additional check, that we are checking sponsor for correct account
 * In a functions account can be named something like dstAccount or else, and it is a bit confusing.
 * Passing the account to this function will mitigate the risk.
 */
inline std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx, std::optional<AccountID> const& acc = {})
{
    if ((!acc || acc == tx[sfAccount]) && tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx))
        return tx[sfSponsor];
    return std::nullopt;
}

inline std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor)
{
    if (sle->isFieldPresent(field))
        return sle->at(field);
    return std::nullopt;
}

inline void
addSponsorToLedgerEntry(
    SLE::ref sle,
    SLE::const_ref sponsorSle,
    SF_ACCOUNT const& field = sfSponsor)
{
    if (sponsorSle)
    {
        XRPL_ASSERT(
            (sle->getType() == ltRIPPLE_STATE &&
             (field == sfHighSponsor || field == sfLowSponsor)) ||
                (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
            "addSponsorToLedgerEntry : field type");

        XRPL_ASSERT(
            sponsorSle->getType() == ltACCOUNT_ROOT, "addSponsorToLedgerEntry : sponsor type");

        sle->at(field) = sponsorSle->at(sfAccount);
    }
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
