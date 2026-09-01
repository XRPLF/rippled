#include <tx/wasm/fixtures/BenchFixtures.h>

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
#include <tx/wasm/fixtures/FloatConstants.h>
#include <tx/wasm/fixtures/NftSetup.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <string>
#include <string_view>
#include <utility>

namespace xrpl::test::bench {

Fixtures::Fixtures()
    : alice_{ledger_.fund("benchAlice")}
    , bob_{ledger_.fund("benchBob")}
    , signerListOwner_{ledger_.fund("benchSigners")}
    , escrow_{keylet::account(AccountID{})}
    , signedMessage_{signMessage("the quick brown fox jumps over the lazy dog")}
    , nftId_{NftIds::makeNftId(alice_.id())}
{
    ledger_.makeSignerList(signerListOwner_, 2, {{alice_, 1}, {bob_, 1}});

    // The escrow has to be submitted after the accounts exist, which is why it is built here
    // rather than in the initializer list: its keylet depends on the owner's sequence number at
    // submission time.
    auto const ownerSeq = ledger_.ledger.getAccountRoot(alice_.id()).getSequence();
    auto const created = ledger_.ledger.submit(
        transactions::EscrowCreateBuilder{alice_.id(), bob_.id(), XRP(100)}.setFinishAfter(
            900'000'000),
        alice_);
    if (created.ter != tesSUCCESS)
    {
        fixtureFailed(std::string{"creating the escrow: "} + transToken(created.ter));
    }
    ledger_.ledger.close();
    escrow_ = keylet::escrow(alice_.id(), SeqProxy::rawSequence(ownerSeq));
}

Account const&
Fixtures::alice() const
{
    return alice_;
}

Account const&
Fixtures::bob() const
{
    return bob_;
}

TxAssembler
Fixtures::memoTx()
{
    auto assembler = escrowFinishTx(ledger_.ledger, alice_);
    assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
        inner(obj);
        auto memos = STArray{};
        memos.push_back(makeMemo(WasmLedger::toBytes("hello")));
        memos.push_back(makeMemo(WasmLedger::toBytes("world")));
        obj.setFieldArray(sfMemos, memos);
    };
    return assembler;
}

FieldLocator
Fixtures::memoLocator()
{
    return FieldLocator{{sfMemos.getCode(), 0, sfMemoData.getCode()}};
}

WasmHost
Fixtures::host()
{
    auto assembler = memoTx();
    return ledger_.makeHost(
        keylet::account(alice_.id()), assembler.type, std::move(assembler.build));
}

WasmHost
Fixtures::cachedHost()
{
    auto wasmHost = host();
    if (!wasmHost->cacheLedgerObj(keylet::account(alice_.id()).key, 1).has_value())
    {
        fixtureFailed("caching the account root into slot 1");
    }
    return wasmHost;
}

WasmHost
Fixtures::signerListHost()
{
    auto assembler = bareTx();
    return ledger_.makeHost(
        keylet::signerList(signerListOwner_.id()), assembler.type, std::move(assembler.build));
}

WasmHost
Fixtures::cachedSignerListHost()
{
    auto assembler = bareTx();
    auto wasmHost =
        ledger_.makeHost(keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
    if (!wasmHost->cacheLedgerObj(keylet::signerList(signerListOwner_.id()).key, 1).has_value())
    {
        fixtureFailed("caching the signer list into slot 1");
    }
    return wasmHost;
}

WasmHost
Fixtures::tracingHost()
{
    return ledger_.makeTracingHost();
}

Keylet const&
Fixtures::escrow() const
{
    return escrow_;
}

WasmHost
Fixtures::escrowHost()
{
    return ledger_.makeHost(escrow_);
}

Slice
Fixtures::floatX()
{
    return FloatConstants::slice(FloatConstants::kPi);
}

Slice
Fixtures::floatY()
{
    return FloatConstants::slice(FloatConstants::kTwo);
}

SignedMessage const&
Fixtures::signedMessage() const
{
    return signedMessage_;
}

uint256 const&
Fixtures::nftId() const
{
    return nftId_;
}

Fixtures&
Fixtures::instance()
{
    static Fixtures kValue;
    return kValue;
}

}  // namespace xrpl::test::bench
