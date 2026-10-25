#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

// set_data_nested_object_field and its reader — one level of nesting inside an account's
// data object.
//
// The two string arguments are the outer key then the inner one, in that order on the wire.
// A contract that swaps them writes somewhere else entirely, which is why the order is
// asserted rather than assumed.
struct SetDataNestedObjectFieldImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeTracingContractHost({contract.id(), alice.id()});
    }
};

TEST_F(SetDataNestedObjectFieldImpl, WhatIsStoredIsWhatIsReadBack)
{
    auto const contractHost = host();

    expectValue(contractHost->setDataNestedObjectField(alice.id(), "stats", "score", u32(9999)), 0);

    expectValue(
        contractHost->getDataNestedObjectField(alice.id(), "stats", "score"),
        serialization(u32(9999)));
}

// The outer key comes first. Read under the swapped pair and there is nothing there, which
// is what a contract that got the order wrong would see.
TEST_F(SetDataNestedObjectFieldImpl, TheOuterKeyIsTheFirstOne)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataNestedObjectField(alice.id(), "stats", "score", u32(1)));

    expectError(
        contractHost->getDataNestedObjectField(alice.id(), "score", "stats"),
        HostFunctionError::InvalidField);
}

TEST_F(SetDataNestedObjectFieldImpl, OneObjectHoldsSeveralKeys)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataNestedObjectField(alice.id(), "stats", "score", u32(1)));
    ASSERT_TRUE(contractHost->setDataNestedObjectField(alice.id(), "stats", "level", u32(2)));

    expectValue(
        contractHost->getDataNestedObjectField(alice.id(), "stats", "score"),
        serialization(u32(1)));
    expectValue(
        contractHost->getDataNestedObjectField(alice.id(), "stats", "level"),
        serialization(u32(2)));
}

TEST_F(SetDataNestedObjectFieldImpl, WhatIsFinalizedIsVisibleToTheNextCall)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(
            contractHost->setDataNestedObjectField(alice.id(), "stats", "score", u32(9999)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    expectValue(
        host()->getDataNestedObjectField(alice.id(), "stats", "score"), serialization(u32(9999)));
}

TEST_F(SetDataNestedObjectFieldImpl, AnArrayIsNotAnObject)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "first", u8(1)));

    expectError(
        contractHost->setDataNestedObjectField(alice.id(), "stats", "score", u32(1)),
        HostFunctionError::InvalidState);
    EXPECT_NE(
        logged().find("setDataNestedObjectField: Invalid state: not an object"), std::string::npos);
}

TEST_F(SetDataNestedObjectFieldImpl, AnAccountThatDoesNotExistCannotBeWrittenTo)
{
    expectError(
        host()->setDataNestedObjectField(Account{"ghost"}.id(), "stats", "score", u32(1)),
        HostFunctionError::InvalidAccount);
}

// An owner with no spare reserve cannot be written to, and the trace says so.
TEST_F(SetDataNestedObjectFieldImpl, an_owner_with_no_spare_reserve_is_refused)
{
    auto const poor = fund("poor", XRP(10));
    expectError(
        host()->setDataNestedObjectField(poor.id(), "a", "b", u8(1)),
        HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataNestedObjectField: Failed to set nested"), std::string::npos);
}

}  // namespace xrpl::test
