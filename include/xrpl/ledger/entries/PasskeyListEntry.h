#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class PasskeyListEntry : public SLEBase<ViewT, ltPASSKEY_LIST>
{
public:
    using Base = SLEBase<ViewT, ltPASSKEY_LIST>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PasskeyListEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::passkeyList(account), view, j)
    {
    }
};

using PasskeyListEntryR = PasskeyListEntry<ReadView>;
using PasskeyListEntryW = PasskeyListEntry<ApplyView>;

}  // namespace xrpl
