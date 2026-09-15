#include <xrpl/protocol/STJson.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <string>

namespace xrpl::test {

// set_data_object_field and get_data_object_field against a real ledger: what the contract
// stores, what it reads back, and who pays for it.
struct SetDataObjectFieldImpl : ContractHostFixture
{
    // The account whose data object the contract writes. It pays the object's reserve, so it
    // has to exist and have something spare.
    Account const alice = fund("alice");

    // The contract's own account. A data test never makes it pay anything.
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }
};

TEST_F(SetDataObjectFieldImpl, WhatIsStoredIsWhatIsReadBack)
{
    auto const contractHost = host();

    expectValue(contractHost->setDataObjectField(alice.id(), "value_u8", u8(42)), 0);

    // The answer is the value's serialization without its type byte: the contract knows the
    // type it asked for, so the wire does not repeat it.
    expectValue(contractHost->getDataObjectField(alice.id(), "value_u8"), serialization(u8(42)));
}

// The cache is what makes a contract's own writes visible to its own reads: nothing has
// reached the ledger at this point, and the data object may not even exist yet.
TEST_F(SetDataObjectFieldImpl, AWriteIsVisibleToALaterReadInTheSameRun)
{
    auto const contractHost = host();

    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(3)));

    auto const& [modified, data] = contractHost.context().result.dataMap.at(alice.id());
    EXPECT_TRUE(modified) << "a written object is marked for the ledger";
    auto const expected = STJson{STJson::Map{{"count", u32(3)}}};
    EXPECT_EQ(data.toBlob(), expected.toBlob());
}

TEST_F(SetDataObjectFieldImpl, StoringTwiceUnderOneKeyKeepsTheLastValue)
{
    auto const contractHost = host();

    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(3)));
    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(4)));

    expectValue(contractHost->getDataObjectField(alice.id(), "count"), serialization(u32(4)));
}

TEST_F(SetDataObjectFieldImpl, WhatIsFinalizedIsVisibleToTheNextCall)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(3)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    auto const sle = contractData(alice.id(), contract.id());
    ASSERT_NE(sle, nullptr) << "finalizing writes the data object to the ledger";

    // A host with no cache at all, so this can only be the ledger answering.
    expectValue(host()->getDataObjectField(alice.id(), "count"), serialization(u32(3)));
}

TEST_F(SetDataObjectFieldImpl, AnAccountThatDoesNotExistCannotBeWrittenTo)
{
    expectError(
        host()->setDataObjectField(Account{"ghost"}.id(), "count", u32(1)),
        HostFunctionError::InvalidAccount);
}

TEST_F(SetDataObjectFieldImpl, AnAccountWithNoDataObjectHasNoFieldToRead)
{
    expectError(
        host()->getDataObjectField(alice.id(), "count"), HostFunctionError::LedgerObjNotFound);
}

TEST_F(SetDataObjectFieldImpl, AKeyThatWasNeverStoredIsNotAField)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(3)));

    expectError(
        contractHost->getDataObjectField(alice.id(), "absent"), HostFunctionError::InvalidField);
}

// An account whose data object is an array is not one an object field can be stored in, and
// the contract is told so rather than having its array replaced.
TEST_F(SetDataObjectFieldImpl, AnArrayIsNotAnObject)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "first", u8(1)));

    expectError(
        contractHost->setDataObjectField(alice.id(), "count", u32(3)),
        HostFunctionError::InvalidState);
}

// The owner pays the data object's reserve, so an account with nothing spare cannot be
// written to at all — the contract cannot spend someone else's reserve for them.
TEST_F(SetDataObjectFieldImpl, AnOwnerWithNoSpareReserveIsRefused)
{
    auto const poor = fund("poor", XRP(10));

    expectError(
        host()->setDataObjectField(poor.id(), "count", u32(3)), HostFunctionError::InvalidState);
}

}  // namespace xrpl::test
