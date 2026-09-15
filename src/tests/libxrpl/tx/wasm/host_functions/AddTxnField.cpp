#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <cstdint>

namespace xrpl::test {

// add_txn_field — setting one field on a transaction being built, from that field's own
// serialization.
struct AddTxnFieldImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");
    Account const carol = fund("carol");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }

    static Slice
    slice(Bytes const& bytes)
    {
        return Slice{bytes.data(), bytes.size()};
    }
};

// An account field carries its length before its id, which is what `STAccount` reads. The
// bare 20 bytes are not a field, and a contract that writes them is told so rather than
// having them stored as something else.
TEST_F(AddTxnFieldImpl, ABareAccountIdIsNotAnAccountField)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const bare = WasmLedger::toBytes(carol.id());
    expectError(
        contractHost->addTxnField(0, sfDestination, slice(bare)), HostFunctionError::InternalFatal);
}

TEST_F(AddTxnFieldImpl, AFieldIsDeserializedOntoTheTransaction)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(192)});
    auto const destination = accountField(carol.id());
    expectValue(contractHost->addTxnField(0, sfAmount, slice(amount)), 0);
    expectValue(contractHost->addTxnField(0, sfDestination, slice(destination)), 0);

    auto const& built = contractHost.context().built_txns.at(0);
    EXPECT_EQ(built.getFieldAmount(sfAmount), STAmount{XRP(192)});
    EXPECT_EQ(built.getAccountID(sfDestination), carol.id());
}

TEST_F(AddTxnFieldImpl, SettingAFieldTwiceKeepsTheLastValue)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const first = WasmLedger::toBytes(STAmount{XRP(1)});
    auto const second = WasmLedger::toBytes(STAmount{XRP(2)});
    ASSERT_TRUE(contractHost->addTxnField(0, sfAmount, slice(first)));
    ASSERT_TRUE(contractHost->addTxnField(0, sfAmount, slice(second)));

    EXPECT_EQ(contractHost.context().built_txns.at(0).getFieldAmount(sfAmount), STAmount{XRP(2)});
}

// A field the transaction's own format does not name is refused, so a contract cannot
// assemble something that could never be applied.
TEST_F(AddTxnFieldImpl, AFieldTheTransactionTypeDoesNotHaveIsRefused)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(1)});
    expectError(
        contractHost->addTxnField(0, sfLimitAmount, slice(amount)),
        HostFunctionError::FieldNotFound);
}

TEST_F(AddTxnFieldImpl, AnIndexNamingNoTransactionIsOutOfBounds)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(1)});
    expectError(
        contractHost->addTxnField(1, sfAmount, slice(amount)), HostFunctionError::IndexOutOfBounds);
}

}  // namespace xrpl::test
