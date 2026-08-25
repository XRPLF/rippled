#pragma once

#include <xrpl/basics/base_uint.h>
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
class CheckEntry : public SLEBase<ViewT, ltCHECK>
{
public:
    using Base = SLEBase<ViewT, ltCHECK>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit CheckEntry(
        AccountID const& id,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::check(id, seq), view, j)
    {
    }

    explicit CheckEntry(
        uint256 const& checkID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::check(checkID), view, j)
    {
    }
};

using CheckEntryR = CheckEntry<ReadView>;
using CheckEntryW = CheckEntry<ApplyView>;

}  // namespace xrpl
