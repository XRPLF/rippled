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
class EscrowEntry : public SLEBase<ViewT, ltESCROW>
{
public:
    using Base = SLEBase<ViewT, ltESCROW>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit EscrowEntry(
        AccountID const& src,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::escrow(src, seq), view, j)
    {
    }
};

using EscrowEntryR = EscrowEntry<ReadView>;
using EscrowEntryW = EscrowEntry<ApplyView>;

}  // namespace xrpl
