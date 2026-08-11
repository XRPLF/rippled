#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class MPTokenEntry : public SLEBase<ViewT, ltMPTOKEN>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenEntry(
        MPTID const& issuanceID,
        AccountID const& holder,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceID, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        uint256 const& issuanceKey,
        AccountID const& holder,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceKey, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        uint256 const& mptokenKey,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(mptokenKey), view, j)
    {
    }
};

using RMPTokenEntry = MPTokenEntry<ReadView>;
using WMPTokenEntry = MPTokenEntry<ApplyView>;

}  // namespace xrpl
