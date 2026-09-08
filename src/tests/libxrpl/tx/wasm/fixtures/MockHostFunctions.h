#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl::test {

// A mock of the host the wasm engine calls back into.
//
// One `MOCK_METHOD` per `HostFunctions` entry, in that header's order, each signature taken
// verbatim from it. The extra parentheses around a return type are what keeps the comma in
// `std::expected<T, HostFunctionError>` from splitting the macro's arguments.
//
// No `ON_CALL` defaults, deliberately: this is always used through `StrictMock`, which fails
// a call to a method carrying no `EXPECT_CALL`.
struct MockHostFunctions : HostFunctions
{
    explicit MockHostFunctions(beast::Journal journal) : HostFunctions(journal)
    {
    }

    MOCK_METHOD(bool, checkSelf, (), (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getLedgerSqn,
        (),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getParentLedgerTime,
        (),
        (const, override));

    MOCK_METHOD(
        (std::expected<Hash, HostFunctionError>),
        getParentLedgerHash,
        (),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getBaseFee,
        (),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        isAmendmentEnabled,
        (uint256 const& amendmentId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        isAmendmentEnabled,
        (std::string_view const& amendmentName),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        cacheLedgerObj,
        (uint256 const& objId, std::int32_t cacheIdx),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getTxField,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getCurrentLedgerObjField,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getLedgerObjField,
        (std::int32_t cacheIdx, SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getTxNestedField,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getCurrentLedgerObjNestedField,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getLedgerObjNestedField,
        (std::int32_t cacheIdx, FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getTxArrayLen,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getCurrentLedgerObjArrayLen,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getLedgerObjArrayLen,
        (std::int32_t cacheIdx, SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getTxNestedArrayLen,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getCurrentLedgerObjNestedArrayLen,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getLedgerObjNestedArrayLen,
        (std::int32_t cacheIdx, FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        updateData,
        (Slice const& data),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        checkSignature,
        (Slice const& message, Slice const& signature, Slice const& pubkey),
        (const, override));

    MOCK_METHOD(
        (std::expected<Hash, HostFunctionError>),
        computeSha512HalfHash,
        (Slice const& data),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        accountKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        ammKeylet,
        (Asset const& issue1, Asset const& issue2),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        checkKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        credentialKeylet,
        (AccountID const& subject, AccountID const& issuer, Slice const& credentialType),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        didKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        delegateKeylet,
        (AccountID const& account, AccountID const& authorize),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        depositPreauthKeylet,
        (AccountID const& account, AccountID const& authorize),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        escrowKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        trustLineKeylet,
        (AccountID const& account1, AccountID const& account2, Currency const& currency),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        mptokenIssuanceKeylet,
        (AccountID const& issuer, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        mptokenKeylet,
        (MPTID const& mptid, AccountID const& holder),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        nftokenOfferKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        offerKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        oracleKeylet,
        (AccountID const& account, std::uint32_t docId),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        paychannelKeylet,
        (AccountID const& account, AccountID const& destination, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        permissionedDomainKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        signerListKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        ticketKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        vaultKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getNFT,
        (AccountID const& account, uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getNFTIssuer,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getNFTTaxon,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getNFTFlags,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        getNFTTransferFee,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getNFTSequence,
        (uint256 const& nftId),
        (const, override));

    // Takes the rendered text, not the guest's buffer: rendering is `HostContext`'s, so what
    // a test asserts here is the log line a node would write.
    MOCK_METHOD(
        void,
        trace,
        (std::string_view const& msg, std::string_view const& data),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatFromInt,
        (std::int64_t x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatFromUint,
        (std::uint64_t x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatFromSTAmount,
        (STAmount const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatFromSTNumber,
        (STNumber const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int64_t, HostFunctionError>),
        floatToInt,
        (Slice const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<FloatPair, HostFunctionError>),
        floatToMantExp,
        (Slice const& x),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatFromMantExp,
        (std::int64_t mantissa, std::int32_t exponent, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        floatCompare,
        (Slice const& x, Slice const& y),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatAdd,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatSubtract,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatMultiply,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatDivide,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        floatPower,
        (Slice const& x, std::int32_t n, std::int32_t mode),
        (const, override));
};

// Matches a `Slice` (or anything with `data()`/`size()`) against the bytes of a string, so
// an expectation can say *what* the guest asked the host to work on.
//
// `MATCHER_P` emits a function of this name, and gmock matchers are CamelCase by convention.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(BytesAre, expected, "")
{
    return std::string_view{reinterpret_cast<char const*>(arg.data()), arg.size()} ==
        std::string_view{expected};
}

}  // namespace xrpl::test
