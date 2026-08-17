#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>

#include <cstdint>
#include <type_traits>

namespace xrpl {

enum class Delegation { Delegable, NotDelegable };

/**
 * Operations a transaction is permitted to perform, as a bitfield.
 *
 * These are declared per-transaction in transactions.macro (via
 * TxSettings::privileges) and enforced in InvariantCheck.cpp.
 */
enum class Privilege : std::uint16_t {
    NoPriv = 0x0000,              // The transaction can not do any of the enumerated operations
    CreateAcct = 0x0001,          // The transaction can create a new ACCOUNT_ROOT object.
    CreatePseudoAcct = 0x0002,    // The transaction can create a pseudo account,
                                  // which implies createAcct
    MustDeleteAcct = 0x0004,      // The transaction must delete an ACCOUNT_ROOT object
    MayDeleteAcct = 0x0008,       // The transaction may delete an ACCOUNT_ROOT
                                  // object, but does not have to
    OverrideFreeze = 0x0010,      // The transaction can override some freeze rules
    ChangeNftCounts = 0x0020,     // The transaction can mint or burn an NFT
    CreateMptIssuance = 0x0040,   // The transaction can create a new MPT issuance
    DestroyMptIssuance = 0x0080,  // The transaction can destroy an MPT issuance
    MustAuthorizeMpt = 0x0100,    // The transaction MUST create or delete an MPT
                                  // object (except by issuer)
    MayAuthorizeMpt = 0x0200,     // The transaction MAY create or delete an MPT
                                  // object (except by issuer)
    MayDeleteMpt = 0x0400,        // The transaction MAY delete an MPT object. May not create.
    MustModifyVault = 0x0800,     // The transaction must modify, delete or create, a vault
    MayModifyVault = 0x1000,      // The transaction MAY modify, delete or create, a vault
    MayCreateMpt = 0x2000,        // The transaction MAY create an MPT object, except for issuer.
};

// The inner static_cast is not redundant: the underlying type is narrower than
// `int`, so the operands integer-promote and the result has to be narrowed back.
// safeCast rejects that narrowing, but every input bit is a Privilege bit by
// construction, so the result is always representable.
constexpr Privilege
operator|(Privilege lhs, Privilege rhs)
{
    using Underlying = std::underlying_type_t<Privilege>;
    return static_cast<Privilege>(
        static_cast<Underlying>(safeCast<Underlying>(lhs) | safeCast<Underlying>(rhs)));
}

constexpr Privilege
operator&(Privilege lhs, Privilege rhs)
{
    using Underlying = std::underlying_type_t<Privilege>;
    return static_cast<Privilege>(
        static_cast<Underlying>(safeCast<Underlying>(lhs) & safeCast<Underlying>(rhs)));
}

/**
 * Per-transaction metadata declared in transactions.macro.
 *
 * Every member has a default, so a transaction only needs to name the settings
 * that differ from the common case. See the documentation at the top of
 * transactions.macro for the authoring syntax.
 *
 * This is deliberately not a constexpr-friendly type: amendment identifiers are
 * runtime-initialized `extern uint256 const` globals (see Feature.h), so a
 * TxSettings can only be built at runtime.
 */
struct TxSettings
{
    /**
     * Whether an account may delegate this transaction to another account.
     */
    Delegation delegable{Delegation::Delegable};

    /**
     * The amendment gating this transaction, or uint256{} if always available.
     */
    // The `{}` looks redundant, because BaseUInt's default constructor already
    // zeroes the value. It is not: without a default member initializer here,
    // every partial designated initializer in transactions.macro trips the
    // missing-designated-field-initializers warning, which the build treats as
    // an error.
    // NOLINTNEXTLINE(readability-redundant-member-init)
    uint256 amendment{};

    /**
     * Operations this transaction is permitted to perform.
     */
    Privilege privileges{Privilege::NoPriv};
};

}  // namespace xrpl
