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
class BeneficiaryEntry : public SLEBase<ViewT, ltBENEFICIARY>
{
public:
    using Base = SLEBase<ViewT, ltBENEFICIARY>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit BeneficiaryEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::beneficiary(account), view, j)
    {
    }
};

using BeneficiaryEntryR = BeneficiaryEntry<ReadView>;
using BeneficiaryEntryW = BeneficiaryEntry<ApplyView>;

}  // namespace xrpl
