#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

#include <map>
#include <set>

namespace xrpl {

template <typename ViewT>
class AmendmentsEntry : public SLEBase<ViewT, ltAMENDMENTS>
{
public:
    using Base = SLEBase<ViewT, ltAMENDMENTS>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AmendmentsEntry(
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amendments(), view, j)
    {
    }

    /**
     * Returns the set of amendments this entry reports as enabled.
     *
     * @return the set of enabled amendments.
     */
    [[nodiscard]] std::set<UInt256>
    enabledAmendments() const
    {
        std::set<UInt256> amendments;

        if (this->exists() && (*this)->isFieldPresent(sfAmendments))
        {
            auto const& v = (*this)->getFieldV256(sfAmendments);
            amendments.insert_range(v);
        }

        return amendments;
    }

    /**
     * Returns a map of amendments that have achieved majority, to the time
     * majority was reached.
     *
     * @return a map of amendment to the time majority was reached.
     */
    [[nodiscard]] std::map<UInt256, NetClock::TimePoint>
    majorityAmendments() const
    {
        std::map<UInt256, NetClock::TimePoint> ret;

        if (this->exists() && (*this)->isFieldPresent(sfMajorities))
        {
            using TimePoint = NetClock::TimePoint;
            using Duration = TimePoint::duration;

            auto const majorities = (*this)->getFieldArray(sfMajorities);

            for (auto const& m : majorities)
            {
                ret[m.getFieldH256(sfAmendment)] = TimePoint(Duration(m.getFieldU32(sfCloseTime)));
            }
        }

        return ret;
    }
};

using AmendmentsEntryR = AmendmentsEntry<ReadView>;
using AmendmentsEntryW = AmendmentsEntry<ApplyView>;

}  // namespace xrpl
