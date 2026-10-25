#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstddef>

namespace xrpl::test {

// set_data_array_element_field and its reader — an account's data object *as an array*.
//
// An account has one data object, and it is either a map of keys or a list of elements. The
// first array write decides which, and there is no going back inside a run.
struct SetDataArrayElementFieldImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeTracingContractHost({contract.id(), alice.id()});
    }
};

TEST_F(SetDataArrayElementFieldImpl, EachElementKeepsItsOwnValue)
{
    auto const contractHost = host();

    expectValue(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)), 0);
    expectValue(contractHost->setDataArrayElementField(alice.id(), 1, "amount", u32(20)), 0);

    expectValue(
        contractHost->getDataArrayElementField(alice.id(), 0, "amount"), serialization(u32(10)));
    expectValue(
        contractHost->getDataArrayElementField(alice.id(), 1, "amount"), serialization(u32(20)));
}

TEST_F(SetDataArrayElementFieldImpl, OneElementHoldsSeveralKeys)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)));
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "kind", u8(2)));

    expectValue(
        contractHost->getDataArrayElementField(alice.id(), 0, "amount"), serialization(u32(10)));
    expectValue(
        contractHost->getDataArrayElementField(alice.id(), 0, "kind"), serialization(u8(2)));
}

TEST_F(SetDataArrayElementFieldImpl, WhatIsFinalizedIsVisibleToTheNextCall)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    expectValue(host()->getDataArrayElementField(alice.id(), 0, "amount"), serialization(u32(10)));
}

// An object is not an array. A contract that has written a key cannot then write an element,
// and is told so rather than having its object replaced by a list.
TEST_F(SetDataArrayElementFieldImpl, AnObjectIsNotAnArray)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(1)));

    expectError(
        contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)),
        HostFunctionError::InvalidState);
    EXPECT_NE(
        logged().find("setDataArrayElementField: Invalid state: not an array"), std::string::npos);
}

TEST_F(SetDataArrayElementFieldImpl, AnElementThatWasNeverWrittenIsNotThere)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)));

    expectError(
        contractHost->getDataArrayElementField(alice.id(), 1, "amount"),
        HostFunctionError::InvalidField);
}

TEST_F(SetDataArrayElementFieldImpl, AKeyThatIsNotInTheElementIsNotAField)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)));

    expectError(
        contractHost->getDataArrayElementField(alice.id(), 0, "absent"),
        HostFunctionError::InvalidField);
}

TEST_F(SetDataArrayElementFieldImpl, AnAccountThatDoesNotExistCannotBeWrittenTo)
{
    expectError(
        host()->setDataArrayElementField(Account{"ghost"}.id(), 0, "amount", u32(1)),
        HostFunctionError::InvalidAccount);
}

// An owner with no spare reserve cannot be written to, and the trace says so.
TEST_F(SetDataArrayElementFieldImpl, an_owner_with_no_spare_reserve_is_refused)
{
    auto const poor = fund("poor", XRP(10));
    expectError(
        host()->setDataArrayElementField(poor.id(), 0, "a", u8(1)),
        HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataArrayElementField: Failed to set array"), std::string::npos);
}

// An element past what a data object of the size limit can hold is refused before the
// array grows to reach it, and the data is left as it was.
TEST_F(SetDataArrayElementFieldImpl, an_index_no_data_object_can_hold_is_refused)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "amount", u32(10)));

    expectError(
        contractHost->setDataArrayElementField(
            alice.id(),
            static_cast<std::size_t>(xrpl::contract::maxContractDataSize),
            "amount",
            u32(1)),
        HostFunctionError::IndexOutOfBounds);
    expectError(
        contractHost->setDataArrayElementField(alice.id(), 0x7FFF'FFFF, "amount", u32(1)),
        HostFunctionError::IndexOutOfBounds);

    expectValue(
        contractHost->getDataArrayElementField(alice.id(), 0, "amount"), serialization(u32(10)));
    expectError(
        contractHost->getDataArrayElementField(alice.id(), 1, "amount"),
        HostFunctionError::InvalidField);
}

}  // namespace xrpl::test
