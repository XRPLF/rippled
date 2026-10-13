#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <utility>

namespace xrpl::test {

struct LedgerObjFieldImpl : RealHostFixture
{
    template <typename Functor>
    void
    checkCachedField(
        Account const& acct,
        std::uint32_t index,
        SField const& field,
        TxAssembler assembler,
        Functor&& f)
    {
        auto const accountKeylet = keylet::account(acct.id());
        auto h = makeHost(accountKeylet, assembler.type, std::move(assembler.build));
        EXPECT_TRUE(h->cacheLedgerObj(accountKeylet.key, 1).has_value());
        expectValue(h->getLedgerObjField(index, field), f());
    }

    void
    checkCachedFieldError(
        Account const& acct,
        std::uint32_t index,
        SField const& field,
        TxAssembler assembler,
        HostFunctionError error)
    {
        auto const accountKeylet = keylet::account(acct.id());
        auto h = makeHost(accountKeylet, assembler.type, std::move(assembler.build));
        EXPECT_TRUE(h->cacheLedgerObj(accountKeylet.key, 1).has_value());
        expectError(h->getLedgerObjField(index, field), error);
    }
};

TEST_F(LedgerObjFieldImpl, matches_account)
{
    auto const owner = fund("owner");
    checkCachedField(
        owner, 1, sfAccount, bareTx(), [&] { return RealHostFixture::toBytes(owner.id()); });
}

TEST_F(LedgerObjFieldImpl, matches_balance)
{
    auto const owner = fund("owner");
    auto const root = ledger.getOpenLedger().read(keylet::account(owner.id()));
    checkCachedField(owner, 1, sfBalance, bareTx(), [&] {
        return RealHostFixture::toBytes(root->getFieldAmount(sfBalance));
    });
}

TEST_F(LedgerObjFieldImpl, matches_account_slot_out_of_range)
{
    auto const owner = fund("owner");
    checkCachedFieldError(owner, 0, sfAccount, bareTx(), HostFunctionError::SlotOutRange);
    checkCachedFieldError(owner, 257, sfAccount, bareTx(), HostFunctionError::SlotOutRange);
}

TEST_F(LedgerObjFieldImpl, matches_account_empty_slot)
{
    auto const owner = fund("owner");
    checkCachedFieldError(owner, 2, sfAccount, bareTx(), HostFunctionError::EmptySlot);
}

TEST_F(LedgerObjFieldImpl, matches_owner_field_not_found)
{
    auto const owner = fund("owner");
    checkCachedFieldError(owner, 1, sfOwner, bareTx(), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
