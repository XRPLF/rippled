#include <xrpl/ledger/helpers/MPTokenIssuanceEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(MPTokenIssuanceEntryTests, Constructors)
{
    EntryTestEnv e;

    MPTID const issuanceID = makeMptID(1, e.alice.id());

    expectKeylet<MPTokenIssuanceEntry>(
        e,
        keylet::mptokenIssuance(makeMptID(1, e.alice.id())),
        "mptokenIssuance(seq, issuer)",
        std::uint32_t{1},
        e.alice.id());

    expectKeylet<MPTokenIssuanceEntry>(
        e, keylet::mptokenIssuance(issuanceID), "mptokenIssuance(MPTID)", issuanceID);

    expectKeylet<MPTokenIssuanceEntry>(
        e, keylet::mptokenIssuance(e.someID()), "mptokenIssuance(uint256)", e.someID());
}

}  // namespace xrpl::test
