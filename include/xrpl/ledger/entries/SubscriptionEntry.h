#pragma once

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
class SubscriptionEntry : public SLEBase<ViewT, ltSUBSCRIPTION>
{
public:
    using Base = SLEBase<ViewT, ltSUBSCRIPTION>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit SubscriptionEntry(
        AccountID const& account,
        AccountID const& dest,
        std::uint32_t seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::subscription(account, dest, seq), view, j)
    {
    }
};

using SubscriptionEntryR = SubscriptionEntry<ReadView>;
using SubscriptionEntryW = SubscriptionEntry<ApplyView>;

}  // namespace xrpl
