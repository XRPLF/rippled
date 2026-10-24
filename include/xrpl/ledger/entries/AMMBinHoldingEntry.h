#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class AMMBinHoldingEntry : public SLEBase<ViewT, ltAMM_BIN_HOLDING>
{
public:
    using Base = SLEBase<ViewT, ltAMM_BIN_HOLDING>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMBinHoldingEntry(
        UInt256 const& ammID,
        AccountID const& owner,
        std::int32_t binID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammBinHolding(ammID, owner, binID), view, j)
    {
    }

    explicit AMMBinHoldingEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammBinHolding(key), view, j)
    {
    }
};

using AMMBinHoldingEntryR = AMMBinHoldingEntry<ReadView>;
using AMMBinHoldingEntryW = AMMBinHoldingEntry<ApplyView>;

}  // namespace xrpl
