#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class WithdrawPreauthEntry : public SLEBase<ViewT, ltWITHDRAW_PREAUTH>
{
public:
    using Base = SLEBase<ViewT, ltWITHDRAW_PREAUTH>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit WithdrawPreauthEntry(
        AccountID const& owner,
        AccountID const& preauthorized,
        std::uint32_t dtag,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::withdrawPreauth(owner, preauthorized, dtag), view, j)
    {
    }

    explicit WithdrawPreauthEntry(
        UInt256 const& preauthID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::withdrawPreauth(preauthID), view, j)
    {
    }
};

using WithdrawPreauthEntryR = WithdrawPreauthEntry<ReadView>;
using WithdrawPreauthEntryW = WithdrawPreauthEntry<ApplyView>;

}  // namespace xrpl
