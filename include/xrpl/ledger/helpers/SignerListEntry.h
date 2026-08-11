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
class SignerListEntry : public SLEBase<ViewT, ltSIGNER_LIST>
{
public:
    using Base = SLEBase<ViewT, ltSIGNER_LIST>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit SignerListEntry(
        AccountID const& account,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::signerList(account), view, j)
    {
    }
};

using RSignerListEntry = SignerListEntry<ReadView>;
using WSignerListEntry = SignerListEntry<ApplyView>;

}  // namespace xrpl
