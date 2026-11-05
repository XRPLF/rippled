#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class ContractSourceEntry : public SLEBase<ViewT, ltCONTRACT_SOURCE>
{
public:
    using Base = SLEBase<ViewT, ltCONTRACT_SOURCE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;
};

using ContractSourceEntryR = ContractSourceEntry<ReadView>;
using ContractSourceEntryW = ContractSourceEntry<ApplyView>;

}  // namespace xrpl
