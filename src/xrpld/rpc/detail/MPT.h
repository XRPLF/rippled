/** @file
 *  Defines `PathFindMPT`, a compact snapshot of MPT ledger state used
 *  during path construction to avoid redundant ledger reads.
 */

#pragma once

#include <xrpl/protocol/MPTIssue.h>

namespace xrpl {

/** Immutable ledger-state snapshot for a single MPT used during pathfinding.
 *
 *  Carries an `MPTID` together with two boolean flags that encode
 *  economically relevant state: whether the account's balance is zero, and
 *  whether the issuance has reached its supply ceiling.  Both flags are
 *  read once by `AssetCache::getMPTs()` and cached so that the pathfinder's
 *  combinatorial search can evaluate candidate paths without re-reading the
 *  ledger on every iteration.
 *
 *  This type is the MPT analogue of `PathFindTrustLine`.  It is `final`,
 *  all members are `const`, and no mutation methods are provided — it is a
 *  read-once snapshot, not a live view.
 *
 *  The implicit conversion to `MPTID const&` lets templated pathfinder
 *  code pass a `PathFindMPT` wherever an `MPTID` is expected (e.g.
 *  `getMPTIssuer(asset)`) without knowing the concrete type.
 *
 *  @see AssetCache::getMPTs()
 *  @see PathFindTrustLine
 */
class PathFindMPT final
{
private:
    MPTID const mptID_;
    /** True when the holder's `sfMPTAmount` is zero; always `false` for the
     *  issuer.  A holder with a zero balance can receive but not send, which
     *  the pathfinder uses to prune outbound paths through that account.
     */
    bool const zeroBalance_;
    /** True when `sfOutstandingAmount` equals the issuance's maximum.
     *  Computed for the issuer directly from `ltMPTOKEN_ISSUANCE`; for
     *  holders, the associated issuance is looked up and the flag defaults
     *  to `true` if that ledger entry is missing (conservative: treat
     *  orphaned issuances as non-viable routes).
     */
    bool const maxedOut_;

public:
    /** Construct with only an MPTID, setting both flags to `false`.
     *
     *  Convenience constructor used when the caller knows a priori that the
     *  balance is non-zero and the issuance is not maxed out (e.g. when
     *  synthesising a `PathFindMPT` from an `MPTIssue` for uniform
     *  interface with `PathFindTrustLine`).
     *
     *  @param mptID The 192-bit Multi-Party Token identifier.
     */
    PathFindMPT(MPTID const& mptID) : mptID_(mptID), zeroBalance_(false), maxedOut_(false)
    {
    }

    /** Construct with explicit state flags captured from the ledger.
     *
     *  @param mptID       The 192-bit Multi-Party Token identifier.
     *  @param zeroBalance `true` if the account's `sfMPTAmount` is zero.
     *      Always pass `false` for the issuer.
     *  @param maxedOut    `true` if `sfOutstandingAmount` equals the
     *      issuance maximum, or if the issuance ledger entry is missing.
     */
    PathFindMPT(MPTID const& mptID, bool zeroBalance, bool maxedOut)
        : mptID_(mptID), zeroBalance_(zeroBalance), maxedOut_(maxedOut)
    {
    }

    /** Implicit conversion to `MPTID const&`.
     *
     *  Allows a `PathFindMPT` to be passed directly to functions that
     *  accept an `MPTID`, such as `getMPTIssuer()`, so that templated
     *  pathfinder loops need not specialise on the asset-vector type.
     */
    operator MPTID const&() const
    {
        return mptID_;
    }

    /** Return the underlying `MPTID`.
     *
     *  Named accessor that mirrors `MPTIssue::getMptID()`, providing an
     *  unambiguous alternative to the implicit conversion in contexts where
     *  overload resolution or `constexpr` evaluation requires it.
     *
     *  @return A const reference to the stored `MPTID`.
     */
    [[nodiscard]] MPTID const&
    getMptID() const
    {
        return mptID_;
    }

    /** Return whether the account's MPToken balance is zero.
     *
     *  When `true`, the pathfinder skips this asset for outbound path
     *  segments — a holder with no balance cannot fund a payment.  Always
     *  `false` for the issuer, which has no `sfMPTAmount`.
     *
     *  @return `true` if `sfMPTAmount == 0` for a holder; always `false`
     *      for the issuer.
     */
    [[nodiscard]] bool
    isZeroBalance() const
    {
        return zeroBalance_;
    }

    /** Return whether the MPT issuance has reached its supply ceiling.
     *
     *  When `true`, no further tokens can be minted, so paths that would
     *  require the issuer to create new supply are not viable.  Defaults to
     *  `true` when the associated `ltMPTOKEN_ISSUANCE` ledger entry cannot
     *  be found for a holder (conservative: orphaned issuances are treated
     *  as non-viable).
     *
     *  @return `true` if `sfOutstandingAmount == maxMPTAmount`, or if the
     *      issuance ledger entry is absent.
     */
    [[nodiscard]] bool
    isMaxedOut() const
    {
        return maxedOut_;
    }
};

}  // namespace xrpl
