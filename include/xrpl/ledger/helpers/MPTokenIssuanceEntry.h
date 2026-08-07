#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class MPTokenIssuanceEntry : public SLEBase<ViewT, ltMPTOKEN_ISSUANCE>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN_ISSUANCE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenIssuanceEntry(
        std::uint32_t seq,
        AccountID const& issuer,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(seq, issuer), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        MPTID const& issuanceID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceID), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        uint256 const& issuanceKey,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceKey), view, j)
    {
    }
};

using RMPTokenIssuanceEntry = MPTokenIssuanceEntry<ReadView>;
using WMPTokenIssuanceEntry = MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
