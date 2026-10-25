#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

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
        return makeTracingContractHost({contract.id(), alice.id()});
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
        contractHost->addTxnField(0, sfDestination, slice(bare)), HostFunctionError::InvalidParams);
    EXPECT_NE(logged().find("addTxnField: Invalid value"), std::string::npos);
}

TEST_F(AddTxnFieldImpl, AFieldIsDeserializedOntoTheTransaction)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(192)});
    auto const destination = accountField(carol.id());
    expectValue(contractHost->addTxnField(0, sfAmount, slice(amount)), 0);
    expectValue(contractHost->addTxnField(0, sfDestination, slice(destination)), 0);

    auto const& built = contractHost.context().builtTxns.at(0);
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

    EXPECT_EQ(contractHost.context().builtTxns.at(0).getFieldAmount(sfAmount), STAmount{XRP(2)});
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
    EXPECT_NE(logged().find("not allowed in transaction type"), std::string::npos);
}

TEST_F(AddTxnFieldImpl, AnIndexNamingNoTransactionIsOutOfBounds)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(1)});
    expectError(
        contractHost->addTxnField(1, sfAmount, slice(amount)), HostFunctionError::IndexOutOfBounds);
    EXPECT_NE(logged().find("addTxnField: index out of bounds"), std::string::npos);
}

// A transaction whose TransactionType was rewritten to one no format has takes no more fields.
TEST_F(AddTxnFieldImpl, a_transaction_type_that_does_not_exist_takes_no_fields)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));
    Bytes const unknownType{0xff, 0xff};
    ASSERT_TRUE(contractHost->addTxnField(0, sfTransactionType, slice(unknownType)));

    auto const amount = WasmLedger::toBytes(STAmount{XRP(1)});
    expectError(
        contractHost->addTxnField(0, sfAmount, slice(amount)), HostFunctionError::FieldNotFound);
    EXPECT_NE(logged().find("addTxnField: Invalid TransactionType"), std::string::npos);
}

}  // namespace xrpl::test
