#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <set>
#include <utility>

namespace xrpl {

template <typename ViewT>
class DepositPreauthEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DepositPreauthEntry(
        AccountID const& owner,
        AccountID const& preauthorized,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(owner, preauthorized), view, j)
    {
    }

    explicit DepositPreauthEntry(
        AccountID const& owner,
        std::set<std::pair<AccountID, Slice>> const& authCreds,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(owner, authCreds), view, j)
    {
    }

    explicit DepositPreauthEntry(
        uint256 const& preauthID,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(preauthID), view, j)
    {
    }
};

}  // namespace xrpl
