#pragma once

#include <rust/cxx.h>

#include <cstdint>

namespace xrpl {
// `xrpl::HostFunctions` is forward-declared rather than included: this header is
// `include!()`d by the cxxbridge-generated translation unit, whose target gets only the
// project's `include/` directory - not the Boost paths that HostFunc.h -> Slice.h ->
// strHex.h transitively need. A reference member and declarations alone do not require a
// complete type; HostContext.cpp, compiled into libxrpl, includes the real header.
class HostFunctions;

// Defined by the cxx bridge, which emits it into `xrpl_wasm_vm_ffi_cxxbridge/lib.h` from the
// declaration in `crates/xrpl-wasm-vm-ffi` - so the data types and their wire values are
// written once, in Rust, rather than kept in step with a copy here.
//
// Forward-declared for the reason `HostFunctions` above is: that generated header includes
// this one, so naming its definition here would be circular. A scoped enum with a fixed
// underlying type needs no definition to appear in a signature; `HostContext.cpp` includes
// the generated header for the `switch`.
enum class TraceDataType : std::int32_t;

// The host handed to the Rust wasm engine: one method per entry in the wasm host ABI,
// each forwarding to `xrpl::HostFunctions` - the single source of truth for ledger
// access - and lowering its typed `std::expected` result onto the ABI's wire form.
//
// Every method is `noexcept`, and every body catches everything: a C++ exception
// unwinding into the Rust frames that called it would be undefined behaviour, so a caught
// one leaves here as `HostFunctionError::InternalFatal`, which the engine reads as a fatal
// error and reports as `tecINTERNAL`.
//
// Not an owner: it borrows the `HostFunctions` it is built over for the length of one run.
class HostContext
{
    // Non-const so a host function that mutates (`cacheLedgerObj`, `updateData`) can be
    // reached from the `const` methods below: constness of the reference is not
    // constness of the referent.
    HostFunctions& hostFunctions_;

public:
    HostContext(HostFunctions& hostFunctions);

    // A byte-producing call is handed `out` - a slice aliasing either guest linear
    // memory or the engine's output buffer - writes the value only if the whole of it
    // fits, and returns the value's *true* length, which may exceed `out`. That is how a
    // guest learns the size to ask for, and it is why these methods never need to know
    // the guest's capacity: the engine owns the buffer-fit, field-cap and transfer-budget
    // rules and derives all three from the length returned here.
    //
    // A negative return is a `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    getLedgerSqn(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getParentLedgerTime(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getParentLedgerHash(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getBaseFee(rust::Slice<std::uint8_t> out) const noexcept;

    // The amendment is either a 32-byte id or a name; a 32-byte input is tried as an
    // id first and falls back to a name lookup. Answers 1 or 0, or a negative
    // `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    isAmendmentEnabled(rust::Slice<std::uint8_t const> amendment) const noexcept;

    // The object id must be a 32-byte uint256, else `InvalidParams`. `cacheIdx` selects
    // the slot (0 = pick a free one). Answers the slot used, or a negative
    // `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    cacheLedgerObj(rust::Slice<std::uint8_t const> objId, std::int32_t cacheIdx) const noexcept;

