#pragma once

#include <xrpld/app/wasm/ParamsHelper.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

enum class HostFunctionError : int32_t {
    INTERNAL = -1,
    FIELD_NOT_FOUND = -2,
    BUFFER_TOO_SMALL = -3,
    NO_ARRAY = -4,
    NOT_LEAF_FIELD = -5,
    LOCATOR_MALFORMED = -6,
    SLOT_OUT_RANGE = -7,
    SLOTS_FULL = -8,
    EMPTY_SLOT = -9,
    LEDGER_OBJ_NOT_FOUND = -10,
    DECODING = -11,
    DATA_FIELD_TOO_LARGE = -12,
    POINTER_OUT_OF_BOUNDS = -13,
    NO_MEM_EXPORTED = -14,
    INVALID_PARAMS = -15,
    INVALID_ACCOUNT = -16,
    INVALID_FIELD = -17,
    INDEX_OUT_OF_BOUNDS = -18,
    FLOAT_INPUT_MALFORMED = -19,
    FLOAT_COMPUTATION_ERROR = -20,
};

inline int32_t
HfErrorToInt(HostFunctionError e)
{
    return static_cast<int32_t>(e);
}

std::string
floatToString(Slice const& data);

Expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode);

Expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode);

Expected<Bytes, HostFunctionError>
floatSetImpl(int64_t mantissa, int32_t exponent, int32_t mode);

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y);

Expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode);

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode);

Expected<Bytes, HostFunctionError>
floatLogImpl(Slice const& x, int32_t mode);

struct HostFunctions
{
    // LCOV_EXCL_START
    virtual void
    setRT(void const*)
    {
    }

    virtual void const*
    getRT() const
    {
        return nullptr;
    }

    virtual beast::Journal
    getJournal()
    {
        return beast::Journal{beast::Journal::getNullSink()};
    }

    virtual Expected<std::int32_t, HostFunctionError>
    getLedgerSqn()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<std::int32_t, HostFunctionError>
    getParentLedgerTime()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    getParentLedgerHash()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getBaseFee()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(uint256 const& amendmentId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    isAmendmentEnabled(std::string_view const& amendmentName)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    updateData(Slice const& data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    checkSignature(
        Slice const& message,
        Slice const& signature,
        Slice const& pubkey)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    ammKeylet(Asset const& issue1, Asset const& issue2)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    lineKeylet(
        AccountID const& account1,
        AccountID const& account2,
        Currency const& currency)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    mptIssuanceKeylet(AccountID const& issuer, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    mptokenKeylet(MPTID const& mptid, AccountID const& holder)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    nftOfferKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    paychanKeylet(
        AccountID const& account,
        AccountID const& destination,
        std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    permissionedDomainKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    signersKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    vaultKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceAccount(std::string_view const& msg, AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceFloat(std::string_view const& msg, Slice const& data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceAmount(std::string_view const& msg, STAmount const& amount)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatFromInt(int64_t x, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatFromUint(uint64_t x, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatSet(int64_t mantissa, int32_t exponent, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    floatCompare(Slice const& x, Slice const& y)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatAdd(Slice const& x, Slice const& y, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatSubtract(Slice const& x, Slice const& y, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatMultiply(Slice const& x, Slice const& y, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatDivide(Slice const& x, Slice const& y, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatRoot(Slice const& x, int32_t n, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatPower(Slice const& x, int32_t n, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    floatLog(Slice const& x, int32_t mode)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual ~HostFunctions() = default;
    // LCOV_EXCL_STOP
};

}  // namespace ripple
