#include <tx/wasm/BenchFixtures.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/NFTFixture.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl::test::bench {

[[noreturn]] void
benchSetupFailed(std::string_view what)
{
    throw std::runtime_error("benchmark fixture setup failed: " + std::string{what});
}

BenchFixture&
benchLedger()
{
    static BenchFixture value;
    return value;
}

Account const&
benchAlice()
{
    static auto const kValue = benchLedger().fund("benchAlice");
    return kValue;
}

Account const&
benchBob()
{
    static auto const kValue = benchLedger().fund("benchBob");
    return kValue;
}

TxAssembler
benchMemoTx()
{
    auto assembler = escrowFinishTx(benchLedger().ledger, benchAlice());
    assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
        inner(obj);
        auto memos = STArray{};
        memos.push_back(makeMemo(RealHostFixture::toBytes("hello")));
        memos.push_back(makeMemo(RealHostFixture::toBytes("world")));
        obj.setFieldArray(sfMemos, memos);
    };
    return assembler;
}

FieldLocator
benchMemoLocator()
{
    return FieldLocator{{sfMemos.getCode(), 0, sfMemoData.getCode()}};
}

WasmHost
benchHost()
{
    auto assembler = benchMemoTx();
    return benchLedger().makeHost(
        keylet::account(benchAlice().id()), assembler.type, std::move(assembler.build));
}

WasmHost
benchCachedHost()
{
    auto host = benchHost();
    if (!host->cacheLedgerObj(keylet::account(benchAlice().id()).key, 1).has_value())
    {
        benchSetupFailed("caching the account root into slot 1");
    }
    return host;
}

Account const&
benchSignerListOwner()
{
    static auto const kValue = [] {
        auto const acct = benchLedger().fund("benchSigners");
        benchLedger().makeSignerList(acct, 2, {{benchAlice(), 1}, {benchBob(), 1}});
        return acct;
    }();
    return kValue;
}

WasmHost
benchSignerListHost()
{
    auto assembler = bareTx();
    return benchLedger().makeHost(
        keylet::signerList(benchSignerListOwner().id()),
        assembler.type,
        std::move(assembler.build));
}

WasmHost
benchCachedSignerListHost()
{
    auto assembler = bareTx();
    auto host = benchLedger().makeHost(
        keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
    if (!host->cacheLedgerObj(keylet::signerList(benchSignerListOwner().id()).key, 1).has_value())
    {
        benchSetupFailed("caching the signer list into slot 1");
    }
    return host;
}

Keylet const&
benchEscrow()
{
    static auto const kValue = [] {
        auto const ownerSeq = benchLedger().ledger.getAccountRoot(benchAlice().id()).getSequence();
        auto const created = benchLedger().ledger.submit(
            transactions::EscrowCreateBuilder{benchAlice().id(), benchBob().id(), XRP(100)}
                .setFinishAfter(900'000'000),
            benchAlice());
        if (created.ter != tesSUCCESS)
        {
            benchSetupFailed(std::string{"creating the escrow: "} + transToken(created.ter));
        }
        benchLedger().ledger.close();
        return keylet::escrow(benchAlice().id(), SeqProxy::rawSequence(ownerSeq));
    }();
    return kValue;
}

WasmHost
benchEscrowHost()
{
    return benchLedger().makeHost(benchEscrow());
}

Slice
benchFloatX()
{
    return FloatTest::slice(FloatTest::kPi);
}

Slice
benchFloatY()
{
    return FloatTest::slice(FloatTest::kTwo);
}

SignedMessage const&
benchSignedMessage()
{
    static auto const kValue = signMessage("the quick brown fox jumps over the lazy dog");
    return kValue;
}

uint256 const&
benchNftId()
{
    // `makeNftId` is a static member, so no fixture instance is needed to reach it.
    static auto const kValue = NFTTest::makeNftId(benchAlice().id());
    return kValue;
}

}  // namespace xrpl::test::bench
