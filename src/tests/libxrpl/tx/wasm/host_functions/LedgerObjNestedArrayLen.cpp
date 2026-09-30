#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/OwnedLocator.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <utility>

namespace xrpl::test {

struct LedgerObjNestedArrayLenImpl : RealHostFixture
{
    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        makeSignerList(acct, 2, {{Account{"alice"}, 1}, {Account{"becky"}, 1}});
        auto assembler = bareTx();
        auto h = makeHost(keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
        EXPECT_TRUE(h->cacheLedgerObj(keylet::signerList(acct.id()).key, 1).has_value());
        return h;
    }
};

TEST_F(LedgerObjNestedArrayLenImpl, signer_entries_length)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getLedgerObjNestedArrayLen(1, locator({sfSignerEntries.getCode()})), 2);
}

TEST_F(LedgerObjNestedArrayLenImpl, non_array_field_no_array)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(1, locator({sfSignerQuorum.getCode()})),
        HostFunctionError::NoArray);
}

TEST_F(LedgerObjNestedArrayLenImpl, missing_field_not_found)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(1, locator({sfSigners.getCode()})),
        HostFunctionError::FieldNotFound);
}

TEST_F(LedgerObjNestedArrayLenImpl, slot_errors)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(0, locator({sfSignerEntries.getCode()})),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedArrayLen(257, locator({sfSignerEntries.getCode()})),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedArrayLen(2, locator({sfSignerEntries.getCode()})),
        HostFunctionError::EmptySlot);
}

TEST_F(LedgerObjNestedArrayLenImpl, nest_into_non_container_malformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(
            1, locator({sfSignerQuorum.getCode(), 0, sfAccount.getCode()})),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
