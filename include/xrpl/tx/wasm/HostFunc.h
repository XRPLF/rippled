#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl {

namespace wasm_float {

std::string
floatToString(Slice const& data);

std::expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatFromSTAmountImpl(STAmount const& x, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatFromSTNumberImpl(STNumber const& x, int32_t mode);

std::expected<int64_t, HostFunctionError>
floatToIntImpl(Slice const& x, int32_t mode);

std::expected<FloatPair, HostFunctionError>
floatToMantExpImpl(Slice const& x);

std::expected<Bytes, HostFunctionError>
floatFromMantExpImpl(int64_t mantissa, int32_t exponent, int32_t mode);

std::expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y);

std::expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode);

std::expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode);

}  // namespace wasm_float

// Intended to work only through wasm runtime. Don't call them directly, except with unit tests
class HostFunctions
{
protected:
    RTOptRef rt_;
    beast::Journal j_;

public:
    HostFunctions(beast::Journal j = beast::Journal{beast::Journal::getNullSink()}) : j_(j)
    {
    }

    void
    setRT(WasmRuntimeWrapper& rt)
    {
        rt_ = rt;
    }

    void
    resetRT()
    {
        rt_ = std::nullopt;
    }

    [[nodiscard]] WasmRuntimeWrapper&
    getRT() const
    {
        if (!rt_)
            Throw<std::logic_error>("Wasm runtime not set");
        return rt_->get();
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

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFunctionError>
    getParentLedgerTime() const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Hash, HostFunctionError>
    getParentLedgerHash() const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<uint32_t, HostFunctionError>
    getBaseFee() const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getTxNestedField(FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, FieldLocator const& locator) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    updateData(Slice const& data)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    trustLineKeylet(AccountID const& account1, AccountID const& account2, Currency const& currency)
        const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    mptokenIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    nftokenOfferKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    paychannelKeylet(AccountID const& account, AccountID const& destination, std::uint32_t seq)
        const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    signerListKeylet(AccountID const& account) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    vaultKeylet(AccountID const& account, std::uint32_t seq) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<std::uint32_t, HostFunctionError>
    getNFTSequence(uint256 const& nftId) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatFromSTAmount(STAmount const& x, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatFromSTNumber(STNumber const& x, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int64_t, HostFunctionError>
    floatToInt(Slice const& x, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<FloatPair, HostFunctionError>
    floatToMantExp(Slice const& x) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    [[nodiscard]] virtual std::expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) const
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    instanceParam(std::uint32_t index, std::uint32_t stTypeId)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    functionParam(std::uint32_t index, std::uint32_t stTypeId)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    getDataObjectField(AccountID const& account, std::string_view const& key)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    getDataNestedObjectField(
        AccountID const& account,
        std::string_view const& key,
        std::string_view const& nestedKey)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    getDataArrayElementField(AccountID const& account, size_t index, std::string_view const& key)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<Bytes, HostFunctionError>
    getDataNestedArrayElementField(
        AccountID const& account,
        std::string_view const& key,
        size_t index,
        std::string_view const& nestedKey)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    setDataObjectField(
        AccountID const& account,
        std::string_view const& keyName,
        STJson::Value const& value)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    setDataNestedObjectField(
        AccountID const& account,
        std::string_view const& nestedKey,
        std::string_view const& key,
        STJson::Value const& value)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    setDataArrayElementField(
        AccountID const& account,
        size_t index,
        std::string_view const& key,
        STJson::Value const& value)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    setDataNestedArrayElementField(
        AccountID const& account,
        std::string_view const& key,
        size_t index,
        std::string_view const& nestedKey,
        STJson::Value const& value)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    buildTxn(std::uint16_t const& txType)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    addTxnField(std::uint32_t const& index, SField const& field, Slice const& data)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    emitBuiltTxn(std::uint32_t const& index)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    emitTxn(std::shared_ptr<STTx const> const& stxPtr)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual std::expected<int32_t, HostFunctionError>
    emitEvent(std::string_view const& eventName, STJson const& eventData)
    {
        return std::unexpected(HostFunctionError::Unimplemented);
    }

    virtual ~HostFunctions() = default;
    // LCOV_EXCL_STOP
};

using HFRef = std::reference_wrapper<HostFunctions>;

}  // namespace xrpl
