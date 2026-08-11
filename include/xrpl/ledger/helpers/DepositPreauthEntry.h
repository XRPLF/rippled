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
class DepositPreauthEntry : public SLEBase<ViewT, ltDEPOSIT_PREAUTH>
{
public:
    using Base = SLEBase<ViewT, ltDEPOSIT_PREAUTH>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DepositPreauthEntry(
        AccountID const& owner,
        AccountID const& preauthorized,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::depositPreauth(owner, preauthorized), view, j)
    {
    }

    explicit DepositPreauthEntry(
        AccountID const& owner,
        std::set<std::pair<AccountID, Slice>> const& authCreds,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::depositPreauth(owner, authCreds), view, j)
    {
    }

    explicit DepositPreauthEntry(
        uint256 const& preauthID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::depositPreauth(preauthID), view, j)
    {
    }
};

using RDepositPreauthEntry = DepositPreauthEntry<ReadView>;
using WDepositPreauthEntry = DepositPreauthEntry<ApplyView>;

}  // namespace xrpl
