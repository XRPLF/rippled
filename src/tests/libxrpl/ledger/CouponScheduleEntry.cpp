#include <xrpl/ledger/entries/CouponScheduleEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(CouponScheduleEntryTests, constructors)
{
    EntryTestEnv e;

    MPTID const issuanceID = makeMptID(1, e.alice.id());

    expectKeylet<CouponScheduleEntry>(
        e, keylet::couponSchedule(issuanceID), "couponSchedule(MPTID)", issuanceID);

    expectKeylet<CouponScheduleEntry>(
        e, keylet::couponSchedule(e.someID()), "couponSchedule(UInt256)", e.someID());
}

}  // namespace xrpl::test
