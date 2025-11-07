#ifndef XRPL_APP_TX_INVARIANTCHECK_H_INCLUDED
#define XRPL_APP_TX_INVARIANTCHECK_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <tuple>
#include <unordered_set>

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
    std::vector<std::shared_ptr<SLE const>> accountsDeleted_;

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
 * @brief Invariant: frozen trust line balance change is not allowed.
 *
 * We iterate all affected trust lines and ensure that they don't have
 * unexpected change of balance if they're frozen.
 */
class TransfersNotFrozen
{
    struct BalanceChange
    {
        std::shared_ptr<SLE const> const line;
        int const balanceChangeSign;
    };

    struct IssuerChanges
    {
        std::vector<BalanceChange> senders;
        std::vector<BalanceChange> receivers;
    };

    using ByIssuer = std::map<Issue, IssuerChanges>;
    ByIssuer balanceChanges_;

    std::map<AccountID, std::shared_ptr<SLE const> const> possibleIssuers_;

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

private:
    bool
    isValidEntry(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    STAmount
    calculateBalanceChange(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after,
        bool isDelete);

    void
    recordBalance(Issue const& issue, BalanceChange change);

    void
    recordBalanceChanges(
        std::shared_ptr<SLE const> const& after,
        STAmount const& balanceChange);

    std::shared_ptr<SLE const>
    findIssuer(AccountID const& issuerID, ReadView const& view);

    bool
    validateIssuerChanges(
        std::shared_ptr<SLE const> const& issuer,
        IssuerChanges const& changes,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce);

    bool
    validateFrozenState(
        BalanceChange const& change,
        bool high,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce,
        bool globalFreeze);
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
    std::uint32_t accountSeq_ = 0;
    bool pseudoAccount_ = false;
    std::uint32_t flags_ = 0;

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
 * @brief Invariant: Validates several invariants for NFToken pages.
 *
 * The following checks are made:
 *  - The page is correctly associated with the owner.
 *  - The page is correctly ordered between the next and previous links.
 *  - The page contains at least one and no more than 32 NFTokens.
 *  - The NFTokens on this page do not belong on a lower or higher page.
 *  - The NFTokens are correctly sorted on the page.
 *  - Each URI, if present, is not empty.
 */
class ValidNFTokenPage
{
    bool badEntry_ = false;
    bool badLink_ = false;
    bool badSort_ = false;
    bool badURI_ = false;
    bool invalidSize_ = false;
    bool deletedFinalPage_ = false;
    bool deletedLink_ = false;

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
 * @brief Invariant: Validates counts of NFTokens after all transaction types.
 *
 * The following checks are made:
 *  - The number of minted or burned NFTokens can only be changed by
 *    NFTokenMint or NFTokenBurn transactions.
 *  - A successful NFTokenMint must increase the number of NFTokens.
 *  - A failed NFTokenMint must not change the number of minted NFTokens.
 *  - An NFTokenMint transaction cannot change the number of burned NFTokens.
 *  - A successful NFTokenBurn must increase the number of burned NFTokens.
 *  - A failed NFTokenBurn must not change the number of burned NFTokens.
 *  - An NFTokenBurn transaction cannot change the number of minted NFTokens.
 */
class NFTokenCountTracking
{
    std::uint32_t beforeMintedTotal = 0;
    std::uint32_t beforeBurnedTotal = 0;
    std::uint32_t afterMintedTotal = 0;
    std::uint32_t afterBurnedTotal = 0;

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

class ValidMPTIssuance
{
    std::uint32_t mptIssuancesCreated_ = 0;
    std::uint32_t mptIssuancesDeleted_ = 0;

    std::uint32_t mptokensCreated_ = 0;
    std::uint32_t mptokensDeleted_ = 0;

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
 * @brief Invariants: Permissioned Domains must have some rules and
 * AcceptedCredentials must have length between 1 and 10 inclusive.
 *
 * Since only permissions constitute rules, an empty credentials list
 * means that there are no rules and the invariant is violated.
 *
 * Credentials must be sorted and no duplicates allowed
 *
 */
class ValidPermissionedDomain
{
    struct SleStatus
    {
        std::size_t credentialsSize_{0};
        bool isSorted_ = false, isUnique_ = false;
    };
    std::optional<SleStatus> sleStatus_[2];

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
 * @brief Invariants: Pseudo-accounts have valid and consisent properties
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

class ValidPermissionedDEX
{
    bool regularOffers_ = false;
    bool badHybrids_ = false;
    hash_set<uint256> domains_;

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

class ValidAMM
{
    std::optional<AccountID> ammAccount_;
    std::optional<STAmount> lptAMMBalanceAfter_;
    std::optional<STAmount> lptAMMBalanceBefore_;
    bool ammPoolChanged_;

public:
    enum class ZeroAllowed : bool { No = false, Yes = true };

    ValidAMM() : ammPoolChanged_{false}
    {
    }
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

private:
    bool
    finalizeBid(bool enforce, beast::Journal const&) const;
    bool
    finalizeVote(bool enforce, beast::Journal const&) const;
    bool
    finalizeCreate(
        STTx const&,
        ReadView const&,
        bool enforce,
        beast::Journal const&) const;
    bool
    finalizeDelete(bool enforce, TER res, beast::Journal const&) const;
    bool
    finalizeDeposit(
        STTx const&,
        ReadView const&,
        bool enforce,
        beast::Journal const&) const;
    // Includes clawback
    bool
    finalizeWithdraw(
        STTx const&,
        ReadView const&,
        bool enforce,
        beast::Journal const&) const;
    bool
    finalizeDEX(bool enforce, beast::Journal const&) const;
    bool
    generalInvariant(
        STTx const&,
        ReadView const&,
        ZeroAllowed zeroAllowed,
        beast::Journal const&) const;
};

/**
 * @brief Invariants: Vault object and MPTokenIssuance for vault shares
 *
 * - vault deleted and vault created is empty
 * - vault created must be linked to pseudo-account for shares and assets
 * - vault must have MPTokenIssuance for shares
 * - vault without shares outstanding must have no shares
 * - loss unrealized does not exceed the difference between assets total and
 *   assets available
 * - assets available do not exceed assets total
 * - vault deposit increases assets and share issuance, and adds to:
 *   total assets, assets available, shares outstanding
 * - vault withdrawal and clawback reduce assets and share issuance, and
 *   subtracts from: total assets, assets available, shares outstanding
 * - vault set must not alter the vault assets or shares balance
 * - no vault transaction can change loss unrealized (it's updated by loan
 *   transactions)
 *
 */
class ValidVault
{
    Number static constexpr zero{};

    struct Vault final
    {
        uint256 key = beast::zero;
        Asset asset = {};
        AccountID pseudoId = {};
        uint192 shareMPTID = beast::zero;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;

        Vault static make(SLE const&);
    };

    struct Shares final
    {
        MPTIssue share = {};
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        Shares static make(SLE const&);
    };

    std::vector<Vault> afterVault_ = {};
    std::vector<Shares> afterMPTs_ = {};
    std::vector<Vault> beforeVault_ = {};
    std::vector<Shares> beforeMPTs_ = {};
    std::unordered_map<uint256, Number> deltas_ = {};

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
    ValidPseudoAccounts,
    ValidVault>;

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
