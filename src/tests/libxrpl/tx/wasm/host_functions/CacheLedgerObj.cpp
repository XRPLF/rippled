#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct CacheLedgerObjImpl : WasmImplTest
{
    void
    runMatchesLedger(bool implicit)
    {
        auto const owner = Account{"owner"};
        ledger.createAccount(owner, XRP(1000));

        auto h = makeHost();
        auto const key = keylet::account(owner.id()).key;

        for (auto i = int32_t{1}; i < 257; ++i)
        {
            auto const slot = h->cacheLedgerObj(key, implicit ? 0 : i);
            ASSERT_TRUE(slot.has_value()) << "cacheLedgerObj should find the created account";
            EXPECT_EQ(*slot, i);

            auto const account = h->getLedgerObjField(*slot, sfAccount);
            ASSERT_TRUE(account.has_value());
            Bytes const ownerBytes{owner.id().begin(), owner.id().end()};
            EXPECT_EQ(*account, ownerBytes);

            auto const sle = ledger.getOpenLedger().read(keylet::account(owner.id()));
            ASSERT_NE(sle, nullptr);
            auto const& ledgerAccount = sle->getAccountID(sfAccount);
            EXPECT_EQ(*account, (Bytes{ledgerAccount.begin(), ledgerAccount.end()}));
        }

        // Every slot is now occupied, so asking to auto-allocate (cacheIdx == 0) has nowhere
        // to put the object.
        auto const result = h->cacheLedgerObj(key, 0);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), HostFunctionError::SlotsFull);
    }
};

TEST_F(CacheLedgerObjImpl, MatchesLedgerExplicitIndices)
{
    runMatchesLedger(false);
}

TEST_F(CacheLedgerObjImpl, MatchesLedgerImplicitIndices)
{
    runMatchesLedger(true);
}

TEST_F(CacheLedgerObjImpl, OutOfRange)
{
    auto h = makeHost();
    auto result = h->cacheLedgerObj(uint256{}, -1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::SlotOutRange);

    result = h->cacheLedgerObj(uint256{}, 257);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::SlotOutRange);
}

TEST_F(CacheLedgerObjImpl, LedgerObjNotFound)
{
    auto const ghost = keylet::account(Account{"ghost"}.id()).key;
    auto result = makeHost()->cacheLedgerObj(ghost, 0);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::LedgerObjNotFound);
}

// Two hosts built from the same fixture are fully independent: each owns its own slot
// table, so caching into one leaves the other's slots empty. (This is what the `WasmHost`
// handle buys over the old shared-fixture-state design.)
TEST_F(CacheLedgerObjImpl, IndependentHostsDoNotShareSlots)
{
    auto const owner = fund("owner");
    auto const key = keylet::account(owner.id()).key;

    auto a = makeHost();
    auto b = makeHost();

    ASSERT_TRUE(a->cacheLedgerObj(key, 1).has_value());
    expectValue(a->getLedgerObjField(1, sfAccount), toBytes(owner.id()));
    // `b` never cached anything, so its slot 1 is still empty.
    expectError(b->getLedgerObjField(1, sfAccount), HostFunctionError::EmptySlot);
}

}  // namespace xrpl::test
