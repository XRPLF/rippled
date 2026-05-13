/** @file
 *  Per-transaction-type privilege bitmasks used by the invariant checker.
 *
 *  Each transaction type published in `transactions.macro` carries a
 *  `Privilege` bitmask that declares what ledger mutations it is permitted to
 *  perform.  Invariant checkers call `hasPrivilege()` in their `finalize()`
 *  methods to enforce that no forbidden mutation occurred.
 *
 *  @note The `assert(enforce)` pattern: several invariant checker files
 *      contain `XRPL_ASSERT(enforce, ...)` where `enforce` is `true` only
 *      when the relevant amendment is active.  This is a deliberate two-layer
 *      defence strategy.  Invariants should never fire in production; the
 *      assert is aimed at developers — it fires in debug/unit-test builds
 *      when code violates an invariant without the protecting amendment being
 *      enabled, catching bugs as early as possible while remaining invisible
 *      to validators in release builds.
 */
#pragma once

#include <xrpl/protocol/STTx.h>

#include <type_traits>

namespace xrpl {

/** Bitmask of operations that a transaction type is permitted to perform.
 *
 *  Each enumerator represents one class of ledger mutation.  The `must*`
 *  variants mean the transaction is *required* to perform that mutation on
 *  success; the `may*` variants mean it is *permitted* but not required.
 *  Invariant checkers use this distinction to differentiate structural
 *  violations (wrong count) from policy violations (wrong type).
 *
 *  Privilege sets for each transaction type are declared in
 *  `transactions.macro` and composed with `operator|`.  Unknown or
 *  deprecated transaction types carry `NoPriv`.
 *
 *  @see hasPrivilege()
 *  @see transactions.macro
 */
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Privilege {
    /** The transaction may not perform any of the enumerated operations. */
    NoPriv = 0x0000,

    /** The transaction may create a new `ACCOUNT_ROOT` object. */
    CreateAcct = 0x0001,

    /** The transaction may create a pseudo-account `ACCOUNT_ROOT`.
     *
     *  Implies `CreateAcct`; checkers that permit `CreatePseudoAcct` also
     *  accept `CreateAcct`, but the converse is not true — a transaction
     *  with only `CreateAcct` must not create a pseudo-account.
     */
    CreatePseudoAcct = 0x0002,

    /** The transaction must delete exactly one `ACCOUNT_ROOT` on success.
     *
     *  Example: `ttACCOUNT_DELETE`, `ttAMM_DELETE`.
     */
    MustDeleteAcct = 0x0004,

    /** The transaction may delete one `ACCOUNT_ROOT`, but is not required to.
     *
     *  Example: `ttAMM_WITHDRAW`, `ttAMM_CLAWBACK` (when LP-token supply
     *  reaches zero).
     */
    MayDeleteAcct = 0x0008,

    /** The transaction may bypass certain freeze rules.
     *
     *  Example: `ttAMM_CLAWBACK` (clawback against AMM trust lines is
     *  permitted even under global freeze).
     */
    OverrideFreeze = 0x0010,

    /** The transaction may mint or burn an NFT, changing `sfMintedNFTokens`
     *  or `sfBurnedNFTokens` on an account root.
     */
    ChangeNftCounts = 0x0020,

    /** The transaction may create a new MPT issuance object. */
    CreateMptIssuance = 0x0040,

    /** The transaction may destroy an existing MPT issuance object. */
    DestroyMptIssuance = 0x0080,

    /** The transaction must create or delete an MPT holder object
     *  (non-issuer path).
     *
     *  Example: `ttMPTOKEN_AUTHORIZE` when the holder explicitly opts in or
     *  out.
     */
    MustAuthorizeMpt = 0x0100,

    /** The transaction may create or delete an MPT holder object
     *  (non-issuer path), but is not required to.
     *
     *  Example: `ttAMM_WITHDRAW`, `ttAMM_CLAWBACK`.
     */
    MayAuthorizeMpt = 0x0200,

    /** The transaction may delete an MPT holder object but may not create one.
     *
     *  Example: `ttMPTOKEN_ISSUANCE_DESTROY`.
     */
    MayDeleteMpt = 0x0400,

    /** The transaction must modify, create, or delete a vault object. */
    MustModifyVault = 0x0800,

    /** The transaction may modify, create, or delete a vault object,
     *  but is not required to.
     */
    MayModifyVault = 0x1000,

    /** The transaction may create an MPT holder object (non-issuer path).
     *
     *  Example: `ttPAYMENT`, `ttCHECK_CASH`.
     */
    MayCreateMpt = 0x2000,
};

/** Compose two `Privilege` bitmasks with bitwise-OR.
 *
 *  Used in `transactions.macro` to declare combined privilege sets such as
 *  `CreateAcct | MayCreateMpt`.  `safeCast` guards against accidental
 *  out-of-range integer conversions.
 *
 *  @param lhs Left-hand privilege set.
 *  @param rhs Right-hand privilege set.
 *  @return A `Privilege` value whose set bits are the union of `lhs` and `rhs`.
 */
constexpr Privilege
operator|(Privilege lhs, Privilege rhs)
{
    return safeCast<Privilege>(
        safeCast<std::underlying_type_t<Privilege>>(lhs) |
        safeCast<std::underlying_type_t<Privilege>>(rhs));
}

/** Query whether a transaction type holds a given privilege.
 *
 *  Implemented in `InvariantCheck.cpp` via an X-macro expansion of
 *  `transactions.macro`: each transaction type's `privileges` bitmask is
 *  AND-ed against `priv` and returned as a `bool`.  Deprecated or unknown
 *  transaction types return `false` (no privileges).
 *
 *  Called exclusively from invariant checker `finalize()` methods to
 *  determine whether a ledger mutation that was observed is permitted for
 *  the transaction type being applied.  Failed transactions carry no
 *  privileges regardless of type — checkers must guard on `isTesSuccess`
 *  separately when the privilege implies a required mutation.
 *
 *  @param tx  The transaction being applied.
 *  @param priv  The privilege bit (or OR-composed set of bits) to test.
 *  @return `true` if `tx`'s transaction type holds all bits in `priv`.
 *  @see Privilege
 */
bool
hasPrivilege(STTx const& tx, Privilege priv);

}  // namespace xrpl
