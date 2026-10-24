#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <string>

namespace xrpl::test {

// get_data_object_field — reading one key out of an account's data object.
//
// What it answers is the value's canonical serialization with no type byte: the contract
// asked for a key, and the type is the one the value was stored as.
struct GetDataObjectFieldImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeContractHost({contract.id(), alice.id()});
    }

    // An account whose data object is already on the ledger, so a read has something to find
    // without a write in the same run.
    void
    store(std::string const& key, STJson::Value const& value)
    {
        auto const contractHost = host();
        ASSERT_TRUE(contractHost->setDataObjectField(alice.id(), key, value));
        ASSERT_EQ(contractHost.finalize(), tesSUCCESS);
    }
};

TEST_F(GetDataObjectFieldImpl, EveryWidthComesBackAsItsCanonicalBytes)
{
    store("value_u8", u8(42));
    store("value_u16", u16(1234));
    store("count", u32(3));
    store("total", u64(9'876'543'210));

    auto const contractHost = host();
    expectValue(contractHost->getDataObjectField(alice.id(), "value_u8"), serialization(u8(42)));
    expectValue(
        contractHost->getDataObjectField(alice.id(), "value_u16"), serialization(u16(1234)));
    expectValue(contractHost->getDataObjectField(alice.id(), "count"), serialization(u32(3)));
    expectValue(
        contractHost->getDataObjectField(alice.id(), "total"), serialization(u64(9'876'543'210)));
}

// An integer's canonical serialization is big-endian, which is the opposite of what
// `instance_param` answers with. The two live side by side in the ABI, so this is asserted
// against the bytes rather than against a helper that could share the mistake.
TEST_F(GetDataObjectFieldImpl, AnIntegerComesBackBigEndian)
{
    store("count", u32(0x01020304));

    expectValue(host()->getDataObjectField(alice.id(), "count"), (Bytes{0x01, 0x02, 0x03, 0x04}));
}

TEST_F(GetDataObjectFieldImpl, AnAccountIsItsLengthAndItsTwentyBytes)
{
    store("owner", acct(contract.id()));

    auto const expected = serialization(acct(contract.id()));
    ASSERT_EQ(expected.size(), 21U) << "an account field carries its length";
    expectValue(host()->getDataObjectField(alice.id(), "owner"), expected);
}

TEST_F(GetDataObjectFieldImpl, AnAccountWithNoDataObjectHasNothingToRead)
{
    expectError(
        host()->getDataObjectField(alice.id(), "count"), HostFunctionError::LedgerObjNotFound);
}

TEST_F(GetDataObjectFieldImpl, AKeyThatIsNotThereIsNotAField)
{
    store("count", u32(3));

    expectError(host()->getDataObjectField(alice.id(), "absent"), HostFunctionError::InvalidField);
}

TEST_F(GetDataObjectFieldImpl, AnAccountThatDoesNotExistIsRefusedBeforeItsDataIsLookedFor)
{
    expectError(
        host()->getDataObjectField(Account{"ghost"}.id(), "count"),
        HostFunctionError::InvalidAccount);
}

// A read caches what the ledger holds for the rest of the call, unmodified, so finalizing
// writes nothing back for it.
TEST_F(GetDataObjectFieldImpl, a_read_is_cached_unmodified)
{
    store("count", u32(3));

    auto const contractHost = host();
    expectValue(contractHost->getDataObjectField(alice.id(), "count"), serialization(u32(3)));

    auto const& [modified, data] = contractHost.context().result.dataMap.at(alice.id());
    EXPECT_FALSE(modified);
    EXPECT_EQ(contractHost.context().result.dataMap.modifiedCount, 0U);
    expectValue(contractHost->getDataObjectField(alice.id(), "count"), serialization(u32(3)));
}

// An object holds no array elements, and an array holds no keys to keep an array under.
TEST_F(GetDataObjectFieldImpl, each_getter_reads_only_the_shape_it_names)
{
    store("count", u32(3));

    auto const contractHost = host();
    expectError(
        contractHost->getDataArrayElementField(alice.id(), 0, "count"),
        HostFunctionError::InvalidState);
    expectError(
        contractHost->getDataNestedObjectField(alice.id(), "count", "inner"),
        HostFunctionError::InvalidField);

    auto const arrayHost = host();
    ASSERT_TRUE(arrayHost->setDataArrayElementField(contract.id(), 0, "first", u8(1)));
    ASSERT_EQ(arrayHost.finalize(), tesSUCCESS);
    expectError(
        host()->getDataNestedArrayElementField(contract.id(), "first", 0, "first"),
        HostFunctionError::InvalidState);
    expectValue(host()->getDataArrayElementField(contract.id(), 0, "first"), serialization(u8(1)));
}

}  // namespace xrpl::test
