#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>

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
        return makeTracingContractHost({contract.id(), alice.id()});
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

// A data object costs one owner count per 512 bytes it holds, and a rewrite that changes that
// cost moves the owner's count by the difference, up or down.
TEST_F(SetDataObjectFieldImpl, a_rewrite_moves_the_reserve_by_the_difference)
{
    auto const writeAll = [&](STJson::Value const& value) {
        auto const contractHost = host();
        for (int i = 0; i < 40; ++i)
        {
            ASSERT_TRUE(
                contractHost->setDataObjectField(alice.id(), "k" + std::to_string(i), value));
        }
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    };

    // Forty one-byte values fit in 512 bytes.
    writeAll(u8(1));
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 1U);

    // Forty eight-byte values do not.
    writeAll(u64(1));
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 2U);

    writeAll(u8(2));
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 1U);
}

// A call may write at most 1000 times; reading an account's data does not count toward that.
TEST_F(SetDataObjectFieldImpl, only_writes_count_toward_the_write_limit)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(0)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    auto const contractHost = host();
    ASSERT_TRUE(contractHost->getDataObjectField(alice.id(), "count"));
    for (std::uint32_t i = 0; i < 1000; ++i)
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(i)));

    expectError(
        contractHost->setDataObjectField(alice.id(), "count", u32(1000)),
        HostFunctionError::InvalidState);
}

// Reading an account's data first does not excuse a later write from the reserve check.
TEST_F(SetDataObjectFieldImpl, a_write_after_a_read_still_needs_spare_reserve)
{
    // Ten XRP base reserve and two per object: thirteen pays for one object, not two.
    auto const owner = fund("owner", XRP(13));
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(owner.id(), "count", u32(1)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    auto const contractHost = host();
    ASSERT_TRUE(contractHost->getDataObjectField(owner.id(), "count"));
    expectError(
        contractHost->setDataObjectField(owner.id(), "count", u32(2)),
        HostFunctionError::InvalidState);
}

// A data object is at most one level deep, and a value that would nest deeper is the
// contract's mistake, answered as one.
TEST_F(SetDataObjectFieldImpl, a_value_nested_too_deep_is_refused)
{
    auto const object = [](STJson::Map map) { return std::make_shared<STJson>(std::move(map)); };
    auto const inner = object({{"x", u8(1)}});

    auto const contractHost = host();
    expectError(
        contractHost->setDataObjectField(alice.id(), "a", object({{"inner", inner}})),
        HostFunctionError::InvalidParams);
    expectError(
        contractHost->setDataNestedObjectField(alice.id(), "a", "b", inner),
        HostFunctionError::InvalidParams);
    expectError(
        contractHost->setDataNestedArrayElementField(alice.id(), "a", 0, "b", inner),
        HostFunctionError::InvalidParams);
    expectError(
        contractHost->setDataArrayElementField(contract.id(), 0, "b", inner),
        HostFunctionError::InvalidParams);
}

// Storing no value removes the key; the rest of the object stays.
TEST_F(SetDataObjectFieldImpl, an_empty_value_removes_the_key)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", u8(1)));
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "b", u8(2)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", nullptr));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    auto const sle = contractData(alice.id(), contract.id());
    ASSERT_NE(sle, nullptr);
    auto const& map = sle->getFieldJson(sfContractJson).getMap();
    EXPECT_FALSE(map.contains("a"));
    EXPECT_TRUE(map.contains("b"));
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 1U);
}

// Removing the last key deletes the data object and gives its reserve back.
TEST_F(SetDataObjectFieldImpl, removing_every_key_deletes_the_data_object)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", u8(1)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }
    ASSERT_EQ(ledger.getOwnerCount(alice.id()), 1U);
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", nullptr));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    EXPECT_EQ(contractData(alice.id(), contract.id()), nullptr);
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 0U);
}

