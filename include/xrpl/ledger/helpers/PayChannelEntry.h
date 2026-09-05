#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

template <typename ViewT>
class PayChannelEntry : public SLEBase<ViewT, ltPAYCHAN>
{
public:
    using Base = SLEBase<ViewT, ltPAYCHAN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PayChannelEntry(
        AccountID const& src,
        AccountID const& dst,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::payChannel(src, dst, seq), view, j)
    {
    }
};

using PayChannelEntryR = PayChannelEntry<ReadView>;
using PayChannelEntryW = PayChannelEntry<ApplyView>;

}  // namespace xrpl
