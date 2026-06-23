#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include "xrpl/basics/contract.h"
#include "xrpl/ledger/ApplyView.h"
#include "xrpl/ledger/ReadView.h"
#include "xrpl/protocol/AccountID.h"
#include "xrpl/protocol/Indexes.h"
#include "xrpl/protocol/SField.h"
#include "xrpl/protocol/STLedgerEntry.h"
#include "xrpl/protocol/STTx.h"
#include <optional>
#include <stdexcept>

namespace xrpl {

SLE::pointer
getTxReserveSponsor(ApplyView& view, STTx const& tx, std::optional<AccountID> const& acc)
{
    if (auto const sponsorID = getTxReserveSponsorAccountID(tx, acc))
    {
        auto sle = view.peek(keylet::account(*sponsorID));
        if (!sle)
            Throw<std::runtime_error>("Empty sponsor");  // LCOV_EXCL_LINE
        return sle;
    }
    return {};
}

SLE::const_pointer
getTxReserveSponsor(ReadView const& view, STTx const& tx, std::optional<AccountID> const& acc)
{
    if (auto const sponsorID = getTxReserveSponsorAccountID(tx, acc))
    {
        auto sle = view.read(keylet::account(*sponsorID));
        if (!sle)
            Throw<std::runtime_error>("Empty sponsor");  // LCOV_EXCL_LINE
        return sle;
    }
    return {};
}

SLE::pointer
getLedgerEntryReserveSponsor(ApplyView& view, SLE::const_ref sle, SF_ACCOUNT const& field)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.peek(keylet::account(*sponsorID));
    return {};
}

SLE::const_pointer
getLedgerEntryReserveSponsor(ReadView const& view, SLE::const_ref sle, SF_ACCOUNT const& field)
{
    auto const sponsorID = getLedgerEntryReserveSponsorAccountID(sle, field);
    if (sponsorID)
        return view.read(keylet::account(*sponsorID));
    return {};
}

}  // namespace xrpl
