#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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
        sponsorshipKeylet,
        (AccountID const& sponsor, AccountID const& sponsee),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        loanBrokerKeylet,
        (AccountID const& owner, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        loanKeylet,
        (uint256 const& loanBrokerID, std::uint32_t loanSeq),
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

    // The contract host functions. Unlike everything above they are not `const`: they
    // mutate the contract's data cache, its transaction builder and its event map.

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        instanceParam,
        (std::uint32_t index, std::uint32_t stTypeId),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        functionParam,
        (std::uint32_t index, std::uint32_t stTypeId),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getDataObjectField,
        (AccountID const& account, std::string_view const& key),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getDataNestedObjectField,
        (AccountID const& account, std::string_view const& key, std::string_view const& nestedKey),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getDataArrayElementField,
        (AccountID const& account, std::size_t index, std::string_view const& key),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getDataNestedArrayElementField,
        (AccountID const& account,
         std::string_view const& key,
         std::size_t index,
         std::string_view const& nestedKey),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        setDataObjectField,
        (AccountID const& account, std::string_view const& key, STJson::Value const& value),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        setDataNestedObjectField,
        (AccountID const& account,
         std::string_view const& key,
         std::string_view const& nestedKey,
         STJson::Value const& value),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        setDataArrayElementField,
        (AccountID const& account,
         std::size_t index,
         std::string_view const& key,
         STJson::Value const& value),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        setDataNestedArrayElementField,
        (AccountID const& account,
         std::string_view const& key,
         std::size_t index,
         std::string_view const& nestedKey,
         STJson::Value const& value),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        buildTxn,
        (std::uint16_t const& txType),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        addTxnField,
        (std::uint32_t const& index, SField const& field, Slice const& data),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        emitBuiltTxn,
        (std::uint32_t const& index),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        emitTxn,
        (std::shared_ptr<STTx const> const& stxPtr),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        emitEvent,
        (std::string_view const& eventName, STJson const& eventData),
        (override));
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

// Matches a `Slice` against exact bytes. `BytesAre` compares against a string and so stops
// at the first NUL, which most serialized fields contain.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(SliceIs, expected, "")
{
    return Bytes{arg.data(), arg.data() + arg.size()} == expected;
}

// Matches an `AccountID` against another, so an expectation can name *whose* data a
// contract asked the host for.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(AccountIs, expected, "")
{
    return arg == expected;
}

// Matches an `STJson::Value` against the type and serialization it should carry, which
// is what the guest wrote into the value region.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P2(JsonValueIs, type, bytes, "")
{
    if (!arg || arg->getSType() != type)
        return false;
    Serializer s;
    arg->add(s);
    return Bytes{s.peekData().begin(), s.peekData().end()} == bytes;
}

// Matches an `STJson` by its serialization. `isEquivalent` compares the `shared_ptr`s a
// map holds rather than the values they point at, so it cannot be used here.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(EventJsonEq, expected, "")
{
    return arg.toBlob() == expected.toBlob();
}

// Matches a transaction by its id, which is all a host call needs to say *which*
// transaction the guest handed it.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(StTxIdIs, expected, "")
{
    return arg && arg->getTransactionID() == expected;
}

}  // namespace xrpl::test
