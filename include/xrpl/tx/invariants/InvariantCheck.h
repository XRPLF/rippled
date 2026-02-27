#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/invariants/AMMInvariant.h>
#include <xrpl/tx/invariants/FreezeInvariant.h>
#include <xrpl/tx/invariants/LoanInvariant.h>
#include <xrpl/tx/invariants/MPTInvariant.h>
#include <xrpl/tx/invariants/NFTInvariant.h>
#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>
#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <cstdint>
#include <tuple>

namespace xrpl {

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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: a deleted account must not have any objects left
 *
 * We iterate all deleted account roots, and ensure that there are no
 * objects left that are directly accessible with that account's ID.
 *
 * There should only be one deleted account, but that's checked by
 * AccountRootsNotDeleted. This invariant will handle multiple deleted account
 * roots without a problem.
 */
class AccountRootsDeletedClean
{
    // Pair is <before, after>. Before is used for most of the checks, so that
    // if, for example, an object ID field is cleared, but the object is not
    // deleted, it can still be found. After is used specifically for any checks
    // that are expected as part of the deletion, such as zeroing out the
    // balance.
    std::vector<std::pair<std::shared_ptr<SLE const>, std::shared_ptr<SLE const>>> accountsDeleted_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: Trust lines with deep freeze flag are not allowed if normal
 * freeze flag is not set.
 *
 * We iterate all the trust lines created by this transaction and ensure
 * that they don't have deep freeze flag set without normal freeze flag set.
 */
class NoDeepFreezeTrustLinesWithoutFreeze
{
    bool deepFreezeWithoutFreeze_ = false;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: a new account root must be the consequence of a payment,
 *                   must have the right starting sequence, and the payment
 *                   may not create more than one new account root.
 */
class ValidNewAccountRoot
{
    std::uint32_t accountsCreated_ = 0;
    std::uint32_t accountSeq_ = 0;
    bool pseudoAccount_ = false;
    std::uint32_t flags_ = 0;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: Token holder's trustline balance cannot be negative after
 * Clawback.
 *
 * We iterate all the trust lines affected by this transaction and ensure
 * that no more than one trustline is modified, and also holder's balance is
 * non-negative.
 */
class ValidClawback
{
    std::uint32_t trustlinesChanged = 0;
    std::uint32_t mptokensChanged = 0;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariants: Pseudo-accounts have valid and consistent properties
 *
 * Pseudo-accounts have certain properties, and some of those properties are
 * unique to pseudo-accounts. Check that all pseudo-accounts are following the
 * rules, and that only pseudo-accounts look like pseudo-accounts.
 *
 */
class ValidPseudoAccounts
{
    std::vector<std::string> errors_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariants: Some fields are unmodifiable
 *
 * Check that any fields specified as unmodifiable are not modified when the
 * object is modified. Creation and deletion are ignored.
 *
 */
class NoModifiedUnmodifiableFields
{
    // Pair is <before, after>.
    std::set<std::pair<SLE::const_pointer, SLE::const_pointer>> changedEntries_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

// additional invariant checks can be declared above and then added to this
// tuple
using InvariantChecks = std::tuple<
    TransactionFeeCheck,
    AccountRootsNotDeleted,
    AccountRootsDeletedClean,
    LedgerEntryTypesMatch,
    XRPBalanceChecks,
    XRPNotCreated,
    NoXRPTrustLines,
    NoDeepFreezeTrustLinesWithoutFreeze,
    TransfersNotFrozen,
    NoBadOffers,
    NoZeroEscrow,
    ValidNewAccountRoot,
    ValidNFTokenPage,
    NFTokenCountTracking,
    ValidClawback,
    ValidMPTIssuance,
    ValidPermissionedDomain,
    ValidPermissionedDEX,
    ValidAMM,
    NoModifiedUnmodifiableFields,
    ValidPseudoAccounts,
    ValidLoanBroker,
    ValidLoan,
    ValidVault>;

/**
 * @brief get a tuple of all invariant checks
 *
 * @return std::tuple of instances that implement the required invariant check
 * methods
 *
 * @see xrpl::InvariantChecker_PROTOTYPE
 */
inline InvariantChecks
getInvariantChecks()
{
    return InvariantChecks{};
}

}  // namespace xrpl
