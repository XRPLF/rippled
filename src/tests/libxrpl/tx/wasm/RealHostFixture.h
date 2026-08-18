#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>  // TapNone
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>  // keylet::account
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/SignerListSet.h>
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

inline Bytes
toBytes(std::uint8_t value)
{
    return {value};
}

inline Bytes
toBytes(std::uint16_t value)
{
    return {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)};
}

inline Bytes
toBytes(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24)};
}

inline Bytes
toBytes(uint256 const& value)
{
    return Bytes{std::begin(value), std::end(value)};
}

inline Bytes
toBytes(std::string_view value)
{
    return Bytes{std::begin(value), std::end(value)};
}

inline Bytes
toBytes(std::span<std::uint8_t const> value)
{
    return Bytes{std::begin(value), std::end(value)};
}

inline Bytes
toBytes(AccountID const& account)
{
    return Bytes{std::begin(account), std::end(account)};
}

inline Bytes
toBytes(Issue const& issue)
{
    auto s = Serializer{};
    s.addBitString(issue.currency);
    if (!isXRP(issue.currency))
        s.addBitString(issue.account);
    return s.getData();
}

inline Bytes
toBytes(Asset const& asset)
{
    if (asset.holds<Issue>())
        return toBytes(asset.get<Issue>());

    auto const& mptIssue = asset.get<MPTIssue>();
    auto const& mptID = mptIssue.getMptID();
    return Bytes{mptID.cbegin(), mptID.cend()};
}

inline Bytes
toBytes(STAmount const& amount)
{
    auto msg = Serializer{};
    amount.add(msg);
    return msg.getData();
}

inline Bytes
toBytes(STNumber const& number)
{
    auto msg = Serializer{};
    number.add(msg);
    return msg.getData();
}

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

inline void
expectKeyletMatches(std::expected<Bytes, HostFunctionError> const& result, Keylet const& expected)
{
    expectValue(result, toBytes(expected.key));
}

struct SignedMessage
{
    Bytes message;
    Bytes signature;
    Bytes publicKey;
};

inline SignedMessage
signMessage(std::string_view message, KeyType keyType = KeyType::Secp256k1)
{
    auto const [pk, sk] = randomKeyPair(keyType);
    auto const msg = Bytes{std::begin(message), std::end(message)};
    auto const sig = sign(pk, sk, Slice{msg.data(), msg.size()});
    return {
        .message = msg,
        .signature = Bytes{sig.data(), sig.data() + sig.size()},
        .publicKey = Bytes{pk.data(), pk.data() + pk.size()}};
}

inline uint256
credentialId(
    std::string_view hex = "0011223344556677889900112233445566778899001122334455667788990011")
{
    auto id = uint256{};
    EXPECT_TRUE(id.parseHex(std::string{hex}));
    return id;
}

inline STObject
makeMemo(Bytes const& data)
{
    auto memo = STObject::makeInnerObject(sfMemo);
    memo.setFieldVL(sfMemoData, data);
    return memo;
}

struct TxAssembler
{
    TxType type;
    std::function<void(STObject&)> build;
};

inline TxAssembler
bareTx(TxType type = ttESCROW_FINISH)
{
    return {.type = type, .build = [](STObject&) {}};
}

inline TxAssembler
escrowFinishTx(TxTest& ledger, Account const& acct)
{
    return {.type = ttESCROW_FINISH, .build = [&ledger, acct](STObject& obj) {
                auto credId = uint256{};
                EXPECT_TRUE(credId.parseHex(
                    "0011223344556677889900112233445566778899001122334455667788990011"));

                obj.setAccountID(sfAccount, acct.id());
                obj.setAccountID(sfOwner, acct.id());
                obj.setFieldU32(sfOfferSequence, ledger.getAccountRoot(acct.id()).getSequence());
                obj.setFieldArray(sfMemos, STArray{});
                auto credIds = STVector256{};
                credIds.pushBack(credId);
                obj.setFieldV256(sfCredentialIDs, credIds);
            }};
}