    [[nodiscard]] std::int32_t
    getTxField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjField(std::int32_t cacheIdx, std::int32_t field, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The locator is a path of little-endian i32 steps, so its byte length must be a
    // non-zero multiple of 4, else `LocatorMalformed`.
    [[nodiscard]] std::int32_t
    getTxNestedField(rust::Slice<std::uint8_t const> locator, rust::Slice<std::uint8_t> out)
        const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjNestedField(
        rust::Slice<std::uint8_t const> locator,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjNestedField(
        std::int32_t cacheIdx,
        rust::Slice<std::uint8_t const> locator,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Answers the array's element count directly, or a negative `HostFunctionError`
    // code (`NoArray` if the field is not an array).
    [[nodiscard]] std::int32_t
    getTxArrayLen(std::int32_t field) const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjArrayLen(std::int32_t field) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjArrayLen(std::int32_t cacheIdx, std::int32_t field) const noexcept;

    [[nodiscard]] std::int32_t
    getTxNestedArrayLen(rust::Slice<std::uint8_t const> locator) const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjNestedArrayLen(rust::Slice<std::uint8_t const> locator) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjNestedArrayLen(std::int32_t cacheIdx, rust::Slice<std::uint8_t const> locator)
        const noexcept;

    // Answers 1/0 for a valid/invalid signature, or a negative `HostFunctionError`.
    [[nodiscard]] std::int32_t
    checkSignature(
        rust::Slice<std::uint8_t const> message,
        rust::Slice<std::uint8_t const> signature,
        rust::Slice<std::uint8_t const> pubkey) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    accountKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
        const noexcept;

    // Each asset is decoded by length (24 = MPT, 20 = XRP, 40 = issue), else
    // `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    ammKeylet(
        rust::Slice<std::uint8_t const> asset1,
        rust::Slice<std::uint8_t const> asset2,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    checkKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Subject and issuer must each be 20 bytes, else `InvalidParams`. Writes the
    // 32-byte keylet.
    [[nodiscard]] std::int32_t
    credentialKeylet(
        rust::Slice<std::uint8_t const> subject,
        rust::Slice<std::uint8_t const> issuer,
        rust::Slice<std::uint8_t const> credentialType,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Both accounts must be 20 bytes, else `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    delegateKeylet(
        rust::Slice<std::uint8_t const> account,
        rust::Slice<std::uint8_t const> authorize,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Both accounts must be 20 bytes, else `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    depositPreauthKeylet(
        rust::Slice<std::uint8_t const> account,
        rust::Slice<std::uint8_t const> authorize,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    didKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    escrowKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Both accounts and the currency must each be 20 bytes, else `InvalidParams`.
    // Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    trustLineKeylet(
        rust::Slice<std::uint8_t const> account1,
        rust::Slice<std::uint8_t const> account2,
        rust::Slice<std::uint8_t const> currency,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The issuer id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    mptokenIssuanceKeylet(
        rust::Slice<std::uint8_t const> issuer,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The MPT id must be 24 bytes and the holder 20, else `InvalidParams`. Writes the
    // 32-byte keylet.
    [[nodiscard]] std::int32_t
    mptokenKeylet(
        rust::Slice<std::uint8_t const> mptid,
        rust::Slice<std::uint8_t const> holder,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    nftokenOfferKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    offerKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `docId` carries the
    // guest's u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    oracleKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t docId,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Both account ids must be 20 bytes, else `InvalidParams`. `seq` carries the
    // guest's u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    paychannelKeylet(
        rust::Slice<std::uint8_t const> account,
        rust::Slice<std::uint8_t const> destination,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    permissionedDomainKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    signerListKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    ticketKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The account id must be 20 bytes, else `InvalidParams`. `seq` carries the guest's
    // u32 as its i32 bit pattern. Writes the 32-byte keylet.
    [[nodiscard]] std::int32_t
    vaultKeylet(
        rust::Slice<std::uint8_t const> account,
        std::int32_t seq,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    sha512Half(rust::Slice<std::uint8_t const> data, rust::Slice<std::uint8_t> out) const noexcept;

    // Renders `data` as `dataType` says, and hands the text to `HostFunctions::trace`, which
    // is what puts it in this node's log.
    //
    // The one call that answers nothing: the guest's wasm function has no result, and this
    // node's own log is the only thing a trace touches, so a buffer that does not hold what
    // it claims is logged here and dropped rather than reported to a contract.
    void
    trace(rust::Str msg, rust::Slice<std::uint8_t const> data, TraceDataType dataType)
        const noexcept;

    // Stores `data` as the current object's data field and returns the number of bytes
    // stored, or a negative `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    updateData(rust::Slice<std::uint8_t const> data) const noexcept;

    // The account id must be 20 bytes and the nft id 32 bytes, else `InvalidParams`.
    // Writes the token's URI bytes.
    [[nodiscard]] std::int32_t
    getNFT(
        rust::Slice<std::uint8_t const> account,
        rust::Slice<std::uint8_t const> nftId,
        rust::Slice<std::uint8_t> out) const noexcept;

    // The nft id must be 32 bytes, else `InvalidParams`. Writes the 20-byte issuer
    // account encoded in the id.
    [[nodiscard]] std::int32_t
    getNFTIssuer(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The nft id must be 32 bytes, else `InvalidParams`. Writes the taxon as its four
    // little-endian bytes.
    [[nodiscard]] std::int32_t
    getNFTTaxon(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The nft id must be 32 bytes, else `InvalidParams`. Returns the flags, or a
    // negative `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    getNFTFlags(rust::Slice<std::uint8_t const> nftId) const noexcept;

    // The nft id must be 32 bytes, else `InvalidParams`. Returns the transfer fee, or a
    // negative `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    getNFTTransferFee(rust::Slice<std::uint8_t const> nftId) const noexcept;

    // The nft id must be 32 bytes, else `InvalidParams`. Writes the sequence number as
    // its four little-endian bytes.
    [[nodiscard]] std::int32_t
    getNFTSequence(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
        const noexcept;

    // Float / number arithmetic. A float is an XRPL `Number` in serialized form;
    // `mode` is a rounding mode. Each writes the result float bytes unless noted.

    [[nodiscard]] std::int32_t
    floatFromInt(std::int64_t x, std::int32_t mode, rust::Slice<std::uint8_t> out) const noexcept;

    // The integer region must be eight bytes, else `InvalidParams`.
    [[nodiscard]] std::int32_t
    floatFromUint(
        rust::Slice<std::uint8_t const> x,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    // `amount` must be a serialized `STAmount`, else `InvalidParams`.
    [[nodiscard]] std::int32_t
    floatFromSTAmount(
        rust::Slice<std::uint8_t const> amount,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    // `number` must be a serialized `STNumber`, else `InvalidParams`.
    [[nodiscard]] std::int32_t
    floatFromSTNumber(
        rust::Slice<std::uint8_t const> number,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Rounds the float to an integer, written as its eight little-endian bytes.
    [[nodiscard]] std::int32_t
    floatToInt(rust::Slice<std::uint8_t const> x, std::int32_t mode, rust::Slice<std::uint8_t> out)
        const noexcept;

    // Writes the mantissa (eight little-endian bytes) and the exponent (four little-
    // endian bytes) to two output regions; returns their total size.
    [[nodiscard]] std::int32_t
    floatToMantExp(
        rust::Slice<std::uint8_t const> x,
        rust::Slice<std::uint8_t> mantissaOut,
        rust::Slice<std::uint8_t> exponentOut) const noexcept;

    [[nodiscard]] std::int32_t
    floatFromMantExp(
        std::int64_t mantissa,
        std::int32_t exponent,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Returns a negative, zero, or positive scalar as `x` is less than, equal to, or
    // greater than `y`, or a negative `HostFunctionError` code on failure.
    [[nodiscard]] std::int32_t
    floatCompare(rust::Slice<std::uint8_t const> x, rust::Slice<std::uint8_t const> y)
        const noexcept;

    [[nodiscard]] std::int32_t
    floatAdd(
        rust::Slice<std::uint8_t const> x,
        rust::Slice<std::uint8_t const> y,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    floatSubtract(
        rust::Slice<std::uint8_t const> x,
        rust::Slice<std::uint8_t const> y,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    floatMultiply(
        rust::Slice<std::uint8_t const> x,
        rust::Slice<std::uint8_t const> y,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    floatDivide(
        rust::Slice<std::uint8_t const> x,
        rust::Slice<std::uint8_t const> y,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    floatRoot(
        rust::Slice<std::uint8_t const> x,
        std::int32_t n,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    floatPower(
        rust::Slice<std::uint8_t const> x,
        std::int32_t n,
        std::int32_t mode,
        rust::Slice<std::uint8_t> out) const noexcept;
};

}  // namespace xrpl
