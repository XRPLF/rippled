#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class FirewallEntry : public SLEBase<ViewT, ltFIREWALL>
{
public:
    using Base = SLEBase<ViewT, ltFIREWALL>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit FirewallEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::firewall(account), view, j)
    {
    }

    explicit FirewallEntry(
        UInt256 const& firewallID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::firewall(firewallID), view, j)
    {
    }
};

using FirewallEntryR = FirewallEntry<ReadView>;
using FirewallEntryW = FirewallEntry<ApplyView>;

}  // namespace xrpl
