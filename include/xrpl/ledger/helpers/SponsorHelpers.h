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

// Optional account is an additional check, that we are checking sponsor for correct account
// In a functions account can be named something like dstAccount or else, and it is a bit confusing.
// Passing the account to this function will mitigate the risk.
inline std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx, std::optional<AccountID> const& acc = {})
{
    if ((!acc || acc == tx[sfAccount]) && tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx))
        return tx[sfSponsor];
    return std::nullopt;
}

template <class V>
auto
getTxReserveSponsor(V&& view, STTx const& tx, std::optional<AccountID> const& acc = {})
{
    auto const sponsorID = getTxReserveSponsorAccountID(tx, acc);
    if (sponsorID)
    {
        if constexpr (std::is_base_of_v<ApplyView, std::remove_cvref_t<decltype(view)>>)
        {
            auto sle = view.peek(keylet::account(*sponsorID));
            // already checked in Transactor::checkSponsor
            if (!sle)
                Throw<std::runtime_error>("Empty sponsor");  // LCOV_EXCL_LINE
            return sle;
        }
        else
        {
            auto sle = view.read(keylet::account(*sponsorID));
            // already checked in Transactor::checkSponsor
            if (!sle)
                Throw<std::runtime_error>("Empty sponsor");  // LCOV_EXCL_LINE
            return sle;
        }
    }

    if constexpr (std::is_base_of_v<ApplyView, std::remove_cvref_t<decltype(view)>>)
    {
        return SLE::pointer();
    }
    else
    {
        return SLE::const_pointer();
    }
}

inline SF_ACCOUNT const&
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
            auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
            XRPL_ASSERT(lowAccount == owner, "getLedgerEntrySponsorField lowAccount == owner");
            return sfLowSponsor;
        }
        default:
            return sfSponsor;
    }
}

inline std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor)
{
    if (sle->isFieldPresent(field))
        return sle->at(field);
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
