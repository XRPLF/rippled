#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class XChainOwnedCreateAccountClaimIDEntry
    : public SLEBase<ViewT, ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID>
{
public:
    using Base = SLEBase<ViewT, ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit XChainOwnedCreateAccountClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::xChainCreateAccountClaimID(bridge, seq), view, j)
    {
    }
};

using XChainOwnedCreateAccountClaimIDEntryR = XChainOwnedCreateAccountClaimIDEntry<ReadView>;
using XChainOwnedCreateAccountClaimIDEntryW = XChainOwnedCreateAccountClaimIDEntry<ApplyView>;

}  // namespace xrpl
