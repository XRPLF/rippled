#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/STXChainBridge.h>

namespace xrpl {

template <typename ViewT>
class BridgeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit BridgeEntry(
        STXChainBridge const& bridge,
        STXChainBridge::ChainType chainType,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::bridge(bridge, chainType), view, j)
    {
    }
};

}  // namespace xrpl
