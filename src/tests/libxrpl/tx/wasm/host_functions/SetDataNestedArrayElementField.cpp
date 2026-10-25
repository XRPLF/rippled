#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

// set_data_nested_array_element_field and its reader — an array held under a key of an
// account's data object, rather than the object itself being one.
//
// The arguments are the outer key, the element index, and the key inside that element: the
// index sits between the two strings on the wire, which is the shape nothing else has.
struct SetDataNestedArrayElementFieldImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeTracingContractHost({contract.id(), alice.id()});
    }
};

TEST_F(SetDataNestedArrayElementFieldImpl, EachElementKeepsItsOwnValue)
{
    auto const contractHost = host();

    expectValue(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)), 0);
    expectValue(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 1, "id", u32(77)), 0);

    expectValue(
        contractHost->getDataNestedArrayElementField(alice.id(), "items", 0, "id"),
        serialization(u32(55)));
    expectValue(
        contractHost->getDataNestedArrayElementField(alice.id(), "items", 1, "id"),
        serialization(u32(77)));
}

TEST_F(SetDataNestedArrayElementFieldImpl, OneElementHoldsSeveralKeys)
{
    auto const contractHost = host();
    ASSERT_TRUE(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)));
    ASSERT_TRUE(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "price", u32(12)));

    expectValue(
        contractHost->getDataNestedArrayElementField(alice.id(), "items", 0, "price"),
        serialization(u32(12)));
}

// Two arrays under two keys are two arrays: an element of one is not an element of the
// other, which is what having the outer key on the wire is for.
TEST_F(SetDataNestedArrayElementFieldImpl, ArraysUnderDifferentKeysAreSeparate)
{
    auto const contractHost = host();
    ASSERT_TRUE(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)));
    ASSERT_TRUE(
        contractHost->setDataNestedArrayElementField(alice.id(), "orders", 0, "id", u32(77)));

    expectValue(
        contractHost->getDataNestedArrayElementField(alice.id(), "items", 0, "id"),
        serialization(u32(55)));
    expectValue(
        contractHost->getDataNestedArrayElementField(alice.id(), "orders", 0, "id"),
        serialization(u32(77)));
}

TEST_F(SetDataNestedArrayElementFieldImpl, WhatIsFinalizedIsVisibleToTheNextCall)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(
            contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    expectValue(
        host()->getDataNestedArrayElementField(alice.id(), "items", 0, "id"),
        serialization(u32(55)));
}

// The data object holds the array, so the object itself may not be one.
TEST_F(SetDataNestedArrayElementFieldImpl, AnArrayRootHasNoKeyToHoldAnArrayUnder)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "first", u8(1)));

    expectError(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)),
        HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataNestedArrayElementField: Invalid state"), std::string::npos);
}

TEST_F(SetDataNestedArrayElementFieldImpl, AnElementThatWasNeverWrittenIsNotThere)
{
    auto const contractHost = host();
    ASSERT_TRUE(
        contractHost->setDataNestedArrayElementField(alice.id(), "items", 0, "id", u32(55)));

    expectError(
        contractHost->getDataNestedArrayElementField(alice.id(), "items", 1, "id"),
        HostFunctionError::InvalidField);
}

TEST_F(SetDataNestedArrayElementFieldImpl, AnAccountThatDoesNotExistCannotBeWrittenTo)
{
    expectError(
        host()->setDataNestedArrayElementField(Account{"ghost"}.id(), "items", 0, "id", u32(1)),
        HostFunctionError::InvalidAccount);
}

// An owner with no spare reserve cannot be written to, and the trace says so.
TEST_F(SetDataNestedArrayElementFieldImpl, an_owner_with_no_spare_reserve_is_refused)
{
    auto const poor = fund("poor", XRP(10));
    expectError(
        host()->setDataNestedArrayElementField(poor.id(), "a", 0, "b", u8(1)),
        HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataNestedArrayElementField: Failed to set"), std::string::npos);
}

}  // namespace xrpl::test
