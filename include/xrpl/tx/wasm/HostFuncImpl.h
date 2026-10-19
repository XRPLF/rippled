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
#include <xrpl/tx/wasm/HostConstants.h>
#include <xrpl/tx/wasm/HostFailure.h>
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
    std::expected<std::shared_ptr<SLE const>, HostFailure>
    getCurrentLedgerObj() const
    {
        if (!currentLedgerObj_)
            currentLedgerObj_ = ctx_.view().read(leKey_);
        if (*currentLedgerObj_)
            return *currentLedgerObj_;
        return std::unexpected(HostFunctionError::LedgerObjNotFound);
    }

    std::expected<int32_t, HostFailure>
    normalizeCacheIndex(int32_t cacheIdx) const
    {
        if (cacheIdx <= 0 || cacheIdx > maxCache)
            return std::unexpected(HostFunctionError::SlotOutRange);
        --cacheIdx;
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
        auto const j = getJournal().trace();
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

    std::expected<std::uint32_t, HostFailure>
    getLedgerSqn() const override;

    std::expected<std::uint32_t, HostFailure>
    getParentLedgerTime() const override;

    std::expected<Hash, HostFailure>
    getParentLedgerHash() const override;

    std::expected<std::uint32_t, HostFailure>
    getBaseFee() const override;

    std::expected<int32_t, HostFailure>
    isAmendmentEnabled(uint256 const& amendmentId) const override;

    std::expected<int32_t, HostFailure>
    isAmendmentEnabled(std::string_view const& amendmentName) const override;

    std::expected<int32_t, HostFailure>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override;

    std::expected<Bytes, HostFailure>
    getTxField(SField const& fname) const override;

    std::expected<Bytes, HostFailure>
    getCurrentLedgerObjField(SField const& fname) const override;

    std::expected<Bytes, HostFailure>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) const override;

    std::expected<Bytes, HostFailure>
    getTxNestedField(FieldLocator const& locator) const override;

    std::expected<Bytes, HostFailure>
    getCurrentLedgerObjNestedField(FieldLocator const& locator) const override;

    std::expected<Bytes, HostFailure>
    getLedgerObjNestedField(int32_t cacheIdx, FieldLocator const& locator) const override;

    std::expected<int32_t, HostFailure>
    getTxArrayLen(SField const& fname) const override;

    std::expected<int32_t, HostFailure>
    getCurrentLedgerObjArrayLen(SField const& fname) const override;

    std::expected<int32_t, HostFailure>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) const override;

    std::expected<int32_t, HostFailure>
    getTxNestedArrayLen(FieldLocator const& locator) const override;

    std::expected<int32_t, HostFailure>
    getCurrentLedgerObjNestedArrayLen(FieldLocator const& locator) const override;

    std::expected<int32_t, HostFailure>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, FieldLocator const& locator) const override;

    std::expected<int32_t, HostFailure>
    updateData(Slice const& data) override;

    std::expected<int32_t, HostFailure>
    checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey)
        const override;

    std::expected<Hash, HostFailure>
    computeSha512HalfHash(Slice const& data) const override;

    std::expected<Bytes, HostFailure>
    accountKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFailure>
    ammKeylet(Asset const& issue1, Asset const& issue2) const override;

    std::expected<Bytes, HostFailure>
    checkKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
        const override;

    std::expected<Bytes, HostFailure>
    didKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFailure>
    delegateKeylet(AccountID const& account, AccountID const& authorize) const override;

    std::expected<Bytes, HostFailure>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize) const override;

    std::expected<Bytes, HostFailure>
    escrowKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    trustLineKeylet(AccountID const& account1, AccountID const& account2, Currency const& currency)
        const override;

    std::expected<Bytes, HostFailure>
    mptokenIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) const override;

    std::expected<Bytes, HostFailure>
    nftokenOfferKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    offerKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    oracleKeylet(AccountID const& account, std::uint32_t docId) const override;

    std::expected<Bytes, HostFailure>
    paychannelKeylet(AccountID const& account, AccountID const& destination, std::uint32_t seq)
        const override;

    std::expected<Bytes, HostFailure>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    signerListKeylet(AccountID const& account) const override;

    std::expected<Bytes, HostFailure>
    ticketKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    vaultKeylet(AccountID const& account, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    sponsorshipKeylet(AccountID const& sponsor, AccountID const& sponsee) const override;

    std::expected<Bytes, HostFailure>
    loanBrokerKeylet(AccountID const& owner, std::uint32_t seq) const override;

    std::expected<Bytes, HostFailure>
    loanKeylet(uint256 const& loanBrokerID, std::uint32_t loanSeq) const override;

    std::expected<Bytes, HostFailure>
    getNFT(AccountID const& account, uint256 const& nftId) const override;

    std::expected<Bytes, HostFailure>
    getNFTIssuer(uint256 const& nftId) const override;

    std::expected<std::uint32_t, HostFailure>
    getNFTTaxon(uint256 const& nftId) const override;

    std::expected<int32_t, HostFailure>
    getNFTFlags(uint256 const& nftId) const override;

    std::expected<int32_t, HostFailure>
    getNFTTransferFee(uint256 const& nftId) const override;

    std::expected<std::uint32_t, HostFailure>
    getNFTSequence(uint256 const& nftId) const override;

    void
    trace(std::string_view const& msg, std::string_view const& data) const override;

    std::expected<Bytes, HostFailure>
    floatFromInt(int64_t x, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatFromUint(uint64_t x, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatFromSTAmount(STAmount const& x, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatFromSTNumber(STNumber const& x, int32_t mode) const override;

    std::expected<int64_t, HostFailure>
    floatToInt(Slice const& x, int32_t mode) const override;

    std::expected<FloatPair, HostFailure>
    floatToMantExp(Slice const& x) const override;

    std::expected<Bytes, HostFailure>
    floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const override;

    std::expected<FloatOrdering, HostFailure>
    floatCompare(Slice const& x, Slice const& y) const override;

    std::expected<Bytes, HostFailure>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) const override;

    std::expected<Bytes, HostFailure>
    floatPower(Slice const& x, int32_t n, int32_t mode) const override;
};

}  // namespace xrpl
