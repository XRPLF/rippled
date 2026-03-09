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
 * MPToken can only have encrypted fields if lsfMPTCanPrivacy is set on
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

}  // namespace xrpl
