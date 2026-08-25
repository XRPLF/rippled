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
class DelegateEntry : public SLEBase<ViewT, ltDELEGATE>
{
public:
    using Base = SLEBase<ViewT, ltDELEGATE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DelegateEntry(
        AccountID const& account,
        AccountID const& authorizedAccount,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::delegate(account, authorizedAccount), view, j)
    {
    }
};

using DelegateEntryR = DelegateEntry<ReadView>;
using DelegateEntryW = DelegateEntry<ApplyView>;

}  // namespace xrpl
