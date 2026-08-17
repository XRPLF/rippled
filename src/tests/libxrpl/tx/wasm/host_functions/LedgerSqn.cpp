#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct LedgerSqnImpl : WasmImplTest
{
};

TEST_F(LedgerSqnImpl, MatchesLedger)
{
    expectValue(makeHost()->getLedgerSqn(), ledger.getOpenLedger().header().seq);
}

}  // namespace xrpl::test
