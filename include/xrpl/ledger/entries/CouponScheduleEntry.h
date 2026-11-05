#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class CouponScheduleEntry : public SLEBase<ViewT, ltCOUPON_SCHEDULE>
{
public:
    using Base = SLEBase<ViewT, ltCOUPON_SCHEDULE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit CouponScheduleEntry(
        UInt192 const& mptIssuanceID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::couponSchedule(mptIssuanceID), view, j)
    {
    }

    explicit CouponScheduleEntry(
        UInt256 const& scheduleKey,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::couponSchedule(scheduleKey), view, j)
    {
    }
};

using CouponScheduleEntryR = CouponScheduleEntry<ReadView>;
using CouponScheduleEntryW = CouponScheduleEntry<ApplyView>;

}  // namespace xrpl
