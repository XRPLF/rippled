#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class FeeSettingsEntry : public SLEBase<ViewT, ltFEE_SETTINGS>
{
public:
    using Base = SLEBase<ViewT, ltFEE_SETTINGS>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit FeeSettingsEntry(
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::feeSettings(), view, j)
    {
    }
};

using FeeSettingsEntryR = FeeSettingsEntry<ReadView>;
using FeeSettingsEntryW = FeeSettingsEntry<ApplyView>;

}  // namespace xrpl
