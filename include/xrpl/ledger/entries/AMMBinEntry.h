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
class AMMBinEntry : public SLEBase<ViewT, ltAMM_BIN>
{
public:
    using Base = SLEBase<ViewT, ltAMM_BIN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMBinEntry(
        UInt256 const& ammID,
        std::int32_t binID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammBin(ammID, binID), view, j)
    {
    }

    explicit AMMBinEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammBin(key), view, j)
    {
    }
};

using AMMBinEntryR = AMMBinEntry<ReadView>;
using AMMBinEntryW = AMMBinEntry<ApplyView>;

}  // namespace xrpl
