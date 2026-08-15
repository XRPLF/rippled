#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>
#include <expected>
#include <iterator>

namespace xrpl::test {

struct EscrowKeyletImpl : WasmImplTest
{
};

TEST_F(EscrowKeyletImpl, MatchesLedgerKeyletFunction)
{
    auto const owner = Account{"owner"};
    auto const seq = std::uint32_t{42};

    auto const result = host().escrowKeylet(owner.id(), seq);

    ASSERT_TRUE(result.has_value());
    auto const expected = keylet::escrow(owner.id(), SeqProxy::rawSequence(seq)).key;
    auto const expectedBytes = Bytes{std::begin(expected), std::end(expected)};
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(EscrowKeyletImpl, DifferentAccountsGiveDifferentKeylets)
{
    auto const a = host().escrowKeylet(Account{"alice"}.id(), 7);
    auto const b = host().escrowKeylet(Account{"becky"}.id(), 7);

    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_NE(*a, *b);
}

TEST_F(EscrowKeyletImpl, UnsetAccountIsInvalidAccount)
{
    auto const result = host().escrowKeylet(AccountID{}, 1);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
