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
#include <xrpl/tx/wasm/HostFailure.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>

#include <xrpl_host_functions.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl::test {

// A mock of the host the wasm engine calls back into.
//
// One `MOCK_METHOD` per `HostFunctions` entry, in that header's order, each signature taken
// verbatim from it. The extra parentheses around a return type are what keeps the comma in
// `std::expected<T, HostFailure>` from splitting the macro's arguments.
//
// No `ON_CALL` defaults, deliberately: this is always used through `StrictMock`, which fails
// a call to a method carrying no `EXPECT_CALL`.
struct MockHostFunctions : HostFunctions
{
    explicit MockHostFunctions(beast::Journal journal) : HostFunctions(journal)
    {
    }

    MOCK_METHOD(bool, checkSelf, (), (const, override));

    MOCK_METHOD((std::expected<std::uint32_t, HostFailure>), getLedgerSqn, (), (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFailure>),
        getParentLedgerTime,
        (),
        (const, override));

    MOCK_METHOD((std::expected<Hash, HostFailure>), getParentLedgerHash, (), (const, override));

    MOCK_METHOD((std::expected<std::uint32_t, HostFailure>), getBaseFee, (), (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        isAmendmentEnabled,
        (uint256 const& amendmentId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        isAmendmentEnabled,
        (std::string_view const& amendmentName),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        cacheLedgerObj,
        (uint256 const& objId, std::int32_t cacheIdx),
        (override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getTxField,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getCurrentLedgerObjField,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getLedgerObjField,
        (std::int32_t cacheIdx, SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getTxNestedField,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getCurrentLedgerObjNestedField,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getLedgerObjNestedField,
        (std::int32_t cacheIdx, FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getTxArrayLen,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getCurrentLedgerObjArrayLen,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getLedgerObjArrayLen,
        (std::int32_t cacheIdx, SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getTxNestedArrayLen,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getCurrentLedgerObjNestedArrayLen,
        (FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getLedgerObjNestedArrayLen,
        (std::int32_t cacheIdx, FieldLocator const& locator),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        updateData,
        (Slice const& data),
        (override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        checkSignature,
        (Slice const& message, Slice const& signature, Slice const& pubkey),
        (const, override));

    MOCK_METHOD(
        (std::expected<Hash, HostFailure>),
        computeSha512HalfHash,
        (Slice const& data),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        accountKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        ammKeylet,
        (Asset const& issue1, Asset const& issue2),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        checkKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        credentialKeylet,
        (AccountID const& subject, AccountID const& issuer, Slice const& credentialType),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        didKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        delegateKeylet,
        (AccountID const& account, AccountID const& authorize),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        depositPreauthKeylet,
        (AccountID const& account, AccountID const& authorize),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        escrowKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        trustLineKeylet,
        (AccountID const& account1, AccountID const& account2, Currency const& currency),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        mptokenIssuanceKeylet,
        (AccountID const& issuer, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        mptokenKeylet,
        (MPTID const& mptid, AccountID const& holder),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        nftokenOfferKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        offerKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        oracleKeylet,
        (AccountID const& account, std::uint32_t docId),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        paychannelKeylet,
        (AccountID const& account, AccountID const& destination, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        permissionedDomainKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        signerListKeylet,
        (AccountID const& account),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        ticketKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        vaultKeylet,
        (AccountID const& account, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        sponsorshipKeylet,
        (AccountID const& sponsor, AccountID const& sponsee),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        loanBrokerKeylet,
        (AccountID const& owner, std::uint32_t seq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        loanKeylet,
        (uint256 const& loanBrokerID, std::uint32_t loanSeq),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getNFT,
        (AccountID const& account, uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        getNFTIssuer,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFailure>),
        getNFTTaxon,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getNFTFlags,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFailure>),
        getNFTTransferFee,
        (uint256 const& nftId),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFailure>),
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
        (std::expected<Bytes, HostFailure>),
        floatFromInt,
        (std::int64_t x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatFromUint,
        (std::uint64_t x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatFromSTAmount,
        (STAmount const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatFromSTNumber,
        (STNumber const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int64_t, HostFailure>),
        floatToInt,
        (Slice const& x, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<FloatPair, HostFailure>),
        floatToMantExp,
        (Slice const& x),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatFromMantExp,
        (std::int64_t mantissa, std::int32_t exponent, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<FloatOrdering, HostFailure>),
        floatCompare,
        (Slice const& x, Slice const& y),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatAdd,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatSubtract,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatMultiply,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
        floatDivide,
        (Slice const& x, Slice const& y, std::int32_t mode),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFailure>),
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
