#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class AMMTickEntry : public SLEBase<ViewT, ltAMM_TICK>
{
public:
    using Base = SLEBase<ViewT, ltAMM_TICK>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMTickEntry(
        UInt256 const& ammID,
        std::int32_t tickIndex,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammTick(ammID, tickIndex), view, j)
    {
    }

    explicit AMMTickEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammTick(key), view, j)
    {
    }
};

using AMMTickEntryR = AMMTickEntry<ReadView>;
using AMMTickEntryW = AMMTickEntry<ApplyView>;

}  // namespace xrpl
