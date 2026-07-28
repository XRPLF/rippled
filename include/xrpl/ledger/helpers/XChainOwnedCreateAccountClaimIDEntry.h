#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class XChainOwnedCreateAccountClaimIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit XChainOwnedCreateAccountClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainCreateAccountClaimID(bridge, seq), view, j)
    {
    }
};

}  // namespace xrpl
