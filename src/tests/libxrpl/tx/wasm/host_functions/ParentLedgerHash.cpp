#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct ParentLedgerHashImpl : RealHostFixture
{
};

TEST_F(ParentLedgerHashImpl, matches_ledger)
{
    expectValue(makeHost()->getParentLedgerHash(), ledger.getOpenLedger().header().parentHash);
}

}  // namespace xrpl::test