// Removing keys can bring the object under a reserve step, and the owner count follows.
TEST_F(SetDataObjectFieldImpl, removing_keys_lowers_the_reserve)
{
    {
        auto const contractHost = host();
        for (int i = 0; i < 40; ++i)
        {
            ASSERT_TRUE(
                contractHost->setDataObjectField(alice.id(), "k" + std::to_string(i), u64(1)));
        }
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }
    ASSERT_EQ(ledger.getOwnerCount(alice.id()), 2U);
    {
        auto const contractHost = host();
        for (int i = 0; i < 20; ++i)
        {
            ASSERT_TRUE(
                contractHost->setDataObjectField(alice.id(), "k" + std::to_string(i), nullptr));
        }
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 1U);
}

// A removal costs no reserve, so an owner with none to spare can still remove a key.
TEST_F(SetDataObjectFieldImpl, a_removal_needs_no_spare_reserve)
{
    // Ten XRP base reserve and two per object: thirteen pays for the object and leaves one.
    auto const owner = fund("owner", XRP(13));
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(owner.id(), "a", u8(1)));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }

    auto const contractHost = host();
    expectValue(contractHost->setDataObjectField(owner.id(), "a", nullptr), 0);
    ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    EXPECT_EQ(contractData(owner.id(), contract.id()), nullptr);
}

// A data object is at most 1024 bytes, and the write that would pass that is refused when
// it is made, not when the call ends.
TEST_F(SetDataObjectFieldImpl, a_write_past_the_size_limit_is_refused)
{
    auto const contractHost = host();
    // Each key and eight-byte value takes 14 bytes; the 73rd takes the object past 1024.
    for (int i = 0; i < 72; ++i)
    {
        ASSERT_TRUE(
            contractHost->setDataObjectField(alice.id(), "k" + std::format("{:02}", i), u64(1)));
    }
    expectError(
        contractHost->setDataObjectField(alice.id(), "k72", u64(1)),
        HostFunctionError::DataFieldTooLarge);
    EXPECT_EQ(contractHost.finalize(), tesSUCCESS);
}

// Each refusal is logged with its reason.
TEST_F(SetDataObjectFieldImpl, each_refusal_is_logged_with_its_reason)
{
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataArrayElementField(alice.id(), 0, "first", u8(1)));
        expectError(
            contractHost->setDataObjectField(alice.id(), "count", u32(3)),
            HostFunctionError::InvalidState);
    }
    EXPECT_NE(logged().find("setDataObjectField: Invalid state: not an object"), std::string::npos);

    auto const poor = fund("poor", XRP(10));
    expectError(
        host()->setDataObjectField(poor.id(), "count", u32(3)), HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataCache: Insufficient reserve"), std::string::npos);
    EXPECT_NE(logged().find("setDataObjectField: Failed to set object field"), std::string::npos);

    auto const nested = std::make_shared<STJson>(
        STJson::Map{{"x", std::make_shared<STJson>(STJson::Map{{"y", u8(1)}})}});
    expectError(
        host()->setDataObjectField(alice.id(), "a", nested), HostFunctionError::InvalidParams);
    EXPECT_NE(logged().find("setDataObjectField: Invalid value"), std::string::npos);

    auto const contractHost = host();
    for (std::uint32_t i = 0; i < 1000; ++i)
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "count", u32(i)));
    expectError(
        contractHost->setDataObjectField(alice.id(), "count", u32(1000)),
        HostFunctionError::InvalidState);
    EXPECT_NE(logged().find("setDataCache: Exceeded max data modifications"), std::string::npos);
}

// Setting a key and removing it in one call leaves no data object to write or delete.
TEST_F(SetDataObjectFieldImpl, a_key_set_and_removed_in_one_call_writes_nothing)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", u8(1)));
    ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), "a", nullptr));
    ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    EXPECT_EQ(contractData(alice.id(), contract.id()), nullptr);
    EXPECT_EQ(ledger.getOwnerCount(alice.id()), 0U);
}

}  // namespace xrpl::test
