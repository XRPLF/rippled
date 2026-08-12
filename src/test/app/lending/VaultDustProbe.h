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
// readVaultDust() below is the whole seam: each solution branch reimplements
// its body and changes nothing else. There is deliberately no "does this
// build have a reservoir?" flag — the shared suite asserts the post-fix
// oracles unconditionally, so on the base branch it fails, and that failure
// is the bug's demonstration (see the RED/GREEN CONTRACT note in
// VaultRounding_test.cpp).

#include <test/jtx/Env.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl::test {

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
    auto const vaultSle = env.le(vaultKeylet);
    if (!vaultSle)
        return Number{};

    xrpl::Asset const asset = vaultSle->at(sfAsset);
    if (asset.integral())
        return Number{};

    auto const vaultAccount = vaultSle->at(sfAccount);
    auto const line =
        env.current()->read(xrpl::keylet::trustLine(vaultAccount, asset.get<xrpl::Issue>()));
    if (!line)
        return Number{};

    // sfDust follows sfBalance's own low/high sign convention: positive
    // means the low account holds the high account's IOUs. Undo it here so
    // the shared suite never has to think about which endpoint of the line
    // is low — see plan-vault-dust-b-prime-field-accounting-kept.md §4.
    bool const vaultIsHigh = vaultAccount > asset.getIssuer();
    Number const dust = line->at(sfDust);
    return vaultIsHigh ? -dust : dust;
}

}  // namespace xrpl::test
