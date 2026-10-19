#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFailure.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <xrpl_host_functions.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace xrpl {

namespace wasm_float {

std::string
floatToString(Slice const& data);

std::expected<Bytes, HostFailure>
floatFromIntImpl(int64_t x, int32_t mode);

std::expected<Bytes, HostFailure>
floatFromUintImpl(uint64_t x, int32_t mode);

std::expected<Bytes, HostFailure>
floatFromSTAmountImpl(STAmount const& x, int32_t mode);

std::expected<Bytes, HostFailure>
floatFromSTNumberImpl(STNumber const& x, int32_t mode);

std::expected<int64_t, HostFailure>
floatToIntImpl(Slice const& x, int32_t mode);

std::expected<FloatPair, HostFailure>
floatToMantExpImpl(Slice const& x);

std::expected<Bytes, HostFailure>
floatFromMantExpImpl(int64_t mantissa, int32_t exponent, int32_t mode);

std::expected<FloatOrdering, HostFailure>
floatCompareImpl(Slice const& x, Slice const& y);

std::expected<Bytes, HostFailure>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFailure>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFailure>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFailure>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFailure>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode);

}  // namespace wasm_float

// Intended to work only through wasm runtime. Don't call them directly, except with unit tests
class HostFunctions
{
protected:
    beast::Journal j_;

public:
    HostFunctions(beast::Journal j = beast::Journal{beast::Journal::getNullSink()}) : j_(j)
    {
    }

    [[nodiscard]] beast::Journal
    getJournal() const
    {
        return j_;
    }

    // LCOV_EXCL_START

    [[nodiscard]] virtual bool
    checkSelf() const
    {
        return true;
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFailure>
    getLedgerSqn() const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFailure>
    getParentLedgerTime() const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Hash, HostFailure>
    getParentLedgerHash() const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<uint32_t, HostFailure>
    getBaseFee() const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    isAmendmentEnabled(uint256 const& amendmentId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    isAmendmentEnabled(std::string_view const& amendmentName) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    virtual std::expected<int32_t, HostFailure>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getTxField(SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getCurrentLedgerObjField(SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getTxNestedField(FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getCurrentLedgerObjNestedField(FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getLedgerObjNestedField(int32_t cacheIdx, FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getTxArrayLen(SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getCurrentLedgerObjArrayLen(SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getTxNestedArrayLen(FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getCurrentLedgerObjNestedArrayLen(FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, FieldLocator const& locator) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    virtual std::expected<int32_t, HostFailure>
    updateData(Slice const& data)
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Hash, HostFailure>
    computeSha512HalfHash(Slice const& data) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    accountKeylet(AccountID const& account) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    ammKeylet(Asset const& issue1, Asset const& issue2) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    checkKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    didKeylet(AccountID const& account) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    delegateKeylet(AccountID const& account, AccountID const& authorize) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    trustLineKeylet(AccountID const& account1, AccountID const& account2, Currency const& currency)
        const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    mptokenIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    nftokenOfferKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    offerKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    oracleKeylet(AccountID const& account, std::uint32_t docId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    paychannelKeylet(AccountID const& account, AccountID const& destination, std::uint32_t seq)
        const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    signerListKeylet(AccountID const& account) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    ticketKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    vaultKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    sponsorshipKeylet(AccountID const& sponsor, AccountID const& sponsee) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    loanBrokerKeylet(AccountID const& owner, std::uint32_t seq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    loanKeylet(uint256 const& loanBrokerID, std::uint32_t loanSeq) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getNFT(AccountID const& account, uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    getNFTIssuer(uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFailure>
    getNFTTaxon(uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getNFTFlags(uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFailure>
    getNFTTransferFee(uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFailure>
    getNFTSequence(uint256 const& nftId) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    // A no-op rather than InternalFatal: trace only writes to the local log.
    // trace_wrap has already rendered the guest's buffer into `data`.
    virtual void
    trace(std::string_view const& msg, std::string_view const& data) const
    {
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatFromInt(int64_t x, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatFromUint(uint64_t x, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatFromSTAmount(STAmount const& x, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatFromSTNumber(STNumber const& x, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<int64_t, HostFailure>
    floatToInt(Slice const& x, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<FloatPair, HostFailure>
    floatToMantExp(Slice const& x) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<FloatOrdering, HostFailure>
    floatCompare(Slice const& x, Slice const& y) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFailure>
    floatPower(Slice const& x, int32_t n, int32_t mode) const
    {
        return std::unexpected(HostTrapError::InternalFatal);
    }

    virtual ~HostFunctions() = default;
    // LCOV_EXCL_STOP
};

}  // namespace xrpl
