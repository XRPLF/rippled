#pragma once

#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/STTx.h>

#include <type_traits>

namespace xrpl {

/*
assert(enforce)

There are several asserts (or XRPL_ASSERTs) in invariant check files that check
a variable named `enforce` when an invariant fails. At first glance, those
asserts may look incorrect, but they are not.

Those asserts take advantage of two facts:
1. `asserts` are not (normally) executed in release builds.
2. Invariants should *never* fail, except in tests that specifically modify
   the open ledger to break them.

This makes `assert(enforce)` sort of a second-layer of invariant enforcement
aimed at _developers_. It's designed to fire if a developer writes code that
violates an invariant, and runs it in unit tests or a develop build that _does
not have the relevant amendments enabled_. It's intentionally a pain in the neck
so that bad code gets caught and fixed as early as possible.
*/

// Bitwise flags, 86 files, used in macros files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Privilege {
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

constexpr Privilege
operator|(Privilege lhs, Privilege rhs)
{
    return safeCast<Privilege>(
        safeCast<std::underlying_type_t<Privilege>>(lhs) |
        safeCast<std::underlying_type_t<Privilege>>(rhs));
}

bool
hasPrivilege(STTx const& tx, Privilege priv);

}  // namespace xrpl
