#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <optional>

namespace xrpl {

template <typename ViewT>
class FirewallLimitEntry : public SLEBase<ViewT, ltFIREWALL_LIMIT>
{
public:
    using Base = SLEBase<ViewT, ltFIREWALL_LIMIT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit FirewallLimitEntry(
        AccountID const& owner,
        Asset const& asset,
        std::optional<AccountID> const& limitSigner,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::firewallLimit(owner, asset, limitSigner), view, j)
    {
    }

    explicit FirewallLimitEntry(
        UInt256 const& limitID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::firewallLimit(limitID), view, j)
    {
    }
};

using FirewallLimitEntryR = FirewallLimitEntry<ReadView>;
using FirewallLimitEntryW = FirewallLimitEntry<ApplyView>;

}  // namespace xrpl
