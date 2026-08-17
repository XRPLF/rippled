#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct ParentLedgerTimeImpl : WasmImplTest
{
};

TEST_F(ParentLedgerTimeImpl, MatchesLedger)
{
    expectValue(
        makeHost()->getParentLedgerTime(),
        ledger.getOpenLedger().parentCloseTime().time_since_epoch().count());
}

}  // namespace xrpl::test
