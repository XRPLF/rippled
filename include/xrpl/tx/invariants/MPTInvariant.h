#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace xrpl {

class ValidMPTIssuance
{
    std::uint32_t mptIssuancesCreated_ = 0;
    std::uint32_t mptIssuancesDeleted_ = 0;

    std::uint32_t mptokensCreated_ = 0;
    std::uint32_t mptokensDeleted_ = 0;
    // non-MPT transactions may attempt to create
    // MPToken by an issuer
    bool mptCreatedByIssuer_ = false;

    /// sfReferenceHolding is intended to be set exactly once at vault
    /// creation and immutable thereafter; true when that rule was violated.
    bool referenceHoldingSetOnCreate_ = false;

    /// True when sfReferenceHolding was mutated on an existing MPTokenIssuance.
    bool referenceHoldingMutated_ = false;

    /// MPTokens and RippleStates deleted during apply. finalize() checks each
    /// holder's AccountRoot to detect vault pseudo-account holdings deleted
    /// outside VaultDelete. All these checks are gated on fixCleanup3_2_0.
    std::vector<std::shared_ptr<SLE const>> deletedHoldings_;

public:
    /**
     * @brief Track MPT issuance and holding creations, deletions, and
     * mutations.
     *
     * @param isDelete Whether the ledger entry is being deleted.
     * @param before The ledger entry before transaction application.
     * @param after The ledger entry after transaction application.
     */
    void
    visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after);

    /**
     * @brief Verify MPT issuance invariants after transaction application.
     *
     * @param tx The transaction being checked.
     * @param result The transaction result code.
     * @param fee The fee charged by the transaction.
     * @param view The ledger view after transaction application.
     * @param j Journal used for diagnostics.
     * @return true if the invariant checks pass, otherwise false.
     */
    [[nodiscard]] bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j) const;
};

/**
 * @brief Verify public MPT amount and outstanding amount accounting.
 *
 * Checks that OutstandingAmount does not exceed MaximumAmount and that
 * OutstandingAmount after application equals OutstandingAmount before
 * application plus the net holder balance delta.
 */
class ValidMPTPayment
{
    enum class Order { Before = 0, After = 1 };
    struct MPTData
    {
        std::array<std::int64_t, 2> outstanding{};
        // sum (MPT after - MPT before)
        std::int64_t mptAmount{0};
    };

    // true if OutstandingAmount > MaximumAmount in after for any MPT
    bool overflow_{false};
    // mptid:MPTData
    hash_map<uint192, MPTData> data_;

public:
    /**
     * @brief Track MPT amount and outstanding amount changes.
     *
     * @param isDelete Whether the ledger entry is being deleted.
     * @param before The ledger entry before transaction application.
     * @param after The ledger entry after transaction application.
     */
    void
    visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after);

    /**
     * @brief Verify public MPT payment accounting invariants.
     *
     * @param tx The transaction being checked.
     * @param result The transaction result code.
     * @param fee The fee charged by the transaction.
     * @param view The ledger view after transaction application.
     * @param j Journal used for diagnostics.
     * @return true if the invariant checks pass, otherwise false.
     */
    bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};

/**
 * @brief Invariants: Confidential MPToken consistency
 *
 * - Convert/ConvertBack symmetry:
 * Regular MPToken balance change (±X) == COA (Confidential Outstanding Amount) change (∓X)
 * - Cannot delete MPToken with non-zero confidential state:
 * Cannot delete if sfIssuerEncryptedBalance exists
 * Cannot delete if sfConfidentialBalanceInbox and sfConfidentialBalanceSpending exist
 * - Privacy flag consistency:
 * MPToken confidential balance fields can only be created or changed if
 * lsfMPTCanHoldConfidentialBalance is set on the issuance.
 * - Encrypted field existence consistency:
 * If sfConfidentialBalanceSpending/sfConfidentialBalanceInbox exists, then
 * sfIssuerEncryptedBalance must also exist (and vice versa). If
 * sfAuditorEncryptedBalance exists, then those core encrypted balance fields
 * must also exist.
 * - COA <= OutstandingAmount:
 * Confidential outstanding balance cannot exceed total outstanding.
 * - Verifies sfConfidentialBalanceVersion is changed whenever sfConfidentialBalanceSpending is
 * modified on an MPToken.
 */
class ValidConfidentialMPToken
{
    struct Changes
    {
        std::int64_t mptAmountDelta = 0;
        std::int64_t coaDelta = 0;
        std::int64_t outstandingDelta = 0;
        SLE::const_pointer issuance;
        bool deletedWithEncrypted = false;
        bool badConsistency = false;
        bool badCOA = false;
        bool changesConfidentialFields = false;
        bool badVersion = false;
    };
    std::map<uint192, Changes> changes_;

public:
    /**
     * @brief Track confidential MPT balance, issuance, and version changes.
     *
     * @param isDelete Whether the ledger entry is being deleted.
     * @param before The ledger entry before transaction application.
     * @param after The ledger entry after transaction application.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief Verify confidential MPT accounting and encrypted-field
     * invariants.
     *
     * @param tx The transaction being checked.
     * @param result The transaction result code.
     * @param fee The fee charged by the transaction.
     * @param view The ledger view after transaction application.
     * @param j Journal used for diagnostics.
     * @return true if the invariant checks pass, otherwise false.
     */
    bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};

class ValidMPTTransfer
{
    struct Value
    {
        std::optional<std::uint64_t> amtBefore;
        std::optional<std::uint64_t> amtAfter;
    };
    // MPTID: {holder: Value}
    hash_map<uint192, hash_map<AccountID, Value>> amount_;
    // Deleted MPToken
    // MPToken key: true if MPTAuthorized is set
    hash_map<uint256, bool> deletedAuthorized_;

public:
    /**
     * @brief Track MPT balance changes and deleted authorization state.
     *
     * @param isDelete Whether the ledger entry is being deleted.
     * @param before The ledger entry before transaction application.
     * @param after The ledger entry after transaction application.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief Verify MPT transfer authorization invariants.
     *
     * @param tx The transaction being checked.
     * @param result The transaction result code.
     * @param fee The fee charged by the transaction.
     * @param view The ledger view after transaction application.
     * @param j Journal used for diagnostics.
     * @return true if the invariant checks pass, otherwise false.
     */
    bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);

private:
    /**
     * @brief Check whether a holder is authorized to send or receive an MPToken.
     *
     * Deleted MPToken SLEs are no longer present in the view by the time
     * finalize() runs, so their authorization state is captured during
     * visitEntry() and stored in deletedAuthorized_. For deleted MPTokens,
     * returns true if reqAuth is false or lsfMPTAuthorized was set at deletion.
     * For existing MPTokens, returns the result of requireAuth().
     *
     * @param view The ledger view after transaction application.
     * @param mptid The MPToken issuance ID.
     * @param holder The holder account being checked.
     * @param requireAuth Whether the issuance requires explicit authorization.
     * @return true if the holder is authorized, otherwise false.
     */
    [[nodiscard]] bool
    isAuthorized(
        ReadView const& view,
        MPTID const& mptid,
        AccountID const& holder,
        bool requireAuth) const;
};

}  // namespace xrpl