inline TxAssembler
ammDepositTx(Account const& acct, Asset const& asset1, Asset const& asset2)
{
    return {.type = ttAMM_DEPOSIT, .build = [acct, asset1, asset2](STObject& obj) {
                obj.setAccountID(sfAccount, acct.id());
                obj.setFieldIssue(sfAsset, STIssue{sfAsset, asset1});
                obj.setFieldIssue(sfAsset2, STIssue{sfAsset2, asset2});
            }};
}

inline TxAssembler
mptIssuanceCreateTx(Account const& acct, std::uint8_t scale)
{
    return {.type = ttMPTOKEN_ISSUANCE_CREATE, .build = [acct, scale](STObject& obj) {
                obj.setAccountID(sfAccount, acct.id());
                obj.setFieldU8(sfAssetScale, scale);
            }};
}

class WasmHost
{
public:
    WasmHost(
        std::shared_ptr<STTx const> tx,
        std::unique_ptr<ApplyContext> context,
        std::unique_ptr<WasmHostFunctionsImpl> host)
        : tx_{std::move(tx)}, context_{std::move(context)}, host_{std::move(host)}
    {
    }

    WasmHostFunctionsImpl*
    operator->() const
    {
        return host_.get();
    }
    WasmHostFunctionsImpl&
    operator*() const
    {
        return *host_;
    }

private:
    std::shared_ptr<STTx const> tx_;
    std::unique_ptr<ApplyContext> context_;
    std::unique_ptr<WasmHostFunctionsImpl> host_;
};

class WasmImplTest : public testing::Test
{
public:
    TxTest ledger;

    Account
    fund(char const* name, XRPAmount amount = XRP(1000))
    {
        auto const account = Account{name};
        ledger.createAccount(account, amount);
        return account;
    }

    WasmHost
    makeHost(
        beast::Journal journal,
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {})
    {
        auto tx = std::make_shared<STTx>(
            txType, [assembler = std::move(assembler)](STObject& obj) { assembler(obj); });
        auto context = std::make_unique<ApplyContext>(
            ledger.getServiceRegistry(),
            ledger.getOpenLedger(),
            *tx,
            tesSUCCESS,
            ledger.getOpenLedger().fees().base,
            TapNone,
            journal);
        auto host = std::make_unique<WasmHostFunctionsImpl>(*context, leKey);
        return WasmHost{std::move(tx), std::move(context), std::move(host)};
    }

    // The common case: a host that discards its log output.
    WasmHost
    makeHost(
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {})
    {
        return makeHost(
            beast::Journal{beast::Journal::getNullSink()}, leKey, txType, std::move(assembler));
    }

    // A host whose `trace` output is captured, so a test can read it back with `logged()`.
    // The sink is a fixture member, so it outlives the host and accumulates across a test.
    WasmHost
    makeTracingHost(
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {})
    {
        return makeHost(beast::Journal{traceSink_}, leKey, txType, std::move(assembler));
    }

    // Everything `trace` has written to the tracing host so far.
    [[nodiscard]] std::string
    logged() const
    {
        return traceSink_.messages();
    }

    // Submit a real SignerListSet so `keylet::signerList(owner)` exists — the object the
    // signer-list nested-field / array-length getters read. `signers` pairs each signer
    // account with its weight.
    void
    makeSignerList(
        Account const& owner,
        std::uint32_t quorum,
        std::vector<std::pair<Account, std::uint16_t>> const& signers)
    {
        auto entries = STArray{};
        for (auto const& [signer, weight] : signers)
        {
            auto entry = STObject::makeInnerObject(sfSignerEntry);
            entry.setAccountID(sfAccount, signer.id());
            entry.setFieldU16(sfSignerWeight, weight);
            entries.push_back(std::move(entry));
        }
        auto const r = ledger.submit(
            transactions::SignerListSetBuilder{owner.id(), quorum}.setSignerEntries(entries),
            owner);
        EXPECT_EQ(r.ter, tesSUCCESS) << transToken(r.ter);
        ledger.close();
    }

private:
    CaptureSink traceSink_{beast::Severity::Trace};
};

}  // namespace xrpl::test
