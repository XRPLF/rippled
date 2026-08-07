#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class XChainOwnedClaimIDEntry : public SLEBase<ViewT, ltXCHAIN_OWNED_CLAIM_ID>
{
public:
    using Base = SLEBase<ViewT, ltXCHAIN_OWNED_CLAIM_ID>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit XChainOwnedClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::xChainClaimID(bridge, seq), view, j)
    {
    }
};

using RXChainOwnedClaimIDEntry = XChainOwnedClaimIDEntry<ReadView>;
using WXChainOwnedClaimIDEntry = XChainOwnedClaimIDEntry<ApplyView>;

}  // namespace xrpl
