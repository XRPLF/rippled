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
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

namespace ripple {

class ReadView;

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
    explicit InvariantChecker_PROTOTYPE() = default;

    /**
     * @brief called for each ledger entry in the current transaction.
     *
     * @param isDelete true if the SLE is being deleted
     * @param before ledger entry before modification by the transaction
     * @param after ledger entry after modification by the transaction
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief called after all ledger entries have been visited to determine
     * the final status of the check
     *
     * @param tx the transaction being applied
     * @param tec the current TER result of the transaction
     * @param fee the fee actually charged for this transaction
     * @param view a ReadView of the ledger being modified
     * @param j journal for logging
     *
     * @return true if check passes, false if it fails
     */
    bool
    finalize(
        STTx const& tx,
        TER const tec,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};
#endif

/**
 * @brief Invariant: We should never charge a transaction a negative fee or a
 * fee that is larger than what the transaction itself specifies.
 *
 * We can, in some circumstances, charge less.
 */
class TransactionFeeCheck
{
public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: A transaction must not create XRP and should only destroy
 * the XRP fee.
 *
 * We iterate through all account roots, payment channels and escrow entries
 * that were modified and calculate the net change in XRP caused by the
 * transactions.
 */
class XRPNotCreated
{
    std::int64_t drops_ = 0;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: we cannot remove an account ledger entry
 *
 * We iterate all account roots that were modified, and ensure that any that
 * were present before the transaction was applied continue to be present
 * afterwards unless they were explicitly deleted by a successful
 * AccountDelete transaction.
 */
class AccountRootsNotDeleted
{
    std::uint32_t accountsDeleted_ = 0;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: An account XRP balance must be in XRP and take a value
 *                   between 0 and INITIAL_XRP drops, inclusive.
 *
 * We iterate all account roots modified by the transaction and ensure that
 * their XRP balances are reasonable.
 */
class XRPBalanceChecks
{
    bool bad_ = false;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
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
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: Trust lines using XRP are not allowed.
 *
 * We iterate all the trust lines created by this transaction and ensure
 * that they are against a valid issuer.
 */
class NoXRPTrustLines
{
    bool xrpTrustLine_ = false;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: offers should be for non-negative amounts and must not
 *                   be XRP to XRP.
 *
 * Examine all offers modified by the transaction and ensure that there are
 * no offers which contain negative amounts or which exchange XRP for XRP.
 */
class NoBadOffers
{
    bool bad_ = false;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: an escrow entry must take a value between 0 and
 *                   INITIAL_XRP drops exclusive.
 */
class NoZeroEscrow
{
    bool bad_ = false;

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

/**
 * @brief Invariant: a new account root must be the consequence of a payment,
 *                   must have the right starting sequence, and the payment
 *                   may not create more than one new account root.
 */
class ValidNewAccountRoot
{
    std::uint32_t accountsCreated_ = 0;
    std::uint32_t accountSeq_ = 0;  // Only meaningful if accountsCreated_ > 0

public:
    void
    visitEntry(
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&);

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

// additional invariant checks can be declared above and then added to this
// tuple
using InvariantChecks = std::tuple<
    TransactionFeeCheck,
    AccountRootsNotDeleted,
    LedgerEntryTypesMatch,
    XRPBalanceChecks,
    XRPNotCreated,
    NoXRPTrustLines,
    NoBadOffers,
    NoZeroEscrow,
    ValidNewAccountRoot>;

/**
 * @brief get a tuple of all invariant checks
 *
 * @return std::tuple of instances that implement the required invariant check
 * methods
 *
 * @see ripple::InvariantChecker_PROTOTYPE
 */
inline InvariantChecks
getInvariantChecks()
{
    return InvariantChecks{};
}

}  // namespace ripple

#endif
