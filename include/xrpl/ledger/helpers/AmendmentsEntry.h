#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class AmendmentsEntry : public SLEBase<ViewT, ltAMENDMENTS>
{
public:
    using Base = SLEBase<ViewT, ltAMENDMENTS>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AmendmentsEntry(
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amendments(), view, j)
    {
    }
};

using RAmendmentsEntry = AmendmentsEntry<ReadView>;
using WAmendmentsEntry = AmendmentsEntry<ApplyView>;

}  // namespace xrpl
