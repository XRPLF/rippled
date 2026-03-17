#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>

namespace xrpl {

bool
isReserveSponsored(STTx const& tx);

bool
isSponsorReserveCoSigning(STTx const& tx);

std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx);

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

std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(
    std::shared_ptr<SLE const> const& sle,
    SF_ACCOUNT const& field = sfSponsor);

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

void
addSponsorToLedgerEntry(
    std::shared_ptr<SLE> const& sle,
    std::shared_ptr<SLE> const& sponsorSle,
    SF_ACCOUNT const& field = sfSponsor);

void
removeSponsorFromLedgerEntry(std::shared_ptr<SLE> const& sle, SF_ACCOUNT const& field = sfSponsor);

}  // namespace xrpl
