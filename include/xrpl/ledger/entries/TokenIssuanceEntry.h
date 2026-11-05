#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

template <typename ViewT>
class TokenIssuanceEntry : public SLEBase<ViewT, ltTOKEN_ISSUANCE>
{
public:
    using Base = SLEBase<ViewT, ltTOKEN_ISSUANCE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit TokenIssuanceEntry(
        AccountID const& issuer,
        Currency const& currency,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::tokenIssuance(issuer, currency), view, j)
    {
    }
};

using TokenIssuanceEntryR = TokenIssuanceEntry<ReadView>;
using TokenIssuanceEntryW = TokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
