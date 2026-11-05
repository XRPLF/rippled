#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class ContractDataEntry : public SLEBase<ViewT, ltCONTRACT_DATA>
{
public:
    using Base = SLEBase<ViewT, ltCONTRACT_DATA>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;
};

using ContractDataEntryR = ContractDataEntry<ReadView>;
using ContractDataEntryW = ContractDataEntry<ApplyView>;

}  // namespace xrpl
