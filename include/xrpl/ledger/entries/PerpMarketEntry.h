#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

template <typename ViewT>
class PerpMarketEntry : public SLEBase<ViewT, ltPERP_MARKET>
{
public:
    using Base = SLEBase<ViewT, ltPERP_MARKET>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PerpMarketEntry(
        UInt256 const& exchangeID,
        Currency const& base,
        Currency const& quote,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::perpMarket(exchangeID, base, quote), view, j)
    {
    }

    explicit PerpMarketEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::perpMarket(key), view, j)
    {
    }
};

using PerpMarketEntryR = PerpMarketEntry<ReadView>;
using PerpMarketEntryW = PerpMarketEntry<ApplyView>;

}  // namespace xrpl
