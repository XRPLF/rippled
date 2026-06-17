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

inline std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx, std::optional<AccountID> const& acc = {})
{
    return (!acc || acc == tx[sfAccount]) && tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx)
        ? std::make_optional(tx[sfSponsor])
        : std::nullopt;
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
            if (sle.isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == owner)
                    return sfLowSponsor;
            }
            // LCOV_EXCL_START
            UNREACHABLE("Should not happen. Owner should be checked before calling this function.");
            // LCOV_EXCL_STOP
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

// namespace sponsor
// {
// // Accessing the ledger to check if provided sponsor is valid.
// [[nodiscard]] TER
// valid(ReadView const& view, STTx const& tx, beast::Journal j)
// {
// }
// }

}  // namespace xrpl
