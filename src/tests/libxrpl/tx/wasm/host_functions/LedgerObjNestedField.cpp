#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/OwnedLocator.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <utility>

namespace xrpl::test {

struct LedgerObjNestedFieldImpl : RealHostFixture
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

TEST_F(LedgerObjNestedFieldImpl, matches_nested_signer_accounts_by_index)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);

    auto const sle = ledger.getOpenLedger().read(keylet::signerList(owner.id()));
    ASSERT_NE(sle, nullptr);
    auto const& entries = sle->getFieldArray(sfSignerEntries);

    expectValue(
        h->getLedgerObjNestedField(1, locator({sfSignerEntries.getCode(), 0, sfAccount.getCode()})),
        RealHostFixture::toBytes(entries[0].getAccountID(sfAccount)));
    expectValue(
        h->getLedgerObjNestedField(1, locator({sfSignerEntries.getCode(), 1, sfAccount.getCode()})),
        RealHostFixture::toBytes(entries[1].getAccountID(sfAccount)));
    EXPECT_NE(entries[0].getAccountID(sfAccount), entries[1].getAccountID(sfAccount));
}

TEST_F(LedgerObjNestedFieldImpl, matches_nested_signer_weight)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getLedgerObjNestedField(
            1, locator({sfSignerEntries.getCode(), 0, sfSignerWeight.getCode()})),
        RealHostFixture::toBytes(static_cast<std::uint16_t>(1)));
}

TEST_F(LedgerObjNestedFieldImpl, matches_base_signer_quorum)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getLedgerObjNestedField(1, locator({sfSignerQuorum.getCode()})),
        RealHostFixture::toBytes(static_cast<std::uint32_t>(2)));
}

TEST_F(LedgerObjNestedFieldImpl, missing_field_not_found)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::FieldNotFound;

    expectError(
        h->getLedgerObjNestedField(1, locator({sfSigners.getCode(), 0, sfAccount.getCode()})), err);
    expectError(
        h->getLedgerObjNestedField(
            1, locator({sfSignerEntries.getCode(), 0, sfDestination.getCode()})),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, index_out_of_bounds)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::IndexOutOfBounds;

    expectError(
        h->getLedgerObjNestedField(1, locator({sfSignerEntries.getCode(), 2, sfAccount.getCode()})),
        err);
    expectError(
        h->getLedgerObjNestedField(
            1, locator({sfSignerEntries.getCode(), -1, sfAccount.getCode()})),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, unknown_field_code_invalid_field)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::InvalidField;

    expectError(
        h->getLedgerObjNestedField(1, locator({fieldCode(99999, 99999), 0, sfAccount.getCode()})),
        err);
    expectError(
        h->getLedgerObjNestedField(
            1, locator({sfSignerEntries.getCode(), 0, fieldCode(99999, 99999)})),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, slot_errors)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);

    // 0 and 257 are outside the 1..256 slot range.
    expectError(
        h->getLedgerObjNestedField(0, locator({sfSignerQuorum.getCode()})),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedField(257, locator({sfSignerQuorum.getCode()})),
        HostFunctionError::SlotOutRange);
    // Slot 2 is in range but nothing was cached there.
    expectError(
        h->getLedgerObjNestedField(2, locator({sfSignerQuorum.getCode()})),
        HostFunctionError::EmptySlot);
}

TEST_F(LedgerObjNestedFieldImpl, container_without_index_not_leaf)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedField(1, locator({sfSignerEntries.getCode()})),
        HostFunctionError::NotLeafField);
}

TEST_F(LedgerObjNestedFieldImpl, nest_into_non_container_malformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedField(1, locator({sfSignerQuorum.getCode(), 0, sfAccount.getCode()})),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
