#include <xrpl/protocol/STJson.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

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
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
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

}  // namespace xrpl::test
