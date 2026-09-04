#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>  // keylet::account
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/Account.h>
#include <helpers/CaptureSink.h>
#include <helpers/TxTest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A real genesis ledger and the real host built over it, with **no test framework**.
//
// This is the piece both `xrpl_tests` and `xrpl.bench.wasm` need, and the reason it is its own
// type: a benchmark wants a ledger and a host, not GTest's lifecycle. `RealHostFixture` adds the
// framework on top (`: testing::Test, WasmLedger`) plus the assertion helpers; a benchmark uses
// `WasmLedger` directly and links no GTest at all.
//
// Setup steps here **throw** rather than `EXPECT_`. That is the point of the separation, not a
// detail: an `EXPECT_` outside a running test is recorded and discarded, so a benchmark whose
// escrow was never created would still run its host call, take the not-found path, and report a
// cheap, plausible, completely wrong price. Throwing turns that into a stopped run.

namespace xrpl::test {

// Fail a setup step loudly. See the note above on why this is not an `EXPECT_`.
[[noreturn]] void
fixtureFailed(std::string_view what);

struct SignedMessage
{
    Bytes message;
    Bytes signature;
    Bytes publicKey;
};

SignedMessage
signMessage(std::string_view message, KeyType keyType = KeyType::Secp256k1);

uint256
credentialId(
    std::string_view hex = "0011223344556677889900112233445566778899001122334455667788990011");

STObject
makeMemo(Bytes const& data);

struct TxAssembler
{
    TxType type;
    std::function<void(STObject&)> build;
};

TxAssembler
bareTx(TxType type = ttESCROW_FINISH);
TxAssembler
escrowFinishTx(TxTest& ledger, Account const& acct);
TxAssembler
ammDepositTx(Account const& acct, Asset const& asset1, Asset const& asset2);
TxAssembler
mptIssuanceCreateTx(Account const& acct, std::uint8_t scale);

class WasmHost
{
public:
    WasmHost(
        std::shared_ptr<STTx const> tx,
        std::unique_ptr<ApplyContext> context,
        std::unique_ptr<WasmHostFunctionsImpl> host);

    WasmHostFunctionsImpl*
    operator->() const;
    WasmHostFunctionsImpl&
    operator*() const;

private:
    std::shared_ptr<STTx const> tx_;
    std::unique_ptr<ApplyContext> context_;
    std::unique_ptr<WasmHostFunctionsImpl> host_;
};

class WasmLedger
{
public:
    TxTest ledger;

    Account
    fund(char const* name, XRPAmount amount = XRP(1000));

    WasmHost
    makeHost(
        beast::Journal journal,
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {});

    // The common case: a host that discards its log output.
    WasmHost
    makeHost(
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {});

    // A host whose `trace` output is captured, so a test can read it back with `logged()`.
    // The sink is a fixture member, so it outlives the host and accumulates across a test.
    WasmHost
    makeTracingHost(
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {});

    // Everything `trace` has written to the tracing host so far.
    [[nodiscard]] std::string
    logged() const;

    // Submit a real SignerListSet so `keylet::signerList(owner)` exists — the object the
    // signer-list nested-field / array-length getters read. `signers` pairs each signer
    // account with its weight.
    void
    makeSignerList(
        Account const& owner,
        std::uint32_t quorum,
        std::vector<std::pair<Account, std::uint16_t>> const& signers);

    static Bytes
    toBytes(std::uint8_t value);
    static Bytes
    toBytes(std::uint16_t value);
    static Bytes
    toBytes(std::uint32_t value);
    static Bytes
    toBytes(uint256 const& value);
    static Bytes
    toBytes(std::string_view value);
    static Bytes
    toBytes(std::span<std::uint8_t const> value);
    static Bytes
    toBytes(AccountID const& account);
    static Bytes
    toBytes(Issue const& issue);
    static Bytes
    toBytes(Asset const& asset);
    static Bytes
    toBytes(STAmount const& amount);
    static Bytes
    toBytes(STNumber const& number);

private:
    CaptureSink traceSink_{beast::Severity::Trace};
};

}  // namespace xrpl::test
