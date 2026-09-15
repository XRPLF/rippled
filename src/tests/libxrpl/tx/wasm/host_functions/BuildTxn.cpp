#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <cstdint>

namespace xrpl::test {

// build_txn — the start of an emitted transaction. It answers an index into the
// contract's own list of transactions under construction, which `add_txn_field` and
// `emit_built_txn` then name.
struct BuildTxnImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host()
    {
        return makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});
    }
};

TEST_F(BuildTxnImpl, TheFirstTransactionIsIndexZeroAndEachIsItsOwn)
{
    auto const contractHost = host();

    expectValue(contractHost->buildTxn(ttPAYMENT), 0);
    expectValue(contractHost->buildTxn(ttPAYMENT), 1);
    expectValue(contractHost->buildTxn(ttACCOUNT_SET), 2);
    EXPECT_EQ(contractHost.context().built_txns.size(), 3U);
}

// Everything the contract does not get to choose: who is sending, in what order, for what
// fee, and that this is an inner transaction rather than one a network saw.
TEST_F(BuildTxnImpl, ATransactionIsStampedWithTheContractsOwnIdentity)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const& built = contractHost.context().built_txns.at(0);
    EXPECT_EQ(built.getAccountID(sfAccount), contract.id());
    EXPECT_EQ(built.getFieldAmount(sfFee), STAmount{0});
    EXPECT_TRUE(built.isFlag(tfInnerBatchTxn));
    EXPECT_TRUE(built.getFieldVL(sfSigningPubKey).empty())
        << "an emitted transaction is authorized by the contract, not by a signature";
}

// Successive transactions take successive sequence numbers, so a contract can emit more
// than one without each invalidating the next.
TEST_F(BuildTxnImpl, EachTransactionTakesTheNextSequence)
{
    auto const contractHost = host();
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));
    ASSERT_TRUE(contractHost->buildTxn(ttPAYMENT));

    auto const& first = contractHost.context().built_txns.at(0);
    auto const& second = contractHost.context().built_txns.at(1);
    EXPECT_EQ(second.getFieldU32(sfSequence), first.getFieldU32(sfSequence) + 1);
}

// A transaction type a contract may not emit is refused here rather than at the emit, so
// nothing is built that could not be sent.
TEST_F(BuildTxnImpl, ATypeAContractMayNotEmitIsRefused)
{
    expectError(host()->buildTxn(ttCONTRACT_CALL), HostFunctionError::SubmitTxnFailure);
}

}  // namespace xrpl::test
