#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STXChainBridge.h>

namespace xrpl {

template <typename ViewT>
class BridgeEntry : public SLEBase<ViewT, ltBRIDGE>
{
public:
    using Base = SLEBase<ViewT, ltBRIDGE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit BridgeEntry(
        STXChainBridge const& bridge,
        STXChainBridge::ChainType chainType,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::bridge(bridge, chainType), view, j)
    {
    }
};

using BridgeEntryR = BridgeEntry<ReadView>;
using BridgeEntryW = BridgeEntry<ApplyView>;

}  // namespace xrpl
