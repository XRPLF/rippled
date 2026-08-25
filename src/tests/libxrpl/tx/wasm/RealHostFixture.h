#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>  // keylet::account
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/CaptureSink.h>
#include <helpers/TxTest.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::test {

template <typename T, typename U>
void
expectValue(
    std::expected<T, HostFunctionError> const& result,
    U const& expected,
    std::source_location loc = std::source_location::current())
{
    auto trace = testing::ScopedTrace{loc.file_name(), static_cast<int>(loc.line()), ""};
    ASSERT_TRUE(result.has_value())
        << "expected a value, got error " << static_cast<int>(result.error());
    EXPECT_EQ(*result, expected);
}

template <typename T>
void
expectError(
    std::expected<T, HostFunctionError> const& result,
    HostFunctionError expected,
    std::source_location loc = std::source_location::current())
{
    auto trace = testing::ScopedTrace{loc.file_name(), static_cast<int>(loc.line()), ""};
    ASSERT_FALSE(result.has_value()) << "expected error, got a value";
    EXPECT_EQ(result.error(), expected);
}

void
expectKeyletMatches(std::expected<Bytes, HostFunctionError> const& result, Keylet const& expected);

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

class RealHostFixture : public testing::Test
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
