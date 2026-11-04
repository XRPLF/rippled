#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class ContractEntry : public SLEBase<ViewT, ltCONTRACT>
{
public:
    using Base = SLEBase<ViewT, ltCONTRACT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;
};

using ContractEntryR = ContractEntry<ReadView>;
using ContractEntryW = ContractEntry<ApplyView>;

}  // namespace xrpl
