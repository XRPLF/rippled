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
class TokenPreauthEntry : public SLEBase<ViewT, ltTOKEN_PREAUTH>
{
public:
    using Base = SLEBase<ViewT, ltTOKEN_PREAUTH>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit TokenPreauthEntry(
        AccountID const& holder,
        MPTID const& issuanceID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::tokenPreauth(holder, issuanceID), view, j)
    {
    }
};

using TokenPreauthEntryR = TokenPreauthEntry<ReadView>;
using TokenPreauthEntryW = TokenPreauthEntry<ApplyView>;

}  // namespace xrpl
