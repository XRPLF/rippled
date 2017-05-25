//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_TX_INVARIANTCHECK_H_INCLUDED
#define RIPPLE_APP_TX_INVARIANTCHECK_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <ripple/beast/utility/Journal.h>
#include <tuple>
#include <cstdint>

namespace ripple {

#if GENERATING_DOCS
/**
 * @brief Prototype for invariant check implementations.
 *
 * __THIS CLASS DOES NOT EXIST__ - or rather it exists in documentation only to
 * communicate the interface required of any invariant checker. Any invariant
 * check implementation should implement the public methods documented here.
 *
 */
class InvariantChecker_PROTOTYPE
{
public:

    /**
     * @brief called for each ledger entry in the current transaction.
     *
     * @param index the key (identifier) for the ledger entry
     * @param isDelete true if the SLE is being deleted
     * @param before ledger entry before modification by the transaction
     * @param after ledger entry after modification by the transaction
     */
    void
    visitEntry(
        uint256 const& index,
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief called after all ledger entries have been visited to determine
     * the final status of the check
     *
     * @param tx the transaction being applied
     * @param tec the current TER result of the transaction
     * @param j journal for logging
     *
     * @return true if check passes, false if it fails
     */
    bool
    finalize(
        STTx const& tx,
        TER tec,
        beast::Journal const& j);
};
#endif

/**
 * @brief Invariant: A transaction must not create XRP and should only destroy
 * XRP, up to the transaction fee.
 *
 * For this check, we start with a signed 64-bit integer set to zero. As we go
 * through the ledger entries, look only at account roots, escrow payments,
 * and payment channels.  Remove from the total any previous XRP values and add
 * to the total any new XRP values. The net balance of a payment channel is
 * computed from two fields (amount and balance) and deletions are ignored
 * for paychan and escrow because the amount fields have not been adjusted for
 * those in the case of deletion.
 *
 * The final total must be less than or equal to zero and greater than or equal
 * to the negative of the tx fee.
 *
 */
class XRPNotCreated
{
    std::int64_t drops_ = 0;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
};

/**
 * @brief Invariant: we cannot remove an account ledger entry
 *
 * an account root should never be the target of a delete
 */
class AccountRootsNotDeleted
{
    bool accountDeleted_ = false;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
};

/**
 * @brief Invariant: An account XRP balance must be in XRP and take a value
                     between 0 and SYSTEM_CURRENCY_START drops, inclusive.
 */
class XRPBalanceChecks
{
    bool bad_ = false;

public:
    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
};

/**
 * @brief Invariant: corresponding modified ledger entries should match in type
 *                   and added entries should be a valid type.
 */
class LedgerEntryTypesMatch
{
    bool typeMismatch_ = false;
    bool invalidTypeAdded_ = false;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
};

/**
 * @brief Invariant: Trust lines using XRP are not allowed.
 */
class NoXRPTrustLines
{
    bool xrpTrustLine_ = false;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
};

/**
 * @brief Invariant: offers should be for non-negative amounts and must not
 *                   be XRP to XRP.
 */
class NoBadOffers
{
    bool bad_ = false;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
    
};

/**
 * @brief Invariant: an escrow entry must take a value between 0 and
 *                   SYSTEM_CURRENCY_START drops exclusive.
 */
class NoZeroEscrow
{
    bool bad_ = false;

public:

    void
    visitEntry(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER, beast::Journal const&);
    
};

// additional invariant checks can be declared above and then added to this
// tuple
using InvariantChecks = std::tuple<
    AccountRootsNotDeleted,
    LedgerEntryTypesMatch,
    XRPBalanceChecks,
    XRPNotCreated,
    NoXRPTrustLines,
    NoBadOffers,
    NoZeroEscrow
>;

/**
 * @brief get a tuple of all invariant checks
 *
 * @return std::tuple of instances that implement the required invariant check
 * methods
 *
 * @see ripple::InvariantChecker_PROTOTYPE
 */
inline
InvariantChecks
getInvariantChecks()
{
    return InvariantChecks{};
}

} //ripple

#endif
