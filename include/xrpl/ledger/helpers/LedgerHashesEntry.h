#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class LedgerHashesEntry : public SLEBase<ViewT, ltLEDGER_HASHES>
{
public:
    using Base = SLEBase<ViewT, ltLEDGER_HASHES>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LedgerHashesEntry(
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::skip(), view, j)
    {
    }
};

using RLedgerHashesEntry = LedgerHashesEntry<ReadView>;
using WLedgerHashesEntry = LedgerHashesEntry<ApplyView>;

}  // namespace xrpl
