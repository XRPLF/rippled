#pragma once

#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/wasm/HostFunc.h>

namespace ripple {
class WasmHostFunctionsImpl : public HostFunctions
{
    ApplyContext& ctx;
    Keylet leKey;
    std::shared_ptr<SLE const> currentLedgerObj = nullptr;
    bool isLedgerObjCached = false;

    static int constexpr MAX_CACHE = 256;
    std::array<std::shared_ptr<SLE const>, MAX_CACHE> cache;
    std::optional<Bytes> data_;

    void const* rt_ = nullptr;

    Expected<std::shared_ptr<SLE const>, HostFunctionError>
    getCurrentLedgerObj()
    {
        if (!isLedgerObjCached)
        {
            isLedgerObjCached = true;
            currentLedgerObj = ctx.view().read(leKey);
        }
        if (currentLedgerObj)
            return currentLedgerObj;
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    }

    Expected<int32_t, HostFunctionError>
    normalizeCacheIndex(int32_t cacheIdx)
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HostFunctionError::SLOT_OUT_RANGE);
        if (!cache[cacheIdx])
            return Unexpected(HostFunctionError::EMPTY_SLOT);
        return cacheIdx;
    }

public:
    WasmHostFunctionsImpl(ApplyContext& ctx, Keylet const& leKey)
        : ctx(ctx), leKey(leKey)
    {
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return ctx.journal;
    }

    std::optional<Bytes> const&
    getData() const
    {
        return data_;
    }

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override;

    Expected<std::int32_t, HostFunctionError>
    getParentLedgerTime() override;

    Expected<Hash, HostFunctionError>
    getParentLedgerHash() override;

    Expected<int32_t, HostFunctionError>
    getBaseFee() override;

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId) override;

    Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName) override;

    Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override;

    Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator) override;

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator) override;

    Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator) override;

    Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator) override;

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator) override;

    Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator) override;

    Expected<int32_t, HostFunctionError>
    updateData(Slice const& data) override;

    Expected<int32_t, HostFunctionError>
    checkSignature(
        Slice const& message,
        Slice const& signature,
        Slice const& pubkey) override;

    Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data) override;

    Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) override;

    Expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2) override;

    Expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType) override;

    Expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account) override;

    Expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize)
        override;

    Expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
        override;

    Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    lineKeylet(
        AccountID const& account1,
        AccountID const& account2,
        Currency const& currency) override;

    Expected<Bytes, HostFunctionError>
    mptIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder) override;

    Expected<Bytes, HostFunctionError>
    nftOfferKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId) override;

    Expected<Bytes, HostFunctionError>
    paychanKeylet(
        AccountID const& account,
        AccountID const& destination,
        std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq)
        override;

    Expected<Bytes, HostFunctionError>
    signersKeylet(AccountID const& account) override;

    Expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    vaultKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) override;

    Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId) override;

    Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId) override;

    Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId) override;

    Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId) override;

    Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId) override;

    Expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex) override;

    Expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data) override;

    Expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account)
        override;

    Expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data) override;

    Expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount) override;

    Expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatSet(int64_t mantissa, int32_t exponent, int32_t mode) override;

    Expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y) override;

    Expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode) override;

    Expected<Bytes, HostFunctionError>
    floatLog(Slice const& x, int32_t mode) override;
};

}  // namespace ripple
