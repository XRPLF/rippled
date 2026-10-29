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
class AMMPositionEntry : public SLEBase<ViewT, ltAMM_POSITION>
{
public:
    using Base = SLEBase<ViewT, ltAMM_POSITION>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMPositionEntry(
        UInt256 const& ammID,
        AccountID const& owner,
        std::uint32_t seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammPosition(ammID, owner, seq), view, j)
    {
    }

    explicit AMMPositionEntry(
        UInt256 const& key,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ammPosition(key), view, j)
    {
    }
};

using AMMPositionEntryR = AMMPositionEntry<ReadView>;
using AMMPositionEntryW = AMMPositionEntry<ApplyView>;

}  // namespace xrpl
