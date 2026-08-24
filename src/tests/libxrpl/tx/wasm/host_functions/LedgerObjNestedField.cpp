#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

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

TEST_F(LedgerObjNestedFieldImpl, MatchesNestedSignerAccountsByIndex)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);

    auto const sle = ledger.getOpenLedger().read(keylet::signerList(owner.id()));
    ASSERT_NE(sle, nullptr);
    auto const& entries = sle->getFieldArray(sfSignerEntries);

    expectValue(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 0, sfAccount.getCode()}}),
        toBytes(entries[0].getAccountID(sfAccount)));
    expectValue(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 1, sfAccount.getCode()}}),
        toBytes(entries[1].getAccountID(sfAccount)));
    EXPECT_NE(entries[0].getAccountID(sfAccount), entries[1].getAccountID(sfAccount));
}

TEST_F(LedgerObjNestedFieldImpl, MatchesNestedSignerWeight)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 0, sfSignerWeight.getCode()}}),
        toBytes(static_cast<std::uint16_t>(1)));
}

TEST_F(LedgerObjNestedFieldImpl, MatchesBaseSignerQuorum)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getLedgerObjNestedField(1, FieldLocator{{sfSignerQuorum.getCode()}}),
        toBytes(static_cast<std::uint32_t>(2)));
}

TEST_F(LedgerObjNestedFieldImpl, MissingFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::FieldNotFound;

    expectError(
        h->getLedgerObjNestedField(1, FieldLocator{{sfSigners.getCode(), 0, sfAccount.getCode()}}),
        err);
    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 0, sfDestination.getCode()}}),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, IndexOutOfBounds)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::IndexOutOfBounds;

    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 2, sfAccount.getCode()}}),
        err);
    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), -1, sfAccount.getCode()}}),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, UnknownFieldCodeInvalidField)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::InvalidField;

    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{fieldCode(99999, 99999), 0, sfAccount.getCode()}}),
        err);
    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerEntries.getCode(), 0, fieldCode(99999, 99999)}}),
        err);
}

TEST_F(LedgerObjNestedFieldImpl, SlotErrors)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);

    // 0 and 257 are outside the 1..256 slot range.
    expectError(
        h->getLedgerObjNestedField(0, FieldLocator{{sfSignerQuorum.getCode()}}),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedField(257, FieldLocator{{sfSignerQuorum.getCode()}}),
        HostFunctionError::SlotOutRange);
    // Slot 2 is in range but nothing was cached there.
    expectError(
        h->getLedgerObjNestedField(2, FieldLocator{{sfSignerQuorum.getCode()}}),
        HostFunctionError::EmptySlot);
}

TEST_F(LedgerObjNestedFieldImpl, ContainerWithoutIndexNotLeaf)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedField(1, FieldLocator{{sfSignerEntries.getCode()}}),
        HostFunctionError::NotLeafField);
}

TEST_F(LedgerObjNestedFieldImpl, NestIntoNonContainerMalformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedField(
            1, FieldLocator{{sfSignerQuorum.getCode(), 0, sfAccount.getCode()}}),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
