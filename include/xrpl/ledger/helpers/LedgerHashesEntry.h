#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>

namespace xrpl {

template <typename ViewT>
class LedgerHashesEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LedgerHashesEntry(
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::skip(), view, j)
    {
    }
};

}  // namespace xrpl
