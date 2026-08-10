#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace xrpl {

// Intended to work only through wasm runtime. Don't call them directly, except with unit tests
class WasmHostFunctionsImpl : public HostFunctions
{
    ApplyContext& ctx_;

    Keylet leKey_;
    mutable std::optional<std::shared_ptr<SLE const>> currentLedgerObj_;

    static int constexpr maxCache = 256;
    std::array<std::shared_ptr<SLE const>, maxCache> cache_;

    std::optional<Bytes> data_;

public:
    std::expected<std::shared_ptr<SLE const>, HostFunctionError>
    getCurrentLedgerObj() const
    {
        if (!currentLedgerObj_)
            currentLedgerObj_ = ctx_.view().read(leKey_);
        if (*currentLedgerObj_)
            return *currentLedgerObj_;
        return std::unexpected(HostFunctionError::LedgerObjNotFound);
    }

    std::expected<int32_t, HostFunctionError>
    normalizeCacheIndex(int32_t cacheIdx) const
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= maxCache)
            return std::unexpected(HostFunctionError::SlotOutRange);
        if (!cache_[cacheIdx])
            return std::unexpected(HostFunctionError::EmptySlot);
        return cacheIdx;
    }

    template <typename F>
    void
    log(std::string_view const& msg, F&& dataFn) const
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        if (!getJournal().active(beast::Severity::Trace))
            return;
        auto j = getJournal().trace();
#endif
        j << "WasmTrace[" << toShortString(leKey_.key) << "]: " << msg << " " << dataFn();

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
    }

public:
    WasmHostFunctionsImpl(ApplyContext& ct, Keylet const& leKey)
        : HostFunctions(ct.journal), ctx_(ct), leKey_(leKey)
    {
    }

    bool
    checkSelf() const override
    {
        return !currentLedgerObj_ && !data_ &&
            std::ranges::none_of(cache_, [](auto const& p) { return !!p; });
    }

    std::optional<Bytes> const&
    getData() const
    {
        return data_;
    }

    std::expected<std::uint32_t, HostFunctionError>
    getLedgerSqn() const override;

    std::expected<std::uint32_t, HostFunctionError>
    getParentLedgerTime() const override;

    std::expected<Hash, HostFunctionError>
    getParentLedgerHash() const override;

    std::expected<std::uint32_t, HostFunctionError>
    getBaseFee() const override;

    std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) const override;

    std::expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) const override;

    std::expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override;

    std::expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) const override;

    std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) const override;

    std::expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) const override;

    std::expected<Bytes, HostFunctionError>
    getTxNestedField(FieldLocator const& locator) const override;

    std::expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(FieldLocator const& locator) const override;

    std::expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, FieldLocator const& locator) const override;

    std::expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) const override;

    std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) const override;

    std::expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const override;

    std::expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(FieldLocator const& locator) const override;

    std::expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(FieldLocator const& locator) const override;

    std::expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, FieldLocator const& locator) const override;

    std::expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override;

    std::expected<int32_t, HostFunctionError>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey)
        const override;

    std::expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) const override;

    std::expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) const override;

    std::expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const override;

    std::expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize) const override;

    std::expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize) const override;

    std::expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    trustLineKeylet(AccountID const& account1, AccountID const& account2, Currency const& currency)
        const override;

    std::expected<Bytes, HostFunctionError>
    mptokenIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) const override;

    std::expected<Bytes, HostFunctionError>
    nftokenOfferKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId) const override;

    std::expected<Bytes, HostFunctionError>
    paychannelKeylet(AccountID const& account, AccountID const& destination, std::uint32_t seq)
        const override;

    std::expected<Bytes, HostFunctionError>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    signerListKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    vaultKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) const override;

    std::expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) const override;

    std::expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) const override;

    std::expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) const override;

    std::expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) const override;

    std::expected<std::uint32_t, HostFunctionError>
    getNFTSequence(uint256 const& nftId) const override;

    void
    trace(std::string_view const& msg, std::string_view const& data) const override;

    std::expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatFromSTAmount(STAmount const& x, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatFromSTNumber(STNumber const& x, int32_t mode) const override;

    std::expected<int64_t, HostFunctionError>
    floatToInt(Slice const& x, int32_t mode) const override;

    std::expected<FloatPair, HostFunctionError>
    floatToMantExp(Slice const& x) const override;

    std::expected<Bytes, HostFunctionError>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const override;

    std::expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) const override;

    std::expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) const override;

    std::expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) const override;
};

}  // namespace xrpl
