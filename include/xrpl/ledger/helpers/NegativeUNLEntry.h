#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class NegativeUNLEntry : public SLEBase<ViewT, ltNEGATIVE_UNL>
{
public:
    using Base = SLEBase<ViewT, ltNEGATIVE_UNL>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit NegativeUNLEntry(
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::negativeUNL(), view, j)
    {
    }
};

using NegativeUNLEntryR = NegativeUNLEntry<ReadView>;
using NegativeUNLEntryW = NegativeUNLEntry<ApplyView>;

}  // namespace xrpl
