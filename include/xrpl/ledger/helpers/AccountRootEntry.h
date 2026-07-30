#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AccountRootEntry(
        AccountID const& id,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::account(id), view, j)
    {
    }
};

using RAccountRootEntry = AccountRootEntry<ReadView>;
using WAccountRootEntry = AccountRootEntry<ApplyView>;

}  // namespace xrpl
