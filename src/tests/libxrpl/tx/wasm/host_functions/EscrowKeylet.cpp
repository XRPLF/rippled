#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct EscrowKeyletImpl : RealHostFixture
{
};

TEST_F(EscrowKeyletImpl, MatchesLedgerKeyletFunction)
{
    auto const owner = Account{"owner"};
    auto const seq = std::uint32_t{42};

    expectKeyletMatches(
        makeHost()->escrowKeylet(owner.id(), seq),
        keylet::escrow(owner.id(), SeqProxy::rawSequence(seq)));
}

TEST_F(EscrowKeyletImpl, DifferentAccountsGiveDifferentKeylets)
{
    auto h = makeHost();
    auto const a = h->escrowKeylet(Account{"alice"}.id(), 7);
    auto const b = h->escrowKeylet(Account{"becky"}.id(), 7);

    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_NE(*a, *b);
}

TEST_F(EscrowKeyletImpl, UnsetAccountIsInvalidAccount)
{
    expectError(makeHost()->escrowKeylet(AccountID{}, 1), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
