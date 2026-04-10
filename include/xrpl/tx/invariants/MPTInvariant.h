#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

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

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Verify:
 *    - OutstandingAmount <= MaximumAmount for any MPT
 *    - OutstandingAmount after = OutstandingAmount before +
 *         sum (MPT after - MPT before) - this is total MPT credit/debit
 */
class ValidMPTPayment
{
    enum Order { Before = 0, After = 1 };
    struct MPTData
    {
        std::array<std::int64_t, After + 1> outstanding{};
        // sum (MPT after - MPT before)
        std::int64_t mptAmount{0};
    };

    // true if OutstandingAmount > MaximumAmount in after for any MPT
    bool overflow_{false};
    // mptid:MPTData
    hash_map<uint192, MPTData> data_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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
 * MPToken can only have encrypted fields if lsfMPTCanConfidentialAmount is set on
 * issuance.
 * - Encrypted field existence consistency:
 * If sfConfidentialBalanceSpending/sfConfidentialBalanceInbox exists, then
 * sfIssuerEncryptedBalance must also exist (and vice versa).
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
        bool requiresPrivacyFlag = false;
        bool badVersion = false;
    };
    std::map<uint192, Changes> changes_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
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

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
