#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct ParentLedgerHashImpl : RealHostFixture
{
};

TEST_F(ParentLedgerHashImpl, MatchesLedger)
{
    expectValue(makeHost()->getParentLedgerHash(), ledger.getOpenLedger().header().parentHash);
}

}  // namespace xrpl::test
