#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT, ltACCOUNT_ROOT>
{
public:
    using Base = SLEBase<ViewT, ltACCOUNT_ROOT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AccountRootEntry(
        AccountID const& id,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::account(id), view, j)
    {
    }
};

using RAccountRootEntry = AccountRootEntry<ReadView>;
using WAccountRootEntry = AccountRootEntry<ApplyView>;

}  // namespace xrpl
