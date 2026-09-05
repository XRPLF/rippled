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
class DIDEntry : public SLEBase<ViewT, ltDID>
{
public:
    using Base = SLEBase<ViewT, ltDID>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DIDEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::did(account), view, j)
    {
    }
};

using DIDEntryR = DIDEntry<ReadView>;
using DIDEntryW = DIDEntry<ApplyView>;

}  // namespace xrpl
