#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

template <typename ViewT>
class PerpExchangeEntry : public SLEBase<ViewT, ltPERP_EXCHANGE>
{
public:
    using Base = SLEBase<ViewT, ltPERP_EXCHANGE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PerpExchangeEntry(
        AccountID const& owner,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::perpExchange(owner, seq), view, j)
    {
    }

    explicit PerpExchangeEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::perpExchange(key), view, j)
    {
    }
};

using PerpExchangeEntryR = PerpExchangeEntry<ReadView>;
using PerpExchangeEntryW = PerpExchangeEntry<ApplyView>;

}  // namespace xrpl
