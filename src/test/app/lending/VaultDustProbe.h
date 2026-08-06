#pragma once

// Vault Dust — the probe seam.
//
// Where a Vault's un-recognized remainder ("dust") is stored differs per
// implementation:
//   - the base branch has no dust mechanism at all;
//   - solution A (…-pseudo-account) stores it as the balance of a second
//     pseudo-account;
//   - solution B' (…-trustline-dust) stores it as a signed field beside the
//     balance on the Vault's custody trust line.
//
// This header is the ONLY place the shared test suite
// (src/test/app/lending/VaultRounding_test.cpp) is allowed to know about
// that difference, and it is the ONLY file that may differ between the two
// solution branches. Do not reference sfDustAccount, sfDust, or any other
// implementation-specific field anywhere else in a test — route every such
// read through readVaultDust() below.
//
// Both symbols below MUST be redefined identically in spirit, but
// differently in body, on each solution branch; the shared suite never
// changes.

#include <test/jtx/Env.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Keylet.h>

namespace xrpl::test {

// Build-level capability: does this branch implement a dust reservoir at
// all? This is a *build* capability, not a per-Vault fact: it stays `true`
// on a solution branch even when reading a Vault that happens to hold no
// dust (e.g. an XRP/MPT Vault, a Legacy Vault, or simply one that has never
// produced any). Per-Vault absence is expressed by readVaultDust() below
// returning zero, not by this flag.
inline constexpr bool kHasDustReservoir = false;  // base branch: no reservoir exists yet.

// Per-Vault: how much dust does this Vault currently hold, normalized to
// Vault-pseudo-account terms (i.e. a positive Number means "the Vault is
// carrying this much unrecognized value on the borrower's behalf").
//
// Returns zero when the build has no reservoir (this branch), and also when
// this particular Vault has none.
//
// `inline` is not optional: this header is included both by the shared
// suite and by each branch's own per-solution test file, so a non-inline
// definition would be a duplicate-symbol link error. If a solution's
// implementation grows past a few lines, move the body to a
// VaultDustProbe.cpp beside this header rather than dropping `inline`.
[[nodiscard]] inline Number
readVaultDust(jtx::Env const& env, Keylet const& vaultKeylet)
{
    (void)env;
    (void)vaultKeylet;
    return Number{};
}

}  // namespace xrpl::test
