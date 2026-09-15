#include <xrpl/protocol/STJson.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

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
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
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

}  // namespace xrpl::test
