#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

template <typename ViewT>
class RippleStateEntry : public SLEBase<ViewT, ltRIPPLE_STATE>
{
public:
    using Base = SLEBase<ViewT, ltRIPPLE_STATE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit RippleStateEntry(
        AccountID const& id0,
        AccountID const& id1,
        Currency const& currency,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::trustLine(id0, id1, currency), view, j)
    {
    }

    explicit RippleStateEntry(
        AccountID const& id,
        Issue const& issue,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::trustLine(id, issue), view, j)
    {
    }
};

using RRippleStateEntry = RippleStateEntry<ReadView>;
using WRippleStateEntry = RippleStateEntry<ApplyView>;

}  // namespace xrpl
